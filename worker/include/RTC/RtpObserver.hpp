#ifndef MS_RTC_RTP_PACKET_OBSERVER_HPP
#define MS_RTC_RTP_PACKET_OBSERVER_HPP

#include "common.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/RtpPacket.hpp"
#include <string>

namespace RTC
{
	class RtpObserver
	{
	public:
		RtpObserver(const std::string& id);
		virtual ~RtpObserver();

	public:
		void Pause();
		void Resume();
		bool IsPaused() const
		{
			return this->paused;
		}
		virtual void AddProducer(RTC::AbstractProducer* producer)                              = 0;
		virtual void RemoveProducer(RTC::AbstractProducer* producer)                           = 0;
		virtual void ReceiveRtpPacket(RTC::AbstractProducer* producer, RTC::RtpPacket* packet) = 0;
		virtual void ProducerPaused(RTC::AbstractProducer* producer)                           = 0;
		virtual void ProducerResumed(RTC::AbstractProducer* producer)                          = 0;

	protected:
		virtual void Paused()  = 0;
		virtual void Resumed() = 0;

	public:
		// Passed by argument.
		const std::string id;

	private:
		// Others.
		bool paused{ false };
	};
} // namespace RTC

#endif
