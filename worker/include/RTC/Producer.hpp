#ifndef MS_RTC_PRODUCER_HPP
#define MS_RTC_PRODUCER_HPP

#include "common.hpp"
#include "Channel/Request.hpp"
#include "handles/Timer.hpp"
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
#include <memory>

using json = nlohmann::json;

namespace RTC
{
	class Producer;
	typedef std::shared_ptr<Producer> ProducerPtr;

	class Producer : public AbstractProducer, 
					 public RTC::RtpStreamRecv::Listener, 
					 public RTC::KeyFrameRequestManager::Listener,
					 public Timer::Listener
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
		ReceiveRtpPacketResult DecodeRtpPacket(RTC::RtpPacket* packet);
		ReceiveRtpPacketResult DispatchRtpPacket(RTC::RtpPacket* packet);

		//
		void startMasterMode(const uint32_t width, const uint32_t height);
	    void stop();

		//
		void setMaster(AbstractProducer * master);
		void addSlave(AbstractProducer * slave, 
						const uint32_t x, const uint32_t y, 
						const uint32_t width, const uint32_t height, 
						const uint32_t z);
    	void updateSlave(const std::string & producerId, 
                        const uint32_t x, const uint32_t y, 
                        const uint32_t width, const uint32_t height, 
                        const uint32_t z);
    	void removeSlave(const std::string & producerId); 

		// 
		void onClosedSlave(AbstractProducer * slave);

		//
		AVFramePtr getLastFrame(const uint32_t width, const uint32_t height) const;

	protected:
		RTC::RtpStreamRecv* GetRtpStream(RTC::RtpPacket* packet);
		RTC::RtpStreamRecv* GetRtpStream(const uint32_t ssrc, const uint8_t payloadType, const std::string & rid);

	private:
		RTC::RtpStreamRecv* CreateRtpStream(
		  const uint32_t ssrc, const RTC::RtpCodecParameters& mediaCodec, size_t encodingIdx);
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
		void OnRtpStreamResendPackets(
			  RTC::RtpStreamRecv* rtpStream, const std::vector<uint16_t> & seqNumbers) override;


		/* Pure virtual methods inherited from RTC::KeyFrameRequestManager::Listener. */
	public:
		void OnKeyFrameNeeded(RTC::KeyFrameRequestManager* keyFrameRequestManager, uint32_t ssrc) override;

	private:
	    void OnTimer(Timer * timer); 


	private:
		// Others.
		struct RtpMapping rtpMapping;
		std::vector<RTC::RtpStreamRecv*> rtpStreamByEncodingIdx;
		// Video orientation.
		bool videoOrientationDetected{ false };
		struct VideoOrientation videoOrientation;
		struct TraceEventTypes traceEventTypes;

		// 40 - 25 fps
		// 67 - 15 fps
		const uint32_t m_timerDelay { 40 };
		Timer m_timer;

		// TODO need remove when slave closed
		bool                      m_isMasterMode { false };
		AbstractProducer        * m_master       { nullptr };

		struct Slave
		{
			AbstractProducer * producer { nullptr };
			uint32_t           x        { 0 };
			uint32_t           y        { 0 };
			uint32_t           width    { 0 };
			uint32_t           height   { 0 };
			uint32_t           z        { 0 };

	        SwsContext       * swc      { nullptr };


			inline bool operator == (const Slave & other) const { return producer == other.producer; }
			inline bool operator == (const AbstractProducer * ptr) const { return producer == ptr; }
			inline bool operator == (const std::string & id) const { return producer->id == id; }
		};
		std::vector<Slave>        m_slaves;
	};
} // namespace RTC

#endif
