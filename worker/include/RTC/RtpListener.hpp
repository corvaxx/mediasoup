#ifndef MS_RTC_RTP_LISTENER_HPP
#define MS_RTC_RTP_LISTENER_HPP

#include "common.hpp"
#include "RTC/AbstractProducer.hpp"
#include "RTC/RtpPacket.hpp"
#include <json.hpp>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace RTC
{
	class RtpListener
	{
	public:
		void FillJson(json& jsonObject) const;
		void AddProducer(RTC::AbstractProducer * producer);
		void RemoveProducer(RTC::AbstractProducer * producer);
		RTC::AbstractProducer * GetProducer(const RTC::RtpPacket* packet);
		RTC::AbstractProducer * GetProducer(uint32_t ssrc) const;

	public:
		// Table of SSRC / Producer pairs.
		std::unordered_map<uint32_t, RTC::AbstractProducer *> ssrcTable;
		//  Table of MID / Producer pairs.
		std::unordered_map<std::string, RTC::AbstractProducer *> midTable;
		//  Table of RID / Producer pairs.
		std::unordered_map<std::string, RTC::AbstractProducer *> ridTable;
	};
} // namespace RTC

#endif
