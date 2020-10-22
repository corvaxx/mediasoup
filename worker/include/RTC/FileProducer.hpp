//******************************************************************************
//******************************************************************************

#ifndef MS_RTC_FILE_PRODUCER_HPP
#define MS_RTC_FILE_PRODUCER_HPP

#include "common.hpp"
#include "Channel/Request.hpp"
#include "RTC/KeyFrameRequestManager.hpp"
#include "RTC/RTCP/CompoundPacket.hpp"
#include "RTC/RTCP/Packet.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

//******************************************************************************
//******************************************************************************
using json = nlohmann::json;

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
class FileProducer : public AbstractProducer
{
public:
	FileProducer(const std::string & id, RTC::AbstractProducer::Listener * listener, json & data);
	virtual ~FileProducer();

public:
	//
    virtual void FillJson(json & jsonObject) const;

    //
	virtual void HandleRequest(Channel::Request * request);

	//
	virtual ReceiveRtpPacketResult ReceiveRtpPacket(RTC::RtpPacket * packet);

	//
	void ReceiveRtcpSenderReport(RTC::RTCP::SenderReport* report);

	//
	void ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo* ssrcInfo);

	//
	void GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs);

	//
	void RequestKeyFrame(uint32_t mappedSsrc);

private:
	std::string m_fileName;
};


} // namespace RTC

#endif
