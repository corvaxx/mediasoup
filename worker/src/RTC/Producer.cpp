#define MS_CLASS "RTC::Producer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Producer.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Utils.hpp"
#include "Channel/Notifier.hpp"
#include "RTC/Codecs/Tools.hpp"
#include "RTC/RTCP/FeedbackPs.hpp"
#include "RTC/RTCP/FeedbackRtp.hpp"
#include "RTC/RTCP/XrReceiverReferenceTime.hpp"
#include <cstring>  // std::memcpy()
#include <iterator> // std::ostream_iterator
#include <sstream>  // std::ostringstream
#include <chrono>
#include <iostream>

uint32_t random32(int type);

namespace RTC
{
    /* Instance methods. */

    Producer::Producer(const std::string & id, 
                       RTC::AbstractProducer::Listener * listener, 
                       json & data)
      : AbstractProducer(id, listener)
      , m_timer(this)
    {
        MS_TRACE();

        auto jsonKindIt = data.find("kind");

        if (jsonKindIt == data.end() || !jsonKindIt->is_string())
        {
            MS_THROW_TYPE_ERROR("missing kind");
        }

        // This may throw.
        this->kind = RTC::Media::GetKind(jsonKindIt->get<std::string>());

        if (this->kind == RTC::Media::Kind::ALL)
        {
            MS_THROW_TYPE_ERROR("invalid empty kind");
        }

        auto jsonRtpParametersIt = data.find("rtpParameters");

        if (jsonRtpParametersIt == data.end() || !jsonRtpParametersIt->is_object())
        {
            MS_THROW_TYPE_ERROR("missing rtpParameters");
        }

        // This may throw.
        this->rtpParameters = RTC::RtpParameters(*jsonRtpParametersIt);

        // Evaluate type.
        this->type = RTC::RtpParameters::GetType(this->rtpParameters);

        // Reserve a slot in rtpStreamByEncodingIdx and rtpStreamsScores vectors
        // for each RTP stream.
        this->rtpStreamByEncodingIdx.resize(this->rtpParameters.encodings.size(), nullptr);
        this->rtpStreamScores.resize(this->rtpParameters.encodings.size(), 0u);

        auto& encoding   = this->rtpParameters.encodings[0];
        auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);

        if (!RTC::Codecs::Tools::IsValidTypeForCodec(this->type, mediaCodec->mimeType))
        {
            MS_THROW_TYPE_ERROR(
              "%s codec not supported for %s",
              mediaCodec->mimeType.ToString().c_str(),
              RTC::RtpParameters::GetTypeString(this->type).c_str());
        }

        auto jsonRtpMappingIt = data.find("rtpMapping");

        if (jsonRtpMappingIt == data.end() || !jsonRtpMappingIt->is_object())
        {
            MS_THROW_TYPE_ERROR("missing rtpMapping");
        }

        auto jsonCodecsIt = jsonRtpMappingIt->find("codecs");

        if (jsonCodecsIt == jsonRtpMappingIt->end() || !jsonCodecsIt->is_array())
        {
            MS_THROW_TYPE_ERROR("missing rtpMapping.codecs");
        }

        for (auto& codec : *jsonCodecsIt)
        {
            if (!codec.is_object())
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (not an object)");
            }

            auto jsonPayloadTypeIt = codec.find("payloadType");

