#ifndef MS_RTC_AUDIO_LEVEL_OBSERVER_HPP
#define MS_RTC_AUDIO_LEVEL_OBSERVER_HPP

#include "RTC/RtpObserver.hpp"
#include "handles/Timer.hpp"
#include <json.hpp>
#include <unordered_map>

using json = nlohmann::json;

namespace RTC
{
	class AudioLevelObserver : public RTC::RtpObserver, public Timer::Listener
	{
	private:
		struct DBovs
		{
			uint16_t totalSum{ 0u }; // Sum of dBvos (positive integer).
			size_t count{ 0u };      // Number of dBvos entries in totalSum.
		};

	public:
		AudioLevelObserver(const std::string& id, json& data);
		~AudioLevelObserver() override;

	public:
		void AddProducer(RTC::AbstractProducer* producer) override;
		void RemoveProducer(RTC::AbstractProducer* producer) override;
		void ReceiveRtpPacket(RTC::AbstractProducer* producer, RTC::RtpPacket* packet) override;
		void ProducerPaused(RTC::AbstractProducer* producer) override;
		void ProducerResumed(RTC::AbstractProducer* producer) override;

	private:
		void Paused() override;
		void Resumed() override;
		void Update();
		void ResetMapProducerDBovs();

		/* Pure virtual methods inherited from Timer. */
	protected:
		void OnTimer(Timer* timer) override;

	private:
		// Passed by argument.
		uint16_t maxEntries{ 1u };
		int8_t threshold{ -80 };
		uint16_t interval{ 1000u };
		// Allocated by this.
		Timer* periodicTimer{ nullptr };
		// Others.
		std::unordered_map<RTC::AbstractProducer*, DBovs> mapProducerDBovs;
		bool silence{ true };
	};
} // namespace RTC

#endif
