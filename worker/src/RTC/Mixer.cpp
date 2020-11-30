//******************************************************************************
//******************************************************************************

#define MS_CLASS "RTC::Mixer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Mixer.hpp"
#include "Channel/Request.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "RTC/Producer.hpp"

auto g_data = R"(
   {
       "kind": "video",
       "paused": false,
       "rtpMapping": {
           "codecs": [
               {
                   "mappedPayloadType": 100,
                   "payloadType": 125
               },
               {
                   "mappedPayloadType": 101,
                   "payloadType": 107
               }
           ],
           "encodings": [
               {
                   "mappedSsrc": 447564902,
                   "rid": "r0",
                   "scalabilityMode": "S1T3"
               },
               {
                   "mappedSsrc": 447564903,
                   "rid": "r1",
                   "scalabilityMode": "S1T3"
               },
               {
                   "mappedSsrc": 447564904,
                   "rid": "r2",
                   "scalabilityMode": "S1T3"
               }
           ]
       },
       "rtpParameters": {
           "codecs": [
               {
                   "clockRate": 90000,
                   "mimeType": "video/H264",
                   "parameters": {
                       "level-asymmetry-allowed": 1,
                       "packetization-mode": 1,
                       "profile-level-id": "42e01f"
                   },
                   "payloadType": 125,
                   "rtcpFeedback": [
                       {
                           "parameter": "",
                           "type": "goog-remb"
                       },
                       {
                           "parameter": "",
                           "type": "transport-cc"
                       },
                       {
                           "parameter": "fir",
                           "type": "ccm"
                       },
                       {
                           "parameter": "",
                           "type": "nack"
                       },
                       {
                           "parameter": "pli",
                           "type": "nack"
                       }
                   ]
               },
               {
                   "clockRate": 90000,
                   "mimeType": "video/rtx",
                   "parameters": {
                       "apt": 125
                   },
                   "payloadType": 107,
                   "rtcpFeedback": []
               }
           ],
           "encodings": [
               {
                   "active": true,
                   "dtx": false,
                   "maxBitrate": 500000,
                   "rid": "r0",
                   "scalabilityMode": "S1T3",
                   "scaleResolutionDownBy": 4
               },
               {
                   "active": true,
                   "dtx": false,
                   "maxBitrate": 1000000,
                   "rid": "r1",
                   "scalabilityMode": "S1T3",
                   "scaleResolutionDownBy": 2
               },
               {
                   "active": true,
                   "dtx": false,
                   "maxBitrate": 5000000,
                   "rid": "r2",
                   "scalabilityMode": "S1T3",
                   "scaleResolutionDownBy": 1
               }
           ],
           "headerExtensions": [
               {
                   "encrypt": false,
                   "id": 4,
                   "parameters": {},
                   "uri": "urn:ietf:params:rtp-hdrext:sdes:mid"
               },
               {
                   "encrypt": false,
                   "id": 5,
                   "parameters": {},
                   "uri": "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
               },
               {
                   "encrypt": false,
                   "id": 6,
                   "parameters": {},
                   "uri": "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"
               },
               {
                   "encrypt": false,
                   "id": 2,
                   "parameters": {},
                   "uri": "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
               },
               {
                   "encrypt": false,
                   "id": 3,
                   "parameters": {},
                   "uri": "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
               },
               {
                   "encrypt": false,
                   "id": 13,
                   "parameters": {},
                   "uri": "urn:3gpp:video-orientation"
               },
               {
                   "encrypt": false,
                   "id": 14,
                   "parameters": {},
                   "uri": "urn:ietf:params:rtp-hdrext:toffset"
               }
           ],
           "mid": "0",
           "rtcp": {
               "cname": "de1c8077",
               "reducedSize": true
           }
       }
   }
)"_json;

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
Mixer::Mixer(const std::string & id, Listener * listener, json & data)
    : id(id)
    , listener(listener)
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
Mixer::~Mixer()
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
void Mixer::HandleRequest(Channel::Request * request)
{
    MS_TRACE();

    switch (request->methodId)
    {
        case Channel::Request::MethodId::MIXER_PRODUCE:
        {
            produce(request);
            break;
        }

        case Channel::Request::MethodId::MIXER_CLOSE:
        {
            request->Accept();
            break;
        }

        default:
        {
            MS_THROW_ERROR("unknown method '%s'", request->method.c_str());
        }
    }
}

