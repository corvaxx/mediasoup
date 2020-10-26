//******************************************************************************
//******************************************************************************

#define MS_CLASS "RTC::AbstractProducer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/AbstractProducer.hpp"

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
AbstractProducer::AbstractProducer(const std::string & id,
                                   RTC::AbstractProducer::Listener * listener)
    : id(id)
    , listener(listener)
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
void AbstractProducer::ReceiveRtcpSenderReport(RTC::RTCP::SenderReport * report)
{
    MS_TRACE();

    auto it = this->mapSsrcRtpStream.find(report->GetSsrc());

    if (it != this->mapSsrcRtpStream.end())
    {
        auto* rtpStream = it->second;
        bool first      = rtpStream->GetSenderReportNtpMs() == 0;

        rtpStream->ReceiveRtcpSenderReport(report);

        this->listener->OnProducerRtcpSenderReport(this, rtpStream, first);

        return;
    }

    // If not found, check with RTX.
    auto it2 = this->mapRtxSsrcRtpStream.find(report->GetSsrc());

    if (it2 != this->mapRtxSsrcRtpStream.end())
    {
        auto* rtpStream = it2->second;

        rtpStream->ReceiveRtxRtcpSenderReport(report);

        return;
    }

    MS_DEBUG_TAG(rtcp, "RtpStream not found [ssrc:%" PRIu32 "]", report->GetSsrc());
}

//******************************************************************************
//******************************************************************************
void AbstractProducer::ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo * ssrcInfo)
{
    MS_TRACE();

    auto it = this->mapSsrcRtpStream.find(ssrcInfo->GetSsrc());

    if (it == this->mapSsrcRtpStream.end())
    {
        MS_WARN_TAG(rtcp, "RtpStream not found [ssrc:%" PRIu32 "]", ssrcInfo->GetSsrc());

        return;
    }

    auto * rtpStream = it->second;

    rtpStream->ReceiveRtcpXrDelaySinceLastRr(ssrcInfo);
}

//******************************************************************************
//******************************************************************************
void AbstractProducer::GetRtcp(RTC::RTCP::CompoundPacket * packet, uint64_t nowMs)
{
    MS_TRACE();

    if (static_cast<float>((nowMs - this->lastRtcpSentTime) * 1.15) < this->maxRtcpInterval)
        return;

    for (auto& kv : this->mapSsrcRtpStream)
    {
        auto* rtpStream = kv.second;
        auto* report    = rtpStream->GetRtcpReceiverReport();

        packet->AddReceiverReport(report);

        auto* rtxReport = rtpStream->GetRtxRtcpReceiverReport();

        if (rtxReport)
            packet->AddReceiverReport(rtxReport);
    }

    // Add a receiver reference time report if no present in the packet.
    if (!packet->HasReceiverReferenceTime())
    {
        auto ntp     = Utils::Time::TimeMs2Ntp(nowMs);
        auto* report = new RTC::RTCP::ReceiverReferenceTime();

        report->SetNtpSec(ntp.seconds);
        report->SetNtpFrac(ntp.fractions);
        packet->AddReceiverReferenceTime(report);
    }

    this->lastRtcpSentTime = nowMs;
}

//******************************************************************************
//******************************************************************************
void AbstractProducer::RequestKeyFrame(uint32_t mappedSsrc)
{
    MS_TRACE();

    if (!this->keyFrameRequestManager || this->paused)
        return;

    auto it = this->mapMappedSsrcSsrc.find(mappedSsrc);

    if (it == this->mapMappedSsrcSsrc.end())
    {
        MS_WARN_2TAGS(rtcp, rtx, "given mappedSsrc not found, ignoring");

        return;
    }

    uint32_t ssrc = it->second;

    // If the current RTP packet is a key frame for the given mapped SSRC do
    // nothing since we are gonna provide Consumers with the requested key frame
    // right now.
    //
    // NOTE: We know that this may only happen before calling MangleRtpPacket()
    // so the SSRC of the packet is still the original one and not the mapped one.
    //
    // clang-format off
    if (
        this->currentRtpPacket &&
        this->currentRtpPacket->GetSsrc() == ssrc &&
        this->currentRtpPacket->IsKeyFrame()
    )
    // clang-format on
    {
        return;
    }

    this->keyFrameRequestManager->KeyFrameNeeded(ssrc);
}

} // namespace RTC