#ifndef MS_RTC_ROUTER_HPP
#define MS_RTC_ROUTER_HPP

#include "common.hpp"
#include "Channel/Request.hpp"
#include "PayloadChannel/Notification.hpp"
#include "PayloadChannel/Request.hpp"
#include "RTC/Consumer.hpp"
#include "RTC/DataConsumer.hpp"
#include "RTC/DataProducer.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/Mixer.hpp"
#include "RTC/RtpObserver.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RtpStream.hpp"
#include "RTC/Transport.hpp"
#include <json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace RTC
{
	class Router : public RTC::Transport::Listener, public RTC::Mixer::Listener
	{
	public:
		explicit Router(const std::string& id);
		virtual ~Router();

	public:
		void FillJson(json& jsonObject) const;
		void HandleRequest(Channel::Request* request);
		void HandleRequest(PayloadChannel::Request* request);
		void HandleNotification(PayloadChannel::Notification* notification);

	private:
		void SetNewTransportIdFromInternal(json& internal, std::string& transportId) const;
		void SetNewRtpObserverIdFromInternal(json& internal, std::string& rtpObserverId) const;
		void SetNewMixerIdFromInternal(json & internal, std::string & mixerId) const;

		RTC::Transport        * GetTransportFromInternal(json& internal) const;
		RTC::RtpObserver      * GetRtpObserverFromInternal(json& internal) const;
		RTC::AbstractProducer * GetProducerFromInternal(json& internal) const;
		RTC::Mixer            * GetMixerFromInternal(json & internal) const;

		void createMixer(Channel::Request* request);

		/* Pure virtual methods inherited from RTC::Transport::Listener, RTC::Mixer::Listener. */
	public:
        void onNewProducer(RTC::AbstractProducer * producer);
		void OnTransportNewProducer(RTC::Transport* transport, RTC::AbstractProducer* producer) override;
        void OnMixerNewProducer(RTC::Mixer * transport, RTC::AbstractProducer * producer) override;

		void onProducerClosed(RTC::AbstractProducer * producer);
		void OnTransportProducerClosed(RTC::Transport * transport, RTC::AbstractProducer* producer) override;
        void OnMixerProducerClosed(RTC::Mixer * Mixer, RTC::AbstractProducer * producer) override;

		void onProducerPaused(RTC::AbstractProducer * producer);
		void OnTransportProducerPaused(RTC::Transport * transport, RTC::AbstractProducer * producer) override;
        void OnMixerProducerPaused(RTC::Mixer * mixer, RTC::AbstractProducer * producer) override;
		
		void onProducerResumed(RTC::AbstractProducer * producer);
		void OnTransportProducerResumed(RTC::Transport * transport, RTC::AbstractProducer * producer) override;
        void OnMixerProducerResumed(RTC::Mixer * mixer, RTC::AbstractProducer * producer) override;
	
		void onProducerNewRtpStream(RTC::AbstractProducer * producer, 
										RTC::RtpStream * rtpStream, 
										uint32_t mappedSsrc);
		void OnTransportProducerNewRtpStream(RTC::Transport * transport, 
											 RTC::AbstractProducer * producer, 
											 RTC::RtpStream * rtpStream, 
											 uint32_t mappedSsrc) override;
        void OnMixerProducerNewRtpStream(RTC::Mixer * mixer,
        								 	RTC::AbstractProducer* producer,
                                            RTC::RtpStream* rtpStream,
                                            uint32_t mappedSsrc);

		void onProducerRtpStreamScore(RTC::AbstractProducer* producer,
									   RTC::RtpStream* rtpStream,
									   uint8_t score,
									   uint8_t previousScore);
		void OnTransportProducerRtpStreamScore(RTC::Transport* transport,
											   RTC::AbstractProducer* producer,
											   RTC::RtpStream* rtpStream,
											   uint8_t score,
											   uint8_t previousScore) override;
        void OnMixerProducerRtpStreamScore(RTC::Mixer* mixer,
                                                RTC::AbstractProducer* producer,
                                                RTC::RtpStream* rtpStream,
                                                uint8_t score,
                                                uint8_t previousScore) override;

		void onProducerRtcpSenderReport(RTC::AbstractProducer * producer, 
										RTC::RtpStream * rtpStream, 
										bool first);
		void OnTransportProducerRtcpSenderReport(RTC::Transport * transport, 
												 RTC::AbstractProducer * producer, 
												 RTC::RtpStream * rtpStream, 
												 bool first) override;
		void OnMixerProducerRtcpSenderReport(RTC::Mixer * mixer, 
												 RTC::AbstractProducer * producer, 
												 RTC::RtpStream * rtpStream, 
												 bool first) override;

		void onProducerRtpPacketReceived(RTC::AbstractProducer * producer, 
										 RTC::RtpPacket * packet);
		void OnTransportProducerRtpPacketReceived(RTC::Transport * transport, 
												  RTC::AbstractProducer * producer, 
												  RTC::RtpPacket * packet) override;
        void OnMixerProducerRtpPacketReceived(RTC::Mixer * mixer, 
                                              RTC::AbstractProducer * producer, 
                                              RTC::RtpPacket * packet) override;

		void onNeedWorstRemoteFractionLost(RTC::AbstractProducer* producer,
											uint32_t mappedSsrc,
											uint8_t& worstRemoteFractionLost);
		void OnTransportNeedWorstRemoteFractionLost(RTC::Transport * transport,
												    RTC::AbstractProducer * producer,
												    uint32_t mappedSsrc,
												    uint8_t & worstRemoteFractionLost) override;
        void OnMixerNeedWorstRemoteFractionLost(RTC::Mixer * mixer,
                                                RTC::AbstractProducer * producer,
                                                uint32_t mappedSsrc,
                                                uint8_t & worstRemoteFractionLost) override;


		void OnTransportNewConsumer(RTC::Transport * transport, 
										RTC::Consumer * consumer, 
										std::string & producerId) override;
		void OnTransportConsumerClosed(RTC::Transport * transport, 
										RTC::Consumer * consumer) override;
		void OnTransportConsumerProducerClosed(RTC::Transport * transport, 
													RTC::Consumer * consumer) override;
		void OnTransportConsumerKeyFrameRequested(RTC::Transport * transport, 
													RTC::Consumer * consumer, 
													uint32_t mappedSsrc) override;
		void OnTransportNewDataProducer(RTC::Transport * transport, 
											RTC::DataProducer * dataProducer) override;
		void OnTransportDataProducerClosed(RTC::Transport * transport, 
											RTC::DataProducer * dataProducer) override;
		void OnTransportDataProducerMessageReceived(RTC::Transport * transport,
													  RTC::DataProducer * dataProducer,
													  uint32_t ppid,
													  const uint8_t * msg,
													  size_t len) override;
		void OnTransportNewDataConsumer(RTC::Transport * transport, 
										RTC::DataConsumer * dataConsumer, 
										std::string & dataProducerId) override;
		void OnTransportDataConsumerClosed(RTC::Transport * transport, 
											RTC::DataConsumer * dataConsumer) override;
		void OnTransportDataConsumerDataProducerClosed(RTC::Transport * transport, 
														RTC::DataConsumer* dataConsumer) override;

        AbstractProducer * getProducerById(const std::string & id) override;


	public:
		// Passed by argument.
		const std::string id;

	private:
		// Allocated by this.
		std::unordered_map<std::string, RTC::Transport*> mapTransports;
		std::unordered_map<std::string, RTC::RtpObserver*> mapRtpObservers;
		std::unordered_map<std::string, RTC::Mixer*> mapMixers;
		// Others.
		std::unordered_map<RTC::AbstractProducer*, std::unordered_set<RTC::Consumer*>> mapProducerConsumers;
		std::unordered_map<RTC::Consumer*, RTC::AbstractProducer*> mapConsumerProducer;
		std::unordered_map<RTC::AbstractProducer*, std::unordered_set<RTC::RtpObserver*>> mapProducerRtpObservers;
		std::unordered_map<std::string, RTC::AbstractProducer*> mapProducers;
		std::unordered_map<RTC::DataProducer*, std::unordered_set<RTC::DataConsumer*>> mapDataProducerDataConsumers;
		std::unordered_map<RTC::DataConsumer*, RTC::DataProducer*> mapDataConsumerDataProducer;
		std::unordered_map<std::string, RTC::DataProducer*> mapDataProducers;
	};
} // namespace RTC

#endif