//******************************************************************************
//******************************************************************************
void Mixer::setNewProducerIdFromInternal(json & internal, std::string & producerId) const
{
    MS_TRACE();

    auto jsonProducerIdIt = internal.find("producerId");

    if (jsonProducerIdIt == internal.end() || !jsonProducerIdIt->is_string())
    {
        MS_THROW_ERROR("missing internal.producerId");
    }

    producerId.assign(jsonProducerIdIt->get<std::string>());

    if (this->mapProducers.find(producerId) != this->mapProducers.end())
    {
        MS_THROW_ERROR("a Producer with same producerId already exists");
    }
}

//******************************************************************************
//******************************************************************************
void Mixer::produce(Channel::Request * request)
{
    MS_TRACE();

    std::string producerId;
    setNewProducerIdFromInternal(request->internal, producerId);

    ProducerPtr producer(new RTC::Producer(producerId, this, g_data/*request->data*/));

    try
    {
        this->rtpListener.AddProducer(producer.get());
    }
    catch (const MediaSoupError& error)
    {
        throw;
    }

    // Notify the listener.
    // This may throw if a Producer with same id already exists.
    try
    {
        this->listener->OnMixerNewProducer(this, producer.get());
    }
    catch (const MediaSoupError& error)
    {
        this->rtpListener.RemoveProducer(producer.get());
        throw;
    }

    // start master mode
    producer->startMasterMode(640, 360);

    // Insert into the map.
    this->mapProducers[producerId] = producer;

    MS_DEBUG_DEV("Producer created [producerId:%s]", producerId.c_str());

    // Take the transport related RTP header extensions of the Producer and
    // add them to the Transport.
    // NOTE: Producer::GetRtpHeaderExtensionIds() returns the original
    // header extension ids of the Producer (and not their mapped values).
    const auto& producerRtpHeaderExtensionIds = producer->GetRtpHeaderExtensionIds();

    if (producerRtpHeaderExtensionIds.mid != 0u)
    {
        this->recvRtpHeaderExtensionIds.mid = producerRtpHeaderExtensionIds.mid;
    }

    if (producerRtpHeaderExtensionIds.rid != 0u)
    {
        this->recvRtpHeaderExtensionIds.rid = producerRtpHeaderExtensionIds.rid;
    }

    if (producerRtpHeaderExtensionIds.rrid != 0u)
    {
        this->recvRtpHeaderExtensionIds.rrid = producerRtpHeaderExtensionIds.rrid;
    }

    if (producerRtpHeaderExtensionIds.absSendTime != 0u)
    {
        this->recvRtpHeaderExtensionIds.absSendTime = producerRtpHeaderExtensionIds.absSendTime;
    }

    if (producerRtpHeaderExtensionIds.transportWideCc01 != 0u)
    {
        this->recvRtpHeaderExtensionIds.transportWideCc01 =
          producerRtpHeaderExtensionIds.transportWideCc01;
    }

    // Create status response.
    json data = json::object();

    data["type"] = RTC::RtpParameters::GetTypeString(producer->GetType());

    request->Accept(data);

    // Check if TransportCongestionControlServer or REMB server must be
    // created.
    // const auto& rtpHeaderExtensionIds = producer->GetRtpHeaderExtensionIds();
    // const auto& codecs                = producer->GetRtpParameters().codecs;

    // Set TransportCongestionControlServer.
    // if (!this->tccServer)
    // {
    //     bool createTccServer{ false };
    //     RTC::BweType bweType;

    //     // Use transport-cc if:
    //     // - there is transport-wide-cc-01 RTP header extension, and
    //     // - there is "transport-cc" in codecs RTCP feedback.
    //     //
    //     // clang-format off
    //     if (
    //         rtpHeaderExtensionIds.transportWideCc01 != 0u &&
    //         std::any_of(
    //             codecs.begin(), codecs.end(), [](const RTC::RtpCodecParameters& codec)
    //             {
    //                 return std::any_of(
    //                     codec.rtcpFeedback.begin(), codec.rtcpFeedback.end(), [](const RTC::RtcpFeedback& fb)
    //                     {
    //                         return fb.type == "transport-cc";
    //                     });
    //             })
    //     )
    //     // clang-format on
    //     {
    //         MS_DEBUG_TAG(bwe, "enabling TransportCongestionControlServer with transport-cc");

    //         createTccServer = true;
    //         bweType         = RTC::BweType::TRANSPORT_CC;
    //     }
    //     // Use REMB if:
    //     // - there is abs-send-time RTP header extension, and
    //     // - there is "remb" in codecs RTCP feedback.
    //     //
    //     // clang-format off
    //     else if (
    //         rtpHeaderExtensionIds.absSendTime != 0u &&
    //         std::any_of(
    //             codecs.begin(), codecs.end(), [](const RTC::RtpCodecParameters& codec)
    //             {
    //                 return std::any_of(
    //                     codec.rtcpFeedback.begin(), codec.rtcpFeedback.end(), [](const RTC::RtcpFeedback& fb)
    //                     {
    //                         return fb.type == "goog-remb";
    //                     });
    //             })
    //     )
    //     // clang-format on
    //     {
    //         MS_DEBUG_TAG(bwe, "enabling TransportCongestionControlServer with REMB");

    //         createTccServer = true;
    //         bweType         = RTC::BweType::REMB;
    //     }

    //     if (createTccServer)
    //     {
    //         this->tccServer = new RTC::TransportCongestionControlServer(this, bweType, RTC::MtuSize);

    //         if (this->maxIncomingBitrate != 0u)
    //             this->tccServer->SetMaxIncomingBitrate(this->maxIncomingBitrate);

    //         if (IsConnected())
    //             this->tccServer->TransportConnected();
    //     }
    // }
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerPaused(RTC::AbstractProducer * producer)
{
    MS_TRACE();
    listener->OnMixerProducerPaused(this, producer);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerResumed(RTC::AbstractProducer* producer)
{
    MS_TRACE();
    listener->OnMixerProducerResumed(this, producer);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerNewRtpStream(RTC::AbstractProducer * producer, 
                                        RTC::RtpStream* rtpStream, 
                                        uint32_t mappedSsrc)
{
    MS_TRACE();
    listener->OnMixerProducerNewRtpStream(this, producer, rtpStream, mappedSsrc);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerRtpStreamScore(RTC::AbstractProducer * producer, 
                                        RTC::RtpStream* rtpStream, 
                                        uint8_t score, uint8_t previousScore)
{
    MS_TRACE();
    listener->OnMixerProducerRtpStreamScore(this, producer, rtpStream, score, previousScore);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerRtcpSenderReport(RTC::AbstractProducer * producer, 
                                        RTC::RtpStream* rtpStream, bool first)
{
    MS_TRACE();
    listener->OnMixerProducerRtcpSenderReport(this, producer, rtpStream, first);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerRtpPacketReceived(RTC::AbstractProducer * producer, RTC::RtpPacket* packet)
{
    MS_TRACE();
    listener->OnMixerProducerRtpPacketReceived(this, producer, packet);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerSendRtcpPacket(RTC::AbstractProducer * /*producer*/, RTC::RTCP::Packet* packet)
{
    MS_TRACE();
    MS_ASSERT(false, "SendRtcpPacket implementation");
    // SendRtcpPacket(packet);
}

//******************************************************************************
//******************************************************************************
void Mixer::OnProducerNeedWorstRemoteFractionLost(RTC::AbstractProducer * producer, 
                                                    uint32_t mappedSsrc, 
                                                    uint8_t& worstRemoteFractionLost)
{
    MS_TRACE();
    listener->OnMixerNeedWorstRemoteFractionLost(this, producer, mappedSsrc, worstRemoteFractionLost);
}

} // namespace RTC