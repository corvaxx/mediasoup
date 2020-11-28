//******************************************************************************
//******************************************************************************

#ifndef MS_RTC_ABSTRACT_PRODUCER_HPP
#define MS_RTC_ABSTRACT_PRODUCER_HPP

#include "Channel/Request.hpp"
#include "Logger.hpp" 
#include "RTC/KeyFrameRequestManager.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RtpStreamRecv.hpp"
#include "RTC/RTCP/CompoundPacket.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"

#include <json.hpp>

using json = nlohmann::json;

//******************************************************************************
//******************************************************************************
namespace RTC
{

enum class ReceiveRtpPacketResult
{
    DISCARDED = 0,
    MEDIA     = 1,
    RETRANSMISSION
};

class AbstractProducer;
typedef std::shared_ptr<AbstractProducer> AbstractProducerPtr;

//******************************************************************************
//******************************************************************************
class AbstractProducer
{
public:
    class Listener
    {
    public:
        virtual void OnProducerPaused(RTC::AbstractProducer * producer)  = 0;
        virtual void OnProducerResumed(RTC::AbstractProducer* producer) = 0;
        virtual void OnProducerNewRtpStream(
          RTC::AbstractProducer * producer, RTC::RtpStream* rtpStream, uint32_t mappedSsrc) = 0;
        virtual void OnProducerRtpStreamScore(
          RTC::AbstractProducer * producer, RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore) = 0;
        virtual void OnProducerRtcpSenderReport(
          RTC::AbstractProducer * producer, RTC::RtpStream* rtpStream, bool first)                         = 0;
        virtual void OnProducerRtpPacketReceived(RTC::AbstractProducer * producer, RTC::RtpPacket* packet) = 0;
        virtual void OnProducerSendRtcpPacket(RTC::AbstractProducer * producer, RTC::RTCP::Packet* packet) = 0;
        virtual void OnProducerNeedWorstRemoteFractionLost(
          RTC::AbstractProducer * producer, uint32_t mappedSsrc, uint8_t& worstRemoteFractionLost) = 0;
    };

public:
    AbstractProducer(const std::string & id, RTC::AbstractProducer::Listener * listener);
    virtual ~AbstractProducer() {}

public:
    virtual void FillJson(json& jsonObject) const = 0;

    //
    virtual void HandleRequest(Channel::Request * request) = 0;

    //
    const struct RTC::RtpHeaderExtensionIds& GetRtpHeaderExtensionIds() const
    {
        return this->rtpHeaderExtensionIds;
    }

    //
    RTC::RtpParameters::Type GetType() const
    {
        return this->type;
    }

    //
    RTC::Media::Kind GetKind() const
    {
        return this->kind;
    }

    //
    bool IsPaused() const
    {
        return this->paused;
    }

    //
    const std::map<RTC::RtpStreamRecv*, uint32_t> & GetRtpStreams()
    {
        return this->mapRtpStreamMappedSsrc;
    }

    //
    const std::vector<uint8_t>* GetRtpStreamScores() const
    {
        return std::addressof(this->rtpStreamScores);
    }

public:
    //
    const RTC::RtpParameters & GetRtpParameters() const
    {
        return this->rtpParameters;
    }

    //
    virtual ReceiveRtpPacketResult ReceiveRtpPacket(RTC::RtpPacket* packet) = 0;

    //
    virtual void ReceiveRtcpSenderReport(RTC::RTCP::SenderReport* report);

    //
    virtual void ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo* ssrcInfo);

    //
    virtual void GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs);

    //
    virtual void RequestKeyFrame(uint32_t mappedSsrc);

public:
    // Passed by argument.
    const std::string id;

protected:
    // Passed by argument.
    RTC::AbstractProducer::Listener          * listener{ nullptr };

    // Allocated by this.
    RTC::KeyFrameRequestManager              * keyFrameRequestManager{ nullptr };
    std::map<uint32_t, RTC::RtpStreamRecv *>   mapSsrcRtpStream;

    // Others.
    bool                                       paused { false };
    RTC::RtpParameters::Type                   type { RTC::RtpParameters::Type::NONE };
    RTC::Media::Kind                           kind;
    std::map<uint32_t, uint32_t>               mapMappedSsrcSsrc;
    std::map<uint32_t, RTC::RtpStreamRecv *>   mapRtxSsrcRtpStream;
    RTC::RtpPacket                           * currentRtpPacket { nullptr };
    struct RTC::RtpHeaderExtensionIds          rtpHeaderExtensionIds;
    std::vector<uint8_t>                       rtpStreamScores;
    std::map<RTC::RtpStreamRecv*, uint32_t>    mapRtpStreamMappedSsrc;
    RTC::RtpParameters                         rtpParameters;
    // Timestamp when last RTCP was sent.
    uint64_t                                   lastRtcpSentTime { 0u };
    uint16_t                                   maxRtcpInterval { 0u };

};

} // namespace RTC


#endif // MS_RTC_ABSTRACT_PRODUCER_HPP