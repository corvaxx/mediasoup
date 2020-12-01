//******************************************************************************
//******************************************************************************

#define MS_CLASS "RTC::Mixer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Mixer.hpp"
#include "Channel/Request.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "RTC/Producer.hpp"

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
            close(request);
            break;
        }

        case Channel::Request::MethodId::MIXER_ADD:
        {
            add(request);
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
RTC::AbstractProducerPtr Mixer::getProducerFromInternal(json & internal) const
{
    MS_TRACE();

    auto jsonProducerIdIt = internal.find("mixerProducerId");

    if (jsonProducerIdIt == internal.end() || !jsonProducerIdIt->is_string())
    {
        MS_THROW_ERROR("missing internal.mixerProducerId");
    }

    auto it = this->mapProducers.find(jsonProducerIdIt->get<std::string>());

    if (it == this->mapProducers.end())
    {
        MS_THROW_ERROR("Producer not found");
    }

    RTC::AbstractProducerPtr producer = it->second;
    return producer;
}

//******************************************************************************
//******************************************************************************
void Mixer::produce(Channel::Request * request)
{
    MS_TRACE();

    std::string producerId;
    setNewProducerIdFromInternal(request->internal, producerId);

    ProducerPtr producer(new RTC::Producer(producerId, this, request->data));

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
    producer->startMasterMode(320, 180);

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
void Mixer::close(Channel::Request * request)
{
    MS_TRACE();
    request->Accept();
}

//******************************************************************************
//******************************************************************************
void Mixer::add(Channel::Request * request)
{
    MS_TRACE();

    // AbstractProducerPtr mixerProducer = getProducerFromInternal(request->internal);

    // auto it = request->internal.find("producerId");
    // if (it == request->internal.end() || !it->is_string())
    // {
    //     MS_THROW_ERROR("missing internal.producerId");
    // }

    // AbstractProducer * producer = listener->getProducerById(it->get<std::string>());

    // producer.setMaster(mixerProducer.get());
    // mixerProducer->addSlave(producer);

    request->Accept();
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
    // MS_ASSERT(false, "SendRtcpPacket implementation");
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