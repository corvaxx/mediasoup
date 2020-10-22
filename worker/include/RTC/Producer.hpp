#ifndef MS_RTC_PRODUCER_HPP
#define MS_RTC_PRODUCER_HPP

#include "common.hpp"
#include "Channel/Request.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/KeyFrameRequestManager.hpp"
#include "RTC/RTCP/CompoundPacket.hpp"
#include "RTC/RTCP/Packet.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace RTC
{
	class Producer : public AbstractProducer, public RTC::RtpStreamRecv::Listener, public RTC::KeyFrameRequestManager::Listener
	{
	private:
		struct RtpEncodingMapping
		{
			std::string rid;
			uint32_t ssrc{ 0 };
			uint32_t mappedSsrc{ 0 };
		};

	private:
		struct RtpMapping
		{
			std::map<uint8_t, uint8_t> codecs;
			std::vector<RtpEncodingMapping> encodings;
		};

	private:
		struct VideoOrientation
		{
			bool camera{ false };
			bool flip{ false };
			uint16_t rotation{ 0 };
		};

	private:
		struct TraceEventTypes
		{
			bool rtp{ false };
			bool keyframe{ false };
			bool nack{ false };
			bool pli{ false };
			bool fir{ false };
		};

	public:
		Producer(const std::string& id, RTC::AbstractProducer::Listener* listener, json& data);
		virtual ~Producer();

	public:
		//
		virtual void FillJson(json& jsonObject) const;

		//
		virtual void FillJsonStats(json& jsonArray) const;

		//
		virtual void HandleRequest(Channel::Request* request);

		//
		virtual ReceiveRtpPacketResult ReceiveRtpPacket(RTC::RtpPacket* packet);

		//
		void ReceiveRtcpSenderReport(RTC::RTCP::SenderReport* report);
		void ReceiveRtcpXrDelaySinceLastRr(RTC::RTCP::DelaySinceLastRr::SsrcInfo* ssrcInfo);
		void GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs);
		void RequestKeyFrame(uint32_t mappedSsrc);

	protected:
		RTC::RtpStreamRecv* GetRtpStream(RTC::RtpPacket* packet);

	private:
		RTC::RtpStreamRecv* CreateRtpStream(
		  RTC::RtpPacket* packet, const RTC::RtpCodecParameters& mediaCodec, size_t encodingIdx);
		void NotifyNewRtpStream(RTC::RtpStreamRecv* rtpStream);
		void PreProcessRtpPacket(RTC::RtpPacket* packet);
		bool MangleRtpPacket(RTC::RtpPacket* packet, RTC::RtpStreamRecv* rtpStream) const;
		void PostProcessRtpPacket(RTC::RtpPacket* packet);
		void EmitScore() const;
		void EmitTraceEventRtpAndKeyFrameTypes(RTC::RtpPacket* packet, bool isRtx = false) const;
		void EmitTraceEventKeyFrameType(RTC::RtpPacket* packet, bool isRtx = false) const;
		void EmitTraceEventPliType(uint32_t ssrc) const;
		void EmitTraceEventFirType(uint32_t ssrc) const;
		void EmitTraceEventNackType() const;

		/* Pure virtual methods inherited from RTC::RtpStreamRecv::Listener. */
	public:
		void OnRtpStreamScore(RTC::RtpStream* rtpStream, uint8_t score, uint8_t previousScore) override;
		void OnRtpStreamSendRtcpPacket(RTC::RtpStreamRecv* rtpStream, RTC::RTCP::Packet* packet) override;
		void OnRtpStreamNeedWorstRemoteFractionLost(
		  RTC::RtpStreamRecv* rtpStream, uint8_t& worstRemoteFractionLost) override;

		/* Pure virtual methods inherited from RTC::KeyFrameRequestManager::Listener. */
	public:
		void OnKeyFrameNeeded(RTC::KeyFrameRequestManager* keyFrameRequestManager, uint32_t ssrc) override;

	private:
		// Allocated by this.
		std::map<uint32_t, RTC::RtpStreamRecv*> mapSsrcRtpStream;
		RTC::KeyFrameRequestManager* keyFrameRequestManager{ nullptr };
		// Others.
		struct RtpMapping rtpMapping;
		std::vector<RTC::RtpStreamRecv*> rtpStreamByEncodingIdx;
		std::map<uint32_t, RTC::RtpStreamRecv*> mapRtxSsrcRtpStream;
		std::map<uint32_t, uint32_t> mapMappedSsrcSsrc;
		RTC::RtpPacket* currentRtpPacket{ nullptr };
		// Timestamp when last RTCP was sent.
		uint64_t lastRtcpSentTime{ 0u };
		uint16_t maxRtcpInterval{ 0u };
		// Video orientation.
		bool videoOrientationDetected{ false };
		struct VideoOrientation videoOrientation;
		struct TraceEventTypes traceEventTypes;
	};
} // namespace RTC

#endif