            // clang-format off
            if (
                jsonPayloadTypeIt == codec.end() ||
                !Utils::Json::IsPositiveInteger(*jsonPayloadTypeIt)
            )
            // clang-format on
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (missing payloadType)");
            }

            auto jsonMappedPayloadTypeIt = codec.find("mappedPayloadType");

            // clang-format off
            if (
                jsonMappedPayloadTypeIt == codec.end() ||
                !Utils::Json::IsPositiveInteger(*jsonMappedPayloadTypeIt)
            )
            // clang-format on
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.codecs (missing mappedPayloadType)");
            }

            this->rtpMapping.codecs[jsonPayloadTypeIt->get<uint8_t>()] =
              jsonMappedPayloadTypeIt->get<uint8_t>();
        }

        auto jsonEncodingsIt = jsonRtpMappingIt->find("encodings");

        if (jsonEncodingsIt == jsonRtpMappingIt->end() || !jsonEncodingsIt->is_array())
        {
            MS_THROW_TYPE_ERROR("missing rtpMapping.encodings");
        }

        this->rtpMapping.encodings.reserve(jsonEncodingsIt->size());

        for (auto& encoding : *jsonEncodingsIt)
        {
            if (!encoding.is_object())
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings");
            }

            this->rtpMapping.encodings.emplace_back();

            auto& encodingMapping = this->rtpMapping.encodings.back();

            // ssrc is optional.
            auto jsonSsrcIt = encoding.find("ssrc");

            // clang-format off
            if (
                jsonSsrcIt != encoding.end() &&
                Utils::Json::IsPositiveInteger(*jsonSsrcIt)
            )
            // clang-format on
            {
                encodingMapping.ssrc = jsonSsrcIt->get<uint32_t>();
            }

            // rid is optional.
            auto jsonRidIt = encoding.find("rid");

            if (jsonRidIt != encoding.end() && jsonRidIt->is_string())
            {
                encodingMapping.rid = jsonRidIt->get<std::string>();
            }

            // However ssrc or rid must be present (if more than 1 encoding).
            // clang-format off
            if (
                jsonEncodingsIt->size() > 1 &&
                jsonSsrcIt == encoding.end() &&
                jsonRidIt == encoding.end()
            )
            // clang-format on
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings (missing ssrc or rid)");
            }

            // If there is no mid and a single encoding, ssrc or rid must be present.
            // clang-format off
            if (
                this->rtpParameters.mid.empty() &&
                jsonEncodingsIt->size() == 1 &&
                jsonSsrcIt == encoding.end() &&
                jsonRidIt == encoding.end()
            )
            // clang-format on
            {
                MS_THROW_TYPE_ERROR(
                  "wrong entry in rtpMapping.encodings (missing ssrc or rid, or rtpParameters.mid)");
            }

            // mappedSsrc is mandatory.
            auto jsonMappedSsrcIt = encoding.find("mappedSsrc");

            // clang-format off
            if (
                jsonMappedSsrcIt == encoding.end() ||
                !Utils::Json::IsPositiveInteger(*jsonMappedSsrcIt)
            )
            // clang-format on
            {
                MS_THROW_TYPE_ERROR("wrong entry in rtpMapping.encodings (missing mappedSsrc)");
            }

            encodingMapping.mappedSsrc = jsonMappedSsrcIt->get<uint32_t>();

            if (translateMode != direct)
            {
                m_timer.Start(m_timerDelay, m_timerDelay);
            }
        }

        auto jsonPausedIt = data.find("paused");

        if (jsonPausedIt != data.end() && jsonPausedIt->is_boolean())
        {
            this->paused = jsonPausedIt->get<bool>();
        }

        // The number of encodings in rtpParameters must match the number of encodings
        // in rtpMapping.
        if (this->rtpParameters.encodings.size() != this->rtpMapping.encodings.size())
        {
            MS_THROW_TYPE_ERROR("rtpParameters.encodings size does not match rtpMapping.encodings size");
        }

        // Fill RTP header extension ids.
        // This may throw.
        for (auto& exten : this->rtpParameters.headerExtensions)
        {
            if (exten.id == 0u)
            {
                MS_THROW_TYPE_ERROR("RTP extension id cannot be 0");
            }

            if (this->rtpHeaderExtensionIds.mid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::MID)
            {
                this->rtpHeaderExtensionIds.mid = exten.id;
            }

            if (this->rtpHeaderExtensionIds.rid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::RTP_STREAM_ID)
            {
                this->rtpHeaderExtensionIds.rid = exten.id;
            }

            if (this->rtpHeaderExtensionIds.rrid == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::REPAIRED_RTP_STREAM_ID)
            {
                this->rtpHeaderExtensionIds.rrid = exten.id;
            }

            if (this->rtpHeaderExtensionIds.absSendTime == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME)
            {
                this->rtpHeaderExtensionIds.absSendTime = exten.id;
            }

            if (this->rtpHeaderExtensionIds.transportWideCc01 == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01)
            {
                this->rtpHeaderExtensionIds.transportWideCc01 = exten.id;
            }

            // NOTE: Remove this once framemarking draft becomes RFC.
            if (this->rtpHeaderExtensionIds.frameMarking07 == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07)
            {
                this->rtpHeaderExtensionIds.frameMarking07 = exten.id;
            }

            if (this->rtpHeaderExtensionIds.frameMarking == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING)
            {
                this->rtpHeaderExtensionIds.frameMarking = exten.id;
            }

            if (this->rtpHeaderExtensionIds.ssrcAudioLevel == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL)
            {
                this->rtpHeaderExtensionIds.ssrcAudioLevel = exten.id;
            }

            if (this->rtpHeaderExtensionIds.videoOrientation == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION)
            {
                this->rtpHeaderExtensionIds.videoOrientation = exten.id;
            }

            if (this->rtpHeaderExtensionIds.toffset == 0u && exten.type == RTC::RtpHeaderExtensionUri::Type::TOFFSET)
            {
                this->rtpHeaderExtensionIds.toffset = exten.id;
            }
        }

        // Set the RTCP report generation interval.
        if (this->kind == RTC::Media::Kind::AUDIO)
            this->maxRtcpInterval = RTC::RTCP::MaxAudioIntervalMs;
        else
            this->maxRtcpInterval = RTC::RTCP::MaxVideoIntervalMs;

        // Create a KeyFrameRequestManager.
        if (this->kind == RTC::Media::Kind::VIDEO)
        {
            auto jsonKeyFrameRequestDelayIt = data.find("keyFrameRequestDelay");
            uint32_t keyFrameRequestDelay   = 0u;

            // clang-format off
            if (
                jsonKeyFrameRequestDelayIt != data.end() &&
                jsonKeyFrameRequestDelayIt->is_number_integer()
            )
            // clang-format on
            {
                keyFrameRequestDelay = jsonKeyFrameRequestDelayIt->get<uint32_t>();
            }

            this->keyFrameRequestManager = new RTC::KeyFrameRequestManager(this, keyFrameRequestDelay);
        }
    }

    Producer::~Producer()
    {
        MS_TRACE();

        // notify master 
        if (m_master)
        {
            m_master->onClosedSlave(this);
            m_master = nullptr;
        }

        // Delete all streams.
        for (auto& kv : this->mapSsrcRtpStream)
        {
            auto* rtpStream = kv.second;

            delete rtpStream;
        }

        this->mapSsrcRtpStream.clear();
        this->rtpStreamByEncodingIdx.clear();
        this->rtpStreamScores.clear();
        this->mapRtxSsrcRtpStream.clear();
        this->mapRtpStreamMappedSsrc.clear();
        this->mapMappedSsrcSsrc.clear();

        // Delete the KeyFrameRequestManager.
        delete this->keyFrameRequestManager;
    }

    void Producer::FillJson(json& jsonObject) const
    {
        MS_TRACE();

        // Add id.
        jsonObject["id"] = this->id;

        // Add kind.
        jsonObject["kind"] = RTC::Media::GetString(this->kind);

        // Add rtpParameters.
        this->rtpParameters.FillJson(jsonObject["rtpParameters"]);

        // Add type.
        jsonObject["type"] = RTC::RtpParameters::GetTypeString(this->type);

        // Add rtpMapping.
        jsonObject["rtpMapping"] = json::object();
        auto jsonRtpMappingIt    = jsonObject.find("rtpMapping");

        // Add rtpMapping.codecs.
        {
            (*jsonRtpMappingIt)["codecs"] = json::array();
            auto jsonCodecsIt             = jsonRtpMappingIt->find("codecs");
            size_t idx{ 0 };

            for (auto& kv : this->rtpMapping.codecs)
            {
                jsonCodecsIt->emplace_back(json::value_t::object);

                auto& jsonEntry        = (*jsonCodecsIt)[idx];
                auto payloadType       = kv.first;
                auto mappedPayloadType = kv.second;

                jsonEntry["payloadType"]       = payloadType;
                jsonEntry["mappedPayloadType"] = mappedPayloadType;

                ++idx;
            }
        }

        // Add rtpMapping.encodings.
        {
            (*jsonRtpMappingIt)["encodings"] = json::array();
            auto jsonEncodingsIt             = jsonRtpMappingIt->find("encodings");

            for (size_t i{ 0 }; i < this->rtpMapping.encodings.size(); ++i)
            {
                jsonEncodingsIt->emplace_back(json::value_t::object);

                auto& jsonEntry             = (*jsonEncodingsIt)[i];
                const auto& encodingMapping = this->rtpMapping.encodings[i];

                if (!encodingMapping.rid.empty())
                    jsonEntry["rid"] = encodingMapping.rid;
                else
                    jsonEntry["rid"] = nullptr;

                if (encodingMapping.ssrc != 0u)
                    jsonEntry["ssrc"] = encodingMapping.ssrc;
                else
                    jsonEntry["ssrc"] = nullptr;

                jsonEntry["mappedSsrc"] = encodingMapping.mappedSsrc;
            }
        }

        // Add rtpStreams.
        jsonObject["rtpStreams"] = json::array();
        auto jsonRtpStreamsIt    = jsonObject.find("rtpStreams");

        for (auto* rtpStream : this->rtpStreamByEncodingIdx)
        {
            if (!rtpStream)
                continue;

            jsonRtpStreamsIt->emplace_back(json::value_t::object);

            auto& jsonEntry = (*jsonRtpStreamsIt)[jsonRtpStreamsIt->size() - 1];

            rtpStream->FillJson(jsonEntry);
        }

        // Add paused.
        jsonObject["paused"] = this->paused;

        // Add traceEventTypes.
        std::vector<std::string> traceEventTypes;
        std::ostringstream traceEventTypesStream;

        if (this->traceEventTypes.rtp)
            traceEventTypes.emplace_back("rtp");
        if (this->traceEventTypes.keyframe)
            traceEventTypes.emplace_back("keyframe");
        if (this->traceEventTypes.nack)
            traceEventTypes.emplace_back("nack");
        if (this->traceEventTypes.pli)
            traceEventTypes.emplace_back("pli");
        if (this->traceEventTypes.fir)
            traceEventTypes.emplace_back("fir");

        if (!traceEventTypes.empty())
        {
            std::copy(
              traceEventTypes.begin(),
              traceEventTypes.end() - 1,
              std::ostream_iterator<std::string>(traceEventTypesStream, ","));
            traceEventTypesStream << traceEventTypes.back();
        }

        jsonObject["traceEventTypes"] = traceEventTypesStream.str();
    }

    void Producer::FillJsonStats(json& jsonArray) const
    {
        MS_TRACE();

        for (auto* rtpStream : this->rtpStreamByEncodingIdx)
        {
            if (!rtpStream)
                continue;

            jsonArray.emplace_back(json::value_t::object);

            auto& jsonEntry = jsonArray[jsonArray.size() - 1];

            rtpStream->FillJsonStats(jsonEntry);
        }
    }

    void Producer::HandleRequest(Channel::Request* request)
    {
        MS_TRACE();

        switch (request->methodId)
        {
            case Channel::Request::MethodId::PRODUCER_DUMP:
            {
                json data = json::object();

                FillJson(data);

                request->Accept(data);

                break;
            }

            case Channel::Request::MethodId::PRODUCER_GET_STATS:
            {
                json data = json::array();

                FillJsonStats(data);

                request->Accept(data);

                break;
            }

            case Channel::Request::MethodId::PRODUCER_PAUSE:
            {
                if (this->paused)
                {
                    request->Accept();

                    return;
                }

                // Pause all streams.
                for (auto& kv : this->mapSsrcRtpStream)
                {
                    auto* rtpStream = kv.second;

                    rtpStream->Pause();
                }

                this->paused = true;

                MS_DEBUG_DEV("Producer paused [producerId:%s]", this->id.c_str());

                this->listener->OnProducerPaused(this);

                request->Accept();

                break;
            }

            case Channel::Request::MethodId::PRODUCER_RESUME:
            {
                if (!this->paused)
                {
                    request->Accept();

                    return;
                }

                // Resume all streams.
                for (auto& kv : this->mapSsrcRtpStream)
                {
                    auto* rtpStream = kv.second;

                    rtpStream->Resume();
                }

                this->paused = false;

                MS_DEBUG_DEV("Producer resumed [producerId:%s]", this->id.c_str());

                this->listener->OnProducerResumed(this);

                if (this->keyFrameRequestManager)
                {
                    MS_DEBUG_2TAGS(rtcp, rtx, "requesting forced key frame(s) after resumed");

                    // Request a key frame for all streams.
                    for (auto& kv : this->mapSsrcRtpStream)
                    {
                        auto ssrc = kv.first;

                        this->keyFrameRequestManager->ForceKeyFrameNeeded(ssrc);
                    }
                }

                request->Accept();

                break;
            }

            case Channel::Request::MethodId::PRODUCER_ENABLE_TRACE_EVENT:
            {
                auto jsonTypesIt = request->data.find("types");

                // Disable all if no entries.
                if (jsonTypesIt == request->data.end() || !jsonTypesIt->is_array())
                    MS_THROW_TYPE_ERROR("wrong types (not an array)");

                // Reset traceEventTypes.
                struct TraceEventTypes newTraceEventTypes;

                for (const auto& type : *jsonTypesIt)
                {
                    if (!type.is_string())
                        MS_THROW_TYPE_ERROR("wrong type (not a string)");

                    std::string typeStr = type.get<std::string>();

                    if (typeStr == "rtp")
                        newTraceEventTypes.rtp = true;
                    else if (typeStr == "keyframe")
                        newTraceEventTypes.keyframe = true;
                    else if (typeStr == "nack")
                        newTraceEventTypes.nack = true;
                    else if (typeStr == "pli")
                        newTraceEventTypes.pli = true;
                    else if (typeStr == "fir")
                        newTraceEventTypes.fir = true;
                }

                this->traceEventTypes = newTraceEventTypes;

                request->Accept();

                break;
            }

            case Channel::Request::MethodId::PRODUCER_SET_TRANSLATE_MODE:
            {
                MS_WARN_TAG(dead, "PRODUCER_SET_TRANSLATE_MODE");

                auto jit = request->data.find("translateMode");
                if (jit == request->data.end() || !jit->is_string())
                {
                    MS_THROW_TYPE_ERROR("wrong type (not an string)");
                }

                std::string mode = jit->get<std::string>();

                MS_WARN_TAG(dead, "PRODUCE MODE %s", mode.c_str());

                if (mode == "direct")
                {
                    m_timer.Stop();
                    translateMode = direct;
                }
                else if (mode == "unpackAndProduce")
                {
                    if (!m_timer.IsActive())
                    {
                        m_timer.Start(m_timerDelay, m_timerDelay);
                    }
                    translateMode = unpackAndProduce;
                }
                else if (mode == "decodeAndEncode")
                {
                    if (!m_timer.IsActive())
                    {
                        m_timer.Start(m_timerDelay, m_timerDelay);
                    }
                    translateMode = decodeAndEncode;
                }
                else
                {
                    MS_THROW_ERROR("unknown mode");
                }

                break;
            }

            case Channel::Request::MethodId::PRODUCER_ATTACH_SLAVE:
            {
                MS_WARN_TAG(dead, "PRODUCER_ATTACH_SLAVE");

                auto jit = request->data.find("slaveId");
                if (jit == request->data.end() || !jit->is_string())
                {
                    MS_THROW_TYPE_ERROR("wrong type (not an string)");
                }

                std::string slaveId = jit->get<std::string>();

                MS_WARN_TAG(dead, "attach slave producer id %s", slaveId.c_str());

                break;
            }

            default:
            {
                MS_THROW_ERROR("unknown method '%s'", request->method.c_str());
            }
        }
    }

    void Producer::OnTimer(Timer * timer)
    {
        MS_TRACE();

        if (paused)
        {
            return;
        }

        for (auto & it : mapSsrcRtpStream)
        {
            RTC::RtpStreamRecv * rtpStream = it.second;

            if (!rtpStream)
            {
                MS_WARN_TAG(dead, "no stream");
                continue;
            }

            std::vector<AVFramePtr> slaveFrames;
            for (Producer * p : m_slaves)
            {
                if (p)
                {
                    // TODO put stream parameters into getLastFrame ??
                    slaveFrames.emplace_back(p->getLastFrame());
                }
            }

            // TODO join slave frames with primary

            RTC::DecodeContext & dc = rtpStream->GetDecodeContext(rtpStream->GetSsrc());
            if (!dc.isOpened || dc.frames.size() == 0)
            {
                MS_WARN_TAG(dead, "context not opened or no frames");
                continue;
            }

            std::lock_guard<std::mutex> lock(dc.lock);

            RTC::EncodeContext & ec = rtpStream->GetEncodeContext(rtpStream->GetSsrc());

            if (dc.frameWidth != ec.frameWidth || dc.frameHeight != ec.frameHeight)
            {
                MS_WARN_TAG(dead, "init context ssrc %" PRIu16 " %" PRIu32 "x%" PRIu32, rtpStream->GetSsrc(), dc.frameWidth == 0 ? 320 : dc.frameWidth, dc.frameHeight == 0 ? 180 : dc.frameHeight);
                ec.initContext(dc.frameWidth == 0 ? 320 : dc.frameWidth, dc.frameHeight == 0 ? 180 : dc.frameHeight);
            }

            // MS_WARN_TAG(dead, "PRUDUCE encode %" PRIu64 " last frames", dc.frames.size());

            std::vector<AVPacketPtr> packets;
            if (!RTC::Codecs::Tools::EncodePacket(ec, rtpStream->GetMimeType(), dc.frames, packets))
            {
                // error or no frames
                // MS_WARN_TAG(dead, "encode error");
            }

            for (AVPacketPtr & pkt : packets)
            {
                // produce
                RTC::ProduceContext & c = rtpStream->GetProduceContext(rtpStream->GetSsrc());
                c.payloadType = rtpStream->GetPayloadType();
                // c.sequence    = packet->GetSequenceNumber();

                std::vector<RTC::RtpPacketPtr> produced;
                if (RTC::Codecs::Tools::ProduceRtpPacket(c, pkt->data, pkt->size, DepLibUV::GetTimeMs(), rtpStream->GetMimeType(), produced))
                {
                    // MS_WARN_TAG(dead, "produced %" PRIu64 " packets", produced.size());

                    for (RTC::RtpPacketPtr & p : produced)
                    {
                        MS_ASSERT(c.produced.count(p->GetSequenceNumber()) == 0, "duplicate sequence");

                        c.produced[p->GetSequenceNumber()] = p;

                        MS_WARN_TAG(dead, "PRUDUCED ssrs %" PRIu32 " produced packet timestamp %" PRIu32 " type %" PRIu32 " seq %" PRIu16, 
                                            rtpStream->GetSsrc(), 
                                            p->GetTimestamp(), p->GetPayloadType(), p->GetSequenceNumber());

                        // // unpack and process packet
                        // RTC::UnpackContext & c2 = rtpStream->GetUnpackContext2(rtpStream->GetSsrc());

                        // std::vector<std::pair<const uint8_t *, size_t> > nalptrs2;
                        // if (!RTC::Codecs::Tools::UnpackRtpPacket(c2, p.get(), rtpStream->GetMimeType(), nalptrs2))
                        // {
                        //     MS_WARN_TAG(dead, "unpack failed");
                        // }
                        // else
                        // {
                        //     static size_t summary = 0;
                        //     static const uint8_t start_code[4] = { 0, 0, 0, 1 };

                        //     // TODO debug code, write to file
                        //     FILE * f = fopen(c2.fileName.c_str(), "a+b");

                        //     for (const std::pair<const uint8_t *, size_t> & nal : nalptrs2)
                        //     {
                        //         fwrite(start_code, 1, 4, f);    
                        //         fwrite(nal.first, 1, nal.second, f);    

                        //         MS_WARN_TAG(dead, "write packet size %" PRIu64 " summary %" PRIu64, nal.second + 4, summary += (nal.second + 4));
                        //     }

                        //     fclose(f);      
                        // }

                        // if (random32(0) % 32 == 0)
                        // {
                        //     MS_WARN_TAG(dead, "DROPPED");
                        //     return ;    
                        // }

                        // Process the packet.
                        DispatchRtpPacket(p.get()); 
                    }
                }

            } // if nalptrs

        }
    }

    // void dumpFrame(RTC::EncodeContext & context, AVFramePtr & frame)
    // {
    //     MS_TRACE();

    //     // set filename
    //     static uint32_t counter = 0;
    //     std::string filename = "/tmp/frame-" + std::to_string(counter++) + "-" + std::to_string(time(nullptr)) + ".jpg";

    //     // create file
    //     FILE * f = fopen( filename.c_str(), "wb");

    //     AVPacketPtr packet(new AVPacket());
    //     av_init_packet(packet.get());

    //     packet->data = nullptr;
    //     packet->size = 0;

    //     int gotFrame;
    //     if (avcodec_encode_video2(context.jpegContext.get(), packet.get(), frame.get(), &gotFrame) < 0) 
    //     {
    //         MS_WARN_TAG(dead, "frame encode error");
    //         return;
    //     }

    //     fwrite(packet->data, 1, packet->size, f);
    //     fclose(f);
    // }

    ReceiveRtpPacketResult Producer::ReceiveRtpPacket(RTC::RtpPacket* packet)
    {
        MS_TRACE();

        if (translateMode == direct)
        {
            // in direct mode - dispatcj packets without processingd j,otv dct ghj,ktvs
            return DispatchRtpPacket(packet);
        }

        auto* rtpStream = GetRtpStream(packet);
        if (!rtpStream)
        {
            // MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());

            return ReceiveRtpPacketResult::DISCARDED;
        }

        if (packet->GetSsrc() == rtpStream->GetRtxSsrc())
        {
            return ReceiveRtpRtx(packet);
        }
        else if (packet->GetSsrc() == rtpStream->GetSsrc())
        {
            // if (random32(0) % 32 == 0)
            // {
            //     MS_WARN_TAG(dead, "DROPPED %" PRIu16, packet->GetSequenceNumber());
            //     return ReceiveRtpPacketResult::MEDIA;    
            // }

            return ReceiveRtpMedia(packet);
        }
        else
        {
            // MS_ABORT("found stream does not match received packet");
        }

        return ReceiveRtpPacketResult::DISCARDED;
    }

    ReceiveRtpPacketResult Producer::ReceiveRtpMedia(RTC::RtpPacket* packet)
    {
        ReceiveRtpPacketResult result = ReceiveRtpPacketResult::MEDIA;
        
        auto* rtpStream = GetRtpStream(packet);
        if (!rtpStream)
        {
            // MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());

            return ReceiveRtpPacketResult::DISCARDED;
        }

        // MS_WARN_TAG(rtp, "received MEDIA packet stream name %s", rtpStream->GetCname().c_str());

        MS_WARN_TAG(dead, "ORIG ssrc %" PRIu32 " rtx %" PRIu32 " unpack packet size %" PRIu64 " timestamp %" PRIu32 " type %" PRIu32 " seq %" PRIu16, 
                            rtpStream->GetSsrc(), rtpStream->GetRtxSsrc(),
                            packet->GetSize(), packet->GetTimestamp(), 
                            packet->GetPayloadType(), packet->GetSequenceNumber());

        std::vector<std::pair<const uint8_t *, size_t> > nalptrs;
        {

            // unpack and process packet
            RTC::UnpackContext & c = rtpStream->GetUnpackContext(rtpStream->GetSsrc());

            if (!RTC::Codecs::Tools::UnpackRtpPacket(c, packet, rtpStream->GetMimeType(), nalptrs))
            {
                // unpack error
                if (c.flags & RTP_PAYLOAD_FLAG_PACKET_LOST)
                {
                    // force request keyframe ?
                    if (this->keyFrameRequestManager)
                    {
                        this->keyFrameRequestManager->ForceKeyFrameNeeded(packet->GetSsrc());
                    }
                    return result;
                }
            }

            // // static size_t summary = 0;
            // static const uint8_t start_code[4] = { 0, 0, 0, 1 };

            // // TODO debug code, write to file
            // FILE * f = fopen(c.fileName.c_str(), "a+b");

            // for (const std::pair<const uint8_t *, size_t> & nal : nalptrs)
            // {
            //     fwrite(start_code, 1, 4, f);    
            //     fwrite(nal.first, 1, nal.second, f);    

            //     // MS_WARN_TAG(dead, "write packet size %" PRIu64 " summary %" PRIu64, nal.second + 4, summary);
            // }

            // fclose(f);      

            if (c.lastSeq != 0 && c.lastSeq + 1 != packet->GetSequenceNumber())
            {
                // TODO request packets
                MS_WARN_TAG(dead, "missed %" PRIu16 " packets from %" PRIu16, packet->GetSequenceNumber() - c.lastSeq - 1, c.lastSeq);
            }

            c.lastSeq = packet->GetSequenceNumber();
        }

        // decode and encode packets
        std::vector<AVPacketPtr> packets;
        if (nalptrs.size() > 0 && translateMode == decodeAndEncode)
        {
            RTC::DecodeContext & c = rtpStream->GetDecodeContext(rtpStream->GetSsrc());

            std::lock_guard<std::mutex> lock(c.lock);

            for (const std::pair<const uint8_t *, size_t> & nal : nalptrs)
            {
                std::vector<uint8_t> v(nal.second + 4);
                v[0] = v[1] = v[2] = 0;
                v[3] = 1;
                memcpy(&v[4], nal.first, nal.second);

                if (!RTC::Codecs::Tools::DecodePacket(c, rtpStream->GetMimeType(), &v[0], nal.second + 4, c.frames))
                {
                    // no frames
                    if (c.frameWidth == 0 && c.frameHeight == 0)
                    {
                        MS_WARN_TAG(dead, "NO KEYFRAME");
                        // TODO request keyframe ?
                    }
                }
                else
                {
                    // MS_WARN_TAG(dead, "decoded %" PRIu64 " frames", frames.size());
                }

                // if (frames.size() > 0)
                // {                
                //     RTC::EncodeContext & ec = rtpStream->GetEncodeContext(rtpStream->GetSsrc());

                //     // dump to jpeg
                //     // for (AVFramePtr & frame : frames)
                //     // {
                //     //     dumpFrame(ec, frame);
                //     // }

                //     // encode

                if (c.updateDefaultFrame() == 0)
                {
                    c.frames.clear();
                    c.frames.emplace_back(c.defaultFrame);
                }
            }

        } // if (nalptrs.size() > 0)

        return result;
    }

    ReceiveRtpPacketResult Producer::ReceiveRtpRtx(RTC::RtpPacket* packet)
    {
        auto* rtpStream = GetRtpStream(packet);
        if (!rtpStream)
        {
            // MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());

            return ReceiveRtpPacketResult::DISCARDED;
        }

        // rtx packet
        MS_WARN_TAG(dead, "RTX ssrc %" PRIu32 " rtx %" PRIu32 " unpack packet timestamp %" PRIu32 " type %" PRIu32 " seq %" PRIu16, 
                            rtpStream->GetSsrc(), rtpStream->GetRtxSsrc(),
                            packet->GetTimestamp(), packet->GetPayloadType(), packet->GetSequenceNumber());

        // return ReceiveRtpPacketInternal(packet);
        return ReceiveRtpPacketResult::RETRANSMISSION;
    }

    ReceiveRtpPacketResult Producer::DispatchRtpPacket(RTC::RtpPacket* packet)
    {
        MS_TRACE();

        // Reset current packet.
        this->currentRtpPacket = nullptr;

        // Count number of RTP streams.
        auto numRtpStreamsBefore = this->mapSsrcRtpStream.size();

        auto* rtpStream = GetRtpStream(packet);

        if (!rtpStream)
        {
            MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());

            return ReceiveRtpPacketResult::DISCARDED;
        }

        // Pre-process the packet.
        PreProcessRtpPacket(packet);

        ReceiveRtpPacketResult result;
        bool isRtx{ false };

        // Media packet.
        if (packet->GetSsrc() == rtpStream->GetSsrc())
        {
            result = ReceiveRtpPacketResult::MEDIA;

            if (!rtpStream->ReceivePacket(packet))
            {
                MS_WARN_TAG(dead, "not processed");
                // May have to announce a new RTP stream to the listener.
                if (this->mapSsrcRtpStream.size() > numRtpStreamsBefore)
                {
                    NotifyNewRtpStream(rtpStream);
                }

                return result;
            }
        }

        // RTX packet.
        else if (packet->GetSsrc() == rtpStream->GetRtxSsrc())
        {
            // MS_WARN_TAG(rtp, "received RTX packet stream name %s", rtpStream->GetCname().c_str());

            result = ReceiveRtpPacketResult::RETRANSMISSION;
            isRtx  = true;

            // Process the packet.
            if (!rtpStream->ReceiveRtxPacket(packet))
                return result;
        }

        // Should not happen.
        else
        {
            MS_ABORT("found stream does not match received packet");
        }

        if (packet->IsKeyFrame())
        {
            MS_DEBUG_TAG(
              rtp,
              "key frame received [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
              packet->GetSsrc(),
              packet->GetSequenceNumber());

            // Tell the keyFrameRequestManager.
            if (this->keyFrameRequestManager)
                this->keyFrameRequestManager->KeyFrameReceived(packet->GetSsrc());
        }

        // May have to announce a new RTP stream to the listener.
        if (this->mapSsrcRtpStream.size() > numRtpStreamsBefore)
        {
            // Request a key frame for this stream since we may have lost the first packets
            // (do not do it if this is a key frame).
            if (this->keyFrameRequestManager && !this->paused && !packet->IsKeyFrame())
                this->keyFrameRequestManager->ForceKeyFrameNeeded(packet->GetSsrc());

            // Update current packet.
            this->currentRtpPacket = packet;

            NotifyNewRtpStream(rtpStream);

            // Reset current packet.
            this->currentRtpPacket = nullptr;
        }

        // If paused stop here.
        if (this->paused)
            return result;

        // May emit 'trace' event.
        EmitTraceEventRtpAndKeyFrameTypes(packet, isRtx);

        // Mangle the packet before providing the listener with it.
        if (!MangleRtpPacket(packet, rtpStream))
            return ReceiveRtpPacketResult::DISCARDED;

        // Post-process the packet.
        PostProcessRtpPacket(packet);

        this->listener->OnProducerRtpPacketReceived(this, packet);

        return result;
    }

    void Producer::setMaster(Producer * master)
    {
        MS_ASSERT(m_master == nullptr, "master already set");
        MS_ASSERT(master == nullptr, "master must be not null");
        m_master = master;
    }

    void Producer::onClosedSlave(Producer * slave)
    {
        // TODO remove from slaves
    }

    AVFramePtr Producer::getLastFrame() const
    {
        return AVFramePtr();
    }

    RTC::RtpStreamRecv* Producer::GetRtpStream(RTC::RtpPacket* packet)
    {
        MS_TRACE();

        uint32_t ssrc       = packet->GetSsrc();
        uint8_t payloadType = packet->GetPayloadType();

        // If stream found in media ssrcs map, return it.
        {
            auto it = this->mapSsrcRtpStream.find(ssrc);

            if (it != this->mapSsrcRtpStream.end())
            {
                auto* rtpStream = it->second;

                return rtpStream;
            }
        }

        // If stream found in RTX ssrcs map, return it.
        {
            auto it = this->mapRtxSsrcRtpStream.find(ssrc);

            if (it != this->mapRtxSsrcRtpStream.end())
            {
                auto* rtpStream = it->second;

                return rtpStream;
            }
        }

        // Otherwise check our encodings and, if appropriate, create a new stream.

        // First, look for an encoding with matching media or RTX ssrc value.
        for (size_t i{ 0 }; i < this->rtpParameters.encodings.size(); ++i)
        {
            auto& encoding         = this->rtpParameters.encodings[i];
            const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
            const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
            bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
            bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

            if (isMediaPacket && encoding.ssrc == ssrc)
            {
                auto* rtpStream = CreateRtpStream(packet, *mediaCodec, i);

                return rtpStream;
            }
            else if (isRtxPacket && encoding.hasRtx && encoding.rtx.ssrc == ssrc)
            {
                auto it = this->mapSsrcRtpStream.find(encoding.ssrc);

                // Ignore if no stream has been created yet for the corresponding encoding.
                if (it == this->mapSsrcRtpStream.end())
                {
                    MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet for not yet created RtpStream (ssrc lookup)");

                    return nullptr;
                }

                auto* rtpStream = it->second;

                // Ensure no RTX ssrc was previously detected.
                if (rtpStream->HasRtx())
                {
                    MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new ssrc (ssrc lookup)");

                    return nullptr;
                }

                // Update the stream RTX data.
                rtpStream->SetRtx(payloadType, ssrc);

                // Insert the new RTX ssrc into the map.
                this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

                return rtpStream;
            }
        }

        // If not found, look for an encoding matching the packet RID value.
        std::string rid;

        if (packet->ReadRid(rid))
        {
            for (size_t i{ 0 }; i < this->rtpParameters.encodings.size(); ++i)
            {
                auto& encoding = this->rtpParameters.encodings[i];

                if (encoding.rid != rid)
                    continue;

                const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
                const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
                bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
                bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

                if (isMediaPacket)
                {
                    // Ensure no other stream already exists with same RID.
                    for (auto& kv : this->mapSsrcRtpStream)
                    {
                        auto* rtpStream = kv.second;

                        if (rtpStream->GetRid() == rid)
                        {
                            MS_WARN_TAG(
                              rtp, "ignoring packet with unknown ssrc but already handled RID (RID lookup)");

                            return nullptr;
                        }
                    }

                    auto* rtpStream = CreateRtpStream(packet, *mediaCodec, i);

                    return rtpStream;
                }
                else if (isRtxPacket)
                {
                    // Ensure a stream already exists with same RID.
                    for (auto& kv : this->mapSsrcRtpStream)
                    {
                        auto* rtpStream = kv.second;

                        if (rtpStream->GetRid() == rid)
                        {
                            // Ensure no RTX ssrc was previously detected.
                            if (rtpStream->HasRtx())
                            {
                                MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new SSRC (RID lookup)");

                                return nullptr;
                            }

                            // Update the stream RTX data.
                            rtpStream->SetRtx(payloadType, ssrc);

                            // Insert the new RTX ssrc into the map.
                            this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

                            return rtpStream;
                        }
                    }

                    MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet for not yet created RtpStream (RID lookup)");

                    return nullptr;
                }
            }

            MS_WARN_TAG(rtp, "ignoring packet with unknown RID (RID lookup)");

            return nullptr;
        }

        // If not found, and there is a single encoding without ssrc and RID, this
        // may be the media or RTX stream.
        //
        // clang-format off
        if (
            this->rtpParameters.encodings.size() == 1 &&
            !this->rtpParameters.encodings[0].ssrc &&
            this->rtpParameters.encodings[0].rid.empty()
        )
        // clang-format on
        {
            auto& encoding         = this->rtpParameters.encodings[0];
            const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);
            const auto* rtxCodec   = this->rtpParameters.GetRtxCodecForEncoding(encoding);
            bool isMediaPacket     = (mediaCodec->payloadType == payloadType);
            bool isRtxPacket       = (rtxCodec && rtxCodec->payloadType == payloadType);

            if (isMediaPacket)
            {
                // Ensure there is no other RTP stream already.
                if (!this->mapSsrcRtpStream.empty())
                {
                    MS_WARN_TAG(
                      rtp,
                      "ignoring packet with unknown ssrc not matching the already existing stream (single RtpStream lookup)");

                    return nullptr;
                }

                auto* rtpStream = CreateRtpStream(packet, *mediaCodec, 0);

                return rtpStream;
            }
            else if (isRtxPacket)
            {
                // There must be already a media RTP stream.
                auto it = this->mapSsrcRtpStream.begin();

                if (it == this->mapSsrcRtpStream.end())
                {
                    MS_DEBUG_2TAGS(
                      rtp, rtx, "ignoring RTX packet for not yet created RtpStream (single stream lookup)");

                    return nullptr;
                }

                auto* rtpStream = it->second;

                // Ensure no RTX SSRC was previously detected.
                if (rtpStream->HasRtx())
                {
                    MS_DEBUG_2TAGS(rtp, rtx, "ignoring RTX packet with new SSRC (single stream lookup)");

                    return nullptr;
                }

                // Update the stream RTX data.
                rtpStream->SetRtx(payloadType, ssrc);

                // Insert the new RTX SSRC into the map.
                this->mapRtxSsrcRtpStream[ssrc] = rtpStream;

                return rtpStream;
            }
        }

        return nullptr;
    }

    RTC::RtpStreamRecv* Producer::CreateRtpStream(
      RTC::RtpPacket* packet, const RTC::RtpCodecParameters& mediaCodec, size_t encodingIdx)
    {
        MS_TRACE();

        uint32_t ssrc = packet->GetSsrc();

        MS_ASSERT(
          this->mapSsrcRtpStream.find(ssrc) == this->mapSsrcRtpStream.end(),
          "RtpStream with given SSRC already exists");
        MS_ASSERT(
          !this->rtpStreamByEncodingIdx[encodingIdx],
          "RtpStream for given encoding index already exists");

        auto& encoding        = this->rtpParameters.encodings[encodingIdx];
        auto& encodingMapping = this->rtpMapping.encodings[encodingIdx];

        MS_DEBUG_TAG(
          rtp,
          "[encodingIdx:%zu, ssrc:%" PRIu32 ", rid:%s, payloadType:%" PRIu8 "]",
          encodingIdx,
          ssrc,
          encoding.rid.c_str(),
          mediaCodec.payloadType);

        // Set stream params.
        RTC::RtpStream::Params params;

        params.encodingIdx    = encodingIdx;
        params.ssrc           = ssrc;
        params.payloadType    = mediaCodec.payloadType;
        params.mimeType       = mediaCodec.mimeType;
        params.clockRate      = mediaCodec.clockRate;
        params.rid            = encoding.rid;
        params.cname          = this->rtpParameters.rtcp.cname;
        params.spatialLayers  = encoding.spatialLayers;
        params.temporalLayers = encoding.temporalLayers;

        // Check in band FEC in codec parameters.
        if (mediaCodec.parameters.HasInteger("useinbandfec") && mediaCodec.parameters.GetInteger("useinbandfec") == 1)
        {
            MS_DEBUG_TAG(rtcp, "in band FEC enabled");

            params.useInBandFec = true;
        }

        // Check DTX in codec parameters.
        if (mediaCodec.parameters.HasInteger("usedtx") && mediaCodec.parameters.GetInteger("usedtx") == 1)
        {
            MS_DEBUG_TAG(rtp, "DTX enabled");

            params.useDtx = true;
        }

        // Check DTX in the encoding.
        if (encoding.dtx)
        {
            MS_DEBUG_TAG(rtp, "DTX enabled");

            params.useDtx = true;
        }

        for (const auto& fb : mediaCodec.rtcpFeedback)
        {
            if (!params.useNack && fb.type == "nack" && fb.parameter.empty())
            {
                MS_DEBUG_2TAGS(rtp, rtcp, "NACK supported");

                params.useNack = true;
            }
            else if (!params.usePli && fb.type == "nack" && fb.parameter == "pli")
            {
                MS_DEBUG_2TAGS(rtp, rtcp, "PLI supported");

                params.usePli = true;
            }
            else if (!params.useFir && fb.type == "ccm" && fb.parameter == "fir")
            {
                MS_DEBUG_2TAGS(rtp, rtcp, "FIR supported");

                params.useFir = true;
            }
        }

        // Create a RtpStreamRecv for receiving a media stream.
        auto* rtpStream = new RTC::RtpStreamRecv(this, params);

        // Insert into the maps.
        this->mapSsrcRtpStream[ssrc]              = rtpStream;
        this->rtpStreamByEncodingIdx[encodingIdx] = rtpStream;
        this->rtpStreamScores[encodingIdx]        = rtpStream->GetScore();

        // Set the mapped SSRC.
        this->mapRtpStreamMappedSsrc[rtpStream]             = encodingMapping.mappedSsrc;
        this->mapMappedSsrcSsrc[encodingMapping.mappedSsrc] = ssrc;

        // If the Producer is paused tell it to the new RtpStreamRecv.
        if (this->paused)
            rtpStream->Pause();

        // Emit the first score event right now.
        EmitScore();

        rtpStream->GetUnpackContext(ssrc);
        rtpStream->GetUnpackContext2(ssrc);
        rtpStream->GetProduceContext(ssrc);
        rtpStream->GetDecodeContext(ssrc);
        // rtpStream->GetEncodeContext(ssrc);

        return rtpStream;
    }

    void Producer::NotifyNewRtpStream(RTC::RtpStreamRecv* rtpStream)
    {
        MS_TRACE();

        auto mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

        // Notify the listener.
        this->listener->OnProducerNewRtpStream(this, static_cast<RTC::RtpStream*>(rtpStream), mappedSsrc);
    }

    inline void Producer::PreProcessRtpPacket(RTC::RtpPacket* packet)
    {
        MS_TRACE();

        if (this->kind == RTC::Media::Kind::VIDEO)
        {
            // NOTE: Remove this once framemarking draft becomes RFC.
            packet->SetFrameMarking07ExtensionId(this->rtpHeaderExtensionIds.frameMarking07);
            packet->SetFrameMarkingExtensionId(this->rtpHeaderExtensionIds.frameMarking);
        }
    }

    inline bool Producer::MangleRtpPacket(RTC::RtpPacket* packet, RTC::RtpStreamRecv* rtpStream) const
    {
        MS_TRACE();

        // Mangle the payload type.
        {
            uint8_t payloadType = packet->GetPayloadType();
            auto it             = this->rtpMapping.codecs.find(payloadType);

            if (it == this->rtpMapping.codecs.end())
            {
                MS_WARN_TAG(rtp, "unknown payload type [payloadType:%" PRIu8 "]", payloadType);

                return false;
            }

            uint8_t mappedPayloadType = it->second;

            packet->SetPayloadType(mappedPayloadType);
        }

        // Mangle the SSRC.
        {
            uint32_t mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

            packet->SetSsrc(mappedSsrc);
        }

        // Mangle RTP header extensions.
        {
            static uint8_t buffer[4096];
            static std::vector<RTC::RtpPacket::GenericExtension> extensions;

            // This happens just once.
            if (extensions.capacity() != 24)
                extensions.reserve(24);

            extensions.clear();

            uint8_t* extenValue;
            uint8_t extenLen;
            uint8_t* bufferPtr{ buffer };

            // Add urn:ietf:params:rtp-hdrext:sdes:mid.
            {
                extenLen = RTC::MidMaxLength;

                extensions.emplace_back(
                  static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::MID), extenLen, bufferPtr);

                bufferPtr += extenLen;
            }

            if (this->kind == RTC::Media::Kind::AUDIO)
            {
                // Proxy urn:ietf:params:rtp-hdrext:ssrc-audio-level.
                extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.ssrcAudioLevel, extenLen);

                if (extenValue)
                {
                    std::memcpy(bufferPtr, extenValue, extenLen);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL),
                      extenLen,
                      bufferPtr);

                    // Not needed since this is the latest added extension.
                    // bufferPtr += extenLen;
                }
            }
            else if (this->kind == RTC::Media::Kind::VIDEO)
            {
                // Add http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time.
                {
                    extenLen = 3u;

                    // NOTE: Add value 0. The sending Transport will update it.
                    uint32_t absSendTime{ 0u };

                    Utils::Byte::Set3Bytes(bufferPtr, 0, absSendTime);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME), extenLen, bufferPtr);

                    bufferPtr += extenLen;
                }

                // Add http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01.
                {
                    extenLen = 2u;

                    // NOTE: Add value 0. The sending Transport will update it.
                    uint16_t wideSeqNumber{ 0u };

                    Utils::Byte::Set2Bytes(bufferPtr, 0, wideSeqNumber);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01),
                      extenLen,
                      bufferPtr);

                    bufferPtr += extenLen;
                }

                // NOTE: Remove this once framemarking draft becomes RFC.
                // Proxy http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07.
                extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.frameMarking07, extenLen);

                if (extenValue)
                {
                    std::memcpy(bufferPtr, extenValue, extenLen);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07),
                      extenLen,
                      bufferPtr);

                    bufferPtr += extenLen;
                }

                // Proxy urn:ietf:params:rtp-hdrext:framemarking.
                extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.frameMarking, extenLen);

                if (extenValue)
                {
                    std::memcpy(bufferPtr, extenValue, extenLen);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING), extenLen, bufferPtr);

                    bufferPtr += extenLen;
                }

                // Proxy urn:3gpp:video-orientation.
                extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.videoOrientation, extenLen);

                if (extenValue)
                {
                    std::memcpy(bufferPtr, extenValue, extenLen);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION),
                      extenLen,
                      bufferPtr);

                    bufferPtr += extenLen;
                }

                // Proxy urn:ietf:params:rtp-hdrext:toffset.
                extenValue = packet->GetExtension(this->rtpHeaderExtensionIds.toffset, extenLen);

                if (extenValue)
                {
                    std::memcpy(bufferPtr, extenValue, extenLen);

                    extensions.emplace_back(
                      static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TOFFSET), extenLen, bufferPtr);

                    // Not needed since this is the latest added extension.
                    // bufferPtr += extenLen;
                }
            }

            // Set the new extensions into the packet using One-Byte format.
            packet->SetExtensions(1, extensions);

            // Assign mediasoup RTP header extension ids (just those that mediasoup may
            // be interested in after passing it to the Router).
            packet->SetMidExtensionId(static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::MID));
            packet->SetAbsSendTimeExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME));
            packet->SetTransportWideCc01ExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01));
            // NOTE: Remove this once framemarking draft becomes RFC.
            packet->SetFrameMarking07ExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07));
            packet->SetFrameMarkingExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING));
            packet->SetSsrcAudioLevelExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL));
            packet->SetVideoOrientationExtensionId(
              static_cast<uint8_t>(RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION));
        }

        return true;
    }

    inline void Producer::PostProcessRtpPacket(RTC::RtpPacket* packet)
    {
        MS_TRACE();

        if (this->kind == RTC::Media::Kind::VIDEO)
        {
            bool camera;
            bool flip;
            uint16_t rotation;

            if (packet->ReadVideoOrientation(camera, flip, rotation))
            {
                // If video orientation was not yet detected or any value has changed,
                // emit event.
                // clang-format off
                if (
                    !this->videoOrientationDetected ||
                    camera != this->videoOrientation.camera ||
                    flip != this->videoOrientation.flip ||
                    rotation != this->videoOrientation.rotation
                )
                // clang-format on
                {
                    this->videoOrientationDetected  = true;
                    this->videoOrientation.camera   = camera;
                    this->videoOrientation.flip     = flip;
                    this->videoOrientation.rotation = rotation;

                    json data = json::object();

                    data["camera"]   = this->videoOrientation.camera;
                    data["flip"]     = this->videoOrientation.flip;
                    data["rotation"] = this->videoOrientation.rotation;

                    Channel::Notifier::Emit(this->id, "videoorientationchange", data);
                }
            }
        }
    }

    inline void Producer::EmitScore() const
    {
        MS_TRACE();

        json data = json::array();

        for (auto* rtpStream : this->rtpStreamByEncodingIdx)
        {
            if (!rtpStream)
                continue;

            data.emplace_back(json::value_t::object);

            auto& jsonEntry = data[data.size() - 1];

            jsonEntry["encodingIdx"] = rtpStream->GetEncodingIdx();
            jsonEntry["ssrc"]        = rtpStream->GetSsrc();

            if (!rtpStream->GetRid().empty())
                jsonEntry["rid"] = rtpStream->GetRid();

            jsonEntry["score"] = rtpStream->GetScore();
        }

        Channel::Notifier::Emit(this->id, "score", data);
    }

    inline void Producer::EmitTraceEventRtpAndKeyFrameTypes(RTC::RtpPacket* packet, bool isRtx) const
    {
        MS_TRACE();

        if (this->traceEventTypes.keyframe && packet->IsKeyFrame())
        {
            json data = json::object();

            data["type"]      = "keyframe";
            data["timestamp"] = DepLibUV::GetTimeMs();
            data["direction"] = "in";

            packet->FillJson(data["info"]);

            if (isRtx)
                data["info"]["isRtx"] = true;

            Channel::Notifier::Emit(this->id, "trace", data);
        }
        else if (this->traceEventTypes.rtp)
        {
            json data = json::object();

            data["type"]      = "rtp";
            data["timestamp"] = DepLibUV::GetTimeMs();
            data["direction"] = "in";

            packet->FillJson(data["info"]);

            if (isRtx)
                data["info"]["isRtx"] = true;

            Channel::Notifier::Emit(this->id, "trace", data);
        }
    }

    inline void Producer::EmitTraceEventPliType(uint32_t ssrc) const
    {
        MS_TRACE();

        if (!this->traceEventTypes.pli)
            return;

        json data = json::object();

        data["type"]         = "pli";
        data["timestamp"]    = DepLibUV::GetTimeMs();
        data["direction"]    = "out";
        data["info"]["ssrc"] = ssrc;

        Channel::Notifier::Emit(this->id, "trace", data);
    }

    inline void Producer::EmitTraceEventFirType(uint32_t ssrc) const
    {
        MS_TRACE();

        if (!this->traceEventTypes.fir)
            return;

        json data = json::object();

        data["type"]         = "fir";
        data["timestamp"]    = DepLibUV::GetTimeMs();
        data["direction"]    = "out";
        data["info"]["ssrc"] = ssrc;

        Channel::Notifier::Emit(this->id, "trace", data);
    }

    inline void Producer::EmitTraceEventNackType() const
    {
        MS_TRACE();

        if (!this->traceEventTypes.nack)
            return;

        json data = json::object();

        data["type"]      = "nack";
        data["timestamp"] = DepLibUV::GetTimeMs();
        data["direction"] = "out";
        data["info"]      = json::object();

        Channel::Notifier::Emit(this->id, "trace", data);
    }

    inline void Producer::OnRtpStreamScore(RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore)
    {
        MS_TRACE();

        // Update the vector of scores.
        this->rtpStreamScores[rtpStream->GetEncodingIdx()] = score;

        // Notify the listener.
        this->listener->OnProducerRtpStreamScore(this, rtpStream, score, previousScore);

        // Emit the score event.
        EmitScore();
    }

    inline void Producer::OnRtpStreamSendRtcpPacket(
      RTC::RtpStreamRecv* /*rtpStream*/, RTC::RTCP::Packet* packet)
    {
        switch (packet->GetType())
        {
            case RTC::RTCP::Type::PSFB:
            {
                auto* feedback = static_cast<RTC::RTCP::FeedbackPsPacket*>(packet);

                switch (feedback->GetMessageType())
                {
                    case RTC::RTCP::FeedbackPs::MessageType::PLI:
                    {
                        // May emit 'trace' event.
                        EmitTraceEventPliType(feedback->GetMediaSsrc());

                        break;
                    }

                    case RTC::RTCP::FeedbackPs::MessageType::FIR:
                    {
                        // May emit 'trace' event.
                        EmitTraceEventFirType(feedback->GetMediaSsrc());

                        break;
                    }

                    default:;
                }
            }

            case RTC::RTCP::Type::RTPFB:
            {
                auto* feedback = static_cast<RTC::RTCP::FeedbackRtpPacket*>(packet);

                switch (feedback->GetMessageType())
                {
                    case RTC::RTCP::FeedbackRtp::MessageType::NACK:
                    {
                        // May emit 'trace' event.
                        EmitTraceEventNackType();

                        break;
                    }

                    default:;
                }
            }

            default:;
        }

        // Notify the listener.
        this->listener->OnProducerSendRtcpPacket(this, packet);
    }

    inline void Producer::OnRtpStreamNeedWorstRemoteFractionLost(
      RTC::RtpStreamRecv* rtpStream, uint8_t& worstRemoteFractionLost)
    {
        auto mappedSsrc = this->mapRtpStreamMappedSsrc.at(rtpStream);

        // Notify the listener.
        this->listener->OnProducerNeedWorstRemoteFractionLost(this, mappedSsrc, worstRemoteFractionLost);
    }

    void Producer::OnRtpStreamResendPackets(RTC::RtpStreamRecv * rtpStream, 
                                            const std::vector<uint16_t> & seqNumbers)
    {
        MS_TRACE();

        for (auto it = seqNumbers.begin(); it != seqNumbers.end(); ++it)
        {
            MS_WARN_TAG(dead, "NACK REQ %" PRIu16, *it);

            RTC::ProduceContext & c = rtpStream->GetProduceContext(rtpStream->GetSsrc(), true);
            if (!c.produced.count(*it))
            {
                MS_WARN_TAG(dead, "PACKET NOT FOUND %" PRIu16 " for stream %" PRIu32, *it, rtpStream->GetSsrc());
                continue;
            }

            RTC::RtpPacketPtr p = c.produced[*it];

            MS_WARN_TAG(dead, "RTX encode packet seq %" PRIu16 " for %" PRIu16 " stream %" PRIu32 " for %" PRIu32, c.rtxsequence+1, *it, rtpStream->GetRtxSsrc(), rtpStream->GetSsrc());

            p->RtxEncode(rtpStream->GetRtxPayloadType(), rtpStream->GetRtxSsrc(), ++c.rtxsequence);

            ReceiveRtpPacket(p.get());
        }
    }

    inline void Producer::OnKeyFrameNeeded(
      RTC::KeyFrameRequestManager* /*keyFrameRequestManager*/, uint32_t ssrc)
    {
        MS_TRACE();

        auto it = this->mapSsrcRtpStream.find(ssrc);

        if (it == this->mapSsrcRtpStream.end())
        {
            MS_WARN_2TAGS(rtcp, rtx, "no associated RtpStream found [ssrc:%" PRIu32 "]", ssrc);

            return;
        }

        auto* rtpStream = it->second;

        rtpStream->RequestKeyFrame();
    }
} // namespace RTC
