//******************************************************************************
//******************************************************************************

#ifndef MS_RTC_MIXER_HPP
#define MS_RTC_MIXER_HPP

#include "Channel/Request.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/RtpListener.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/TransportCongestionControlServer.hpp"

#include <json.hpp>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
class Mixer : public RTC::AbstractProducer::Listener,
              public RTC::TransportCongestionControlServer::Listener
{
public:
    class Listener
    {
    public:
        virtual void OnMixerNewProducer(RTC::Mixer * mixer, 
                                        RTC::AbstractProducer * producer) = 0;
        virtual void OnMixerProducerClosed(RTC::Mixer* Mixer, 
                                               RTC::AbstractProducer* producer) = 0;
        virtual void OnMixerProducerPaused(RTC::Mixer* mixer, 
                                               RTC::AbstractProducer* producer) = 0;
        virtual void OnMixerProducerResumed(RTC::Mixer* mixer, 
                                                RTC::AbstractProducer* producer) = 0;
        virtual void OnMixerProducerNewRtpStream(RTC::Mixer* mixer,
                                                     RTC::AbstractProducer* producer,
                                                     RTC::RtpStream* rtpStream,
                                                     uint32_t mappedSsrc) = 0;
        virtual void OnMixerProducerRtpStreamScore(RTC::Mixer* mixer,
                                                       RTC::AbstractProducer* producer,
                                                       RTC::RtpStream* rtpStream,
                                                       uint8_t score,
                                                       uint8_t previousScore) = 0;
        virtual void OnMixerProducerRtcpSenderReport(RTC::Mixer* mixer, 
                                                         RTC::AbstractProducer* producer, 
                                                         RTC::RtpStream* rtpStream, 
                                                         bool first) = 0;
        virtual void OnMixerProducerRtpPacketReceived(RTC::Mixer* mixer, 
                                                          RTC::AbstractProducer* producer, 
                                                          RTC::RtpPacket* packet) = 0;
        virtual void OnMixerNeedWorstRemoteFractionLost(RTC::Mixer* mixer,
                                                            RTC::AbstractProducer* producer,
                                                            uint32_t mappedSsrc,
                                                            uint8_t& worstRemoteFractionLost) = 0;
        virtual AbstractProducer * getProducerById(const std::string & id) = 0;
    };

public:
    Mixer(const std::string & id, Listener * listener, json & data);
    virtual ~Mixer();

public:
    virtual void HandleRequest(Channel::Request * request);

protected:
    // 
    void setNewProducerIdFromInternal(json & internal, std::string & producerId) const;
    //
    RTC::AbstractProducerPtr getProducerFromInternal(json & internal) const;

    // 
    void produce(Channel::Request * request);
    // 
    void close(Channel::Request * request);
    // 
    void add(Channel::Request * request);
    // 
    void update(Channel::Request * request);
    // 
    void remove(Channel::Request * request);

private:
    virtual bool IsConnected() const;

protected:
    // AbstractProducer::Listener
    virtual void OnProducerPaused(RTC::AbstractProducer * producer);
    virtual void OnProducerResumed(RTC::AbstractProducer* producer);
    virtual void OnProducerNewRtpStream(RTC::AbstractProducer * producer, 
                                            RTC::RtpStream* rtpStream, 
                                            uint32_t mappedSsrc);
    virtual void OnProducerRtpStreamScore(RTC::AbstractProducer * producer, 
                                            RTC::RtpStream* rtpStream, 
                                            uint8_t score, uint8_t previousScore);
    virtual void OnProducerRtcpSenderReport(RTC::AbstractProducer * producer, 
                                            RTC::RtpStream* rtpStream, bool first);
    virtual void OnProducerRtpPacketReceived(RTC::AbstractProducer * producer, RTC::RtpPacket* packet);
    virtual void OnProducerSendRtcpPacket(RTC::AbstractProducer * producer, RTC::RTCP::Packet* packet);
    virtual void OnProducerNeedWorstRemoteFractionLost(RTC::AbstractProducer * producer, 
                                                        uint32_t mappedSsrc, 
                                                        uint8_t& worstRemoteFractionLost);

    // TransportCongestionControlServer::Listener
    virtual void OnTransportCongestionControlServerSendRtcpPacket(RTC::TransportCongestionControlServer* tccServer, RTC::RTCP::Packet* packet);

private:
    const std::string id;
    Listener * listener;

private:
    RTC::RtpListener rtpListener;
    RTC::RtpHeaderExtensionIds recvRtpHeaderExtensionIds;
    std::unordered_map<std::string, RTC::AbstractProducerPtr> mapProducers;

    RTC::TransportCongestionControlServerPtr tccServer;

    uint32_t initialAvailableOutgoingBitrate{ 600000u };
    uint32_t maxIncomingBitrate{ 0u };

};

} // namespace RTC

#endif // MS_RTC_MIXER_HPP
