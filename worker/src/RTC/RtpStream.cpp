#define MS_CLASS "RTC::RtpStream"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RtpStream.hpp"
#include "Logger.hpp"
#include "RTC/SeqManager.hpp"

uint32_t random32(int type);

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
}

namespace RTC
{
	/* Static. */

	static constexpr uint16_t MaxDropout{ 3000 };
	static constexpr uint16_t MaxMisorder{ 1500 };
	static constexpr uint32_t RtpSeqMod{ 1 << 16 };
	static constexpr size_t ScoreHistogramLength{ 24 };

	/* Instance methods. */

	RtpStream::RtpStream(
	  RTC::RtpStream::Listener* listener, RTC::RtpStream::Params& params, uint8_t initialScore)
	  : listener(listener), params(params), score(initialScore), activeSinceMs(DepLibUV::GetTimeMs())
	{
		MS_TRACE();
	}

	RtpStream::~RtpStream()
	{
		MS_TRACE();

		delete this->rtxStream;
	}

	void RtpStream::FillJson(json& jsonObject) const
	{
		MS_TRACE();

		// Add params.
		this->params.FillJson(jsonObject["params"]);

		// Add score.
		jsonObject["score"] = this->score;

		// Add rtxStream.
		if (HasRtx())
			this->rtxStream->FillJson(jsonObject["rtxStream"]);
	}

	void RtpStream::FillJsonStats(json& jsonObject)
	{
		MS_TRACE();

		uint64_t nowMs = DepLibUV::GetTimeMs();

		jsonObject["timestamp"]            = nowMs;
		jsonObject["ssrc"]                 = this->params.ssrc;
		jsonObject["kind"]                 = RtpCodecMimeType::type2String[this->params.mimeType.type];
		jsonObject["mimeType"]             = this->params.mimeType.ToString();
		jsonObject["packetsLost"]          = this->packetsLost;
		jsonObject["fractionLost"]         = this->fractionLost;
		jsonObject["packetsDiscarded"]     = this->packetsDiscarded;
		jsonObject["packetsRetransmitted"] = this->packetsRetransmitted;
		jsonObject["packetsRepaired"]      = this->packetsRepaired;
		jsonObject["nackCount"]            = this->nackCount;
		jsonObject["nackPacketCount"]      = this->nackPacketCount;
		jsonObject["pliCount"]             = this->pliCount;
		jsonObject["firCount"]             = this->firCount;
		jsonObject["score"]                = this->score;

		if (!this->params.rid.empty())
			jsonObject["rid"] = this->params.rid;

		if (this->params.rtxSsrc)
			jsonObject["rtxSsrc"] = this->params.rtxSsrc;

		if (this->hasRtt)
			jsonObject["roundTripTime"] = this->rtt;
	}

	void RtpStream::SetRtx(uint8_t payloadType, uint32_t ssrc)
	{
		MS_TRACE();

		this->params.rtxPayloadType = payloadType;
		this->params.rtxSsrc        = ssrc;

		if (HasRtx())
		{
			delete this->rtxStream;
			this->rtxStream = nullptr;
		}

		// Set RTX stream params.
		RTC::RtxStream::Params params;

		params.ssrc             = ssrc;
		params.payloadType      = payloadType;
		params.mimeType.type    = GetMimeType().type;
		params.mimeType.subtype = RTC::RtpCodecMimeType::Subtype::RTX;
		params.clockRate        = GetClockRate();
		params.rrid             = GetRid();
		params.cname            = GetCname();

		// Tell the RtpCodecMimeType to update its string based on current type and subtype.
		params.mimeType.UpdateMimeType();

		this->rtxStream = new RTC::RtxStream(params);
	}

	bool RtpStream::ReceivePacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		uint16_t seq = packet->GetSequenceNumber();

		// If this is the first packet seen, initialize stuff.
		if (!this->started)
		{
			InitSeq(seq);

			this->started     = true;
			this->maxSeq      = seq - 1;
			this->maxPacketTs = packet->GetTimestamp();
			this->maxPacketMs = DepLibUV::GetTimeMs();
		}

		// If not a valid packet ignore it.
		if (!UpdateSeq(packet))
		{
			MS_WARN_TAG(
			  rtp,
			  "invalid packet [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber());

			return false;
		}

		// Update highest seen RTP timestamp.
		if (RTC::SeqManager<uint32_t>::IsSeqHigherThan(packet->GetTimestamp(), this->maxPacketTs))
		{
			this->maxPacketTs = packet->GetTimestamp();
			this->maxPacketMs = DepLibUV::GetTimeMs();
		}

		return true;
	}

	void RtpStream::ResetScore(uint8_t score, bool notify)
	{
		MS_TRACE();

		this->scores.clear();

		if (this->score != score)
		{
			auto previousScore = this->score;

			this->score = score;

			// If previous score was 0 (and new one is not 0) then update activeSinceMs.
			if (previousScore == 0u)
				this->activeSinceMs = DepLibUV::GetTimeMs();

			// Notify the listener.
			if (notify)
				this->listener->OnRtpStreamScore(this, score, previousScore);
		}
	}

	bool RtpStream::UpdateSeq(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		uint16_t seq    = packet->GetSequenceNumber();
		uint16_t udelta = seq - this->maxSeq;

		// If the new packet sequence number is greater than the max seen but not
		// "so much bigger", accept it.
		// NOTE: udelta also handles the case of a new cycle, this is:
		//    maxSeq:65536, seq:0 => udelta:1
		if (udelta < MaxDropout)
		{
			// In order, with permissible gap.
			if (seq < this->maxSeq)
			{
				// Sequence number wrapped: count another 64K cycle.
				this->cycles += RtpSeqMod;
			}

			this->maxSeq = seq;
		}
		// Too old packet received (older than the allowed misorder).
		// Or to new packet (more than acceptable dropout).
		else if (udelta <= RtpSeqMod - MaxMisorder)
		{
			// The sequence number made a very large jump. If two sequential packets
			// arrive, accept the latter.
			if (seq == this->badSeq)
			{
				// Two sequential packets. Assume that the other side restarted without
				// telling us so just re-sync (i.e., pretend this was the first packet).
				MS_WARN_TAG(
				  rtp,
				  "too bad sequence number, re-syncing RTP [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
				  packet->GetSsrc(),
				  packet->GetSequenceNumber());

				InitSeq(seq);

				this->maxPacketTs = packet->GetTimestamp();
				this->maxPacketMs = DepLibUV::GetTimeMs();
			}
			else
			{
				MS_WARN_TAG(
				  rtp,
				  "bad sequence number, ignoring packet [ssrc:%" PRIu32 ", seq:%" PRIu16 "]",
				  packet->GetSsrc(),
				  packet->GetSequenceNumber());

				this->badSeq = (seq + 1) & (RtpSeqMod - 1);

				// Packet discarded due to late or early arriving.
				this->packetsDiscarded++;

				return false;
			}
		}
		// Acceptable misorder.
		else
		{
			// Do nothing.
		}

		return true;
	}

	void RtpStream::UpdateScore(uint8_t score)
	{
		MS_TRACE();

		// Add the score into the histogram.
		if (this->scores.size() == ScoreHistogramLength)
			this->scores.erase(this->scores.begin());

		auto previousScore = this->score;

		// Compute new effective score taking into accout entries in the histogram.
		this->scores.push_back(score);

		/*
		 * Scoring mechanism is a weighted average.
		 *
		 * The more recent the score is, the more weight it has.
		 * The oldest score has a weight of 1 and subsequent scores weight is
		 * increased by one sequentially.
		 *
		 * Ie:
		 * - scores: [1,2,3,4]
		 * - this->scores = ((1) + (2+2) + (3+3+3) + (4+4+4+4)) / 10 = 2.8 => 3
		 */

		size_t weight{ 0 };
		size_t samples{ 0 };
		size_t totalScore{ 0 };

		for (auto score : this->scores)
		{
			weight++;
			samples += weight;
			totalScore += weight * score;
		}

		// clang-tidy "thinks" that this can lead to division by zero but we are
		// smarter.
		// NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
		this->score = static_cast<uint8_t>(std::round(static_cast<double>(totalScore) / samples));

		// Call the listener if the global score has changed.
		if (this->score != previousScore)
		{
			MS_DEBUG_TAG(
			  score,
			  "[added score:%" PRIu8 ", previous computed score:%" PRIu8 ", new computed score:%" PRIu8
			  "] (calling listener)",
			  score,
			  previousScore,
			  this->score);

			// If previous score was 0 (and new one is not 0) then update activeSinceMs.
			if (previousScore == 0u)
				this->activeSinceMs = DepLibUV::GetTimeMs();

			this->listener->OnRtpStreamScore(this, this->score, previousScore);
		}
		else
		{
#if MS_LOG_DEV_LEVEL == 3
			MS_DEBUG_TAG(
			  score,
			  "[added score:%" PRIu8 ", previous computed score:%" PRIu8 ", new computed score:%" PRIu8
			  "] (no change)",
			  score,
			  previousScore,
			  this->score);
#endif
		}
	}

	void RtpStream::PacketRetransmitted(RTC::RtpPacket* /*packet*/)
	{
		MS_TRACE();

		this->packetsRetransmitted++;
	}

	void RtpStream::PacketRepaired(RTC::RtpPacket* /*packet*/)
	{
		MS_TRACE();

		this->packetsRepaired++;
	}

	inline void RtpStream::InitSeq(uint16_t seq)
	{
		MS_TRACE();

		// Initialize/reset RTP counters.
		this->baseSeq = seq;
		this->maxSeq  = seq;
		this->badSeq  = RtpSeqMod + 1; // So seq == badSeq is false.
	}

	void RtpStream::Params::FillJson(json& jsonObject) const
	{
		MS_TRACE();

		jsonObject["encodingIdx"] = this->encodingIdx;
		jsonObject["ssrc"]        = this->ssrc;
		jsonObject["payloadType"] = this->payloadType;
		jsonObject["mimeType"]    = this->mimeType.ToString();
		jsonObject["clockRate"]   = this->clockRate;

		if (!this->rid.empty())
			jsonObject["rid"] = this->rid;

		jsonObject["cname"] = this->cname;

		if (this->rtxSsrc != 0)
		{
			jsonObject["rtxSsrc"]        = this->rtxSsrc;
			jsonObject["rtxPayloadType"] = this->rtxPayloadType;
		}

		jsonObject["useNack"]        = this->useNack;
		jsonObject["usePli"]         = this->usePli;
		jsonObject["useFir"]         = this->useFir;
		jsonObject["useInBandFec"]   = this->useInBandFec;
		jsonObject["useDtx"]         = this->useDtx;
		jsonObject["spatialLayers"]  = this->spatialLayers;
		jsonObject["temporalLayers"] = this->temporalLayers;
	}

	RTC::UnpackContext & RtpStream::GetUnpackContext(const std::string & rid)
	{
		// assert(rid.size() != 0 && "bad rid");

		if (unpackContexts.count(rid) == 0)
		{
			unpackContexts[rid].fileName = "/tmp/debug-out-recv-" + rid + "-" + std::to_string(time(nullptr)) + ".media";
		}
		return unpackContexts[rid];
	}

	RTC::UnpackContext & RtpStream::GetUnpackContext2(const std::string & rid)
	{
		// assert(rid.size() != 0 && "bad rid");

		if (unpackContexts2.count(rid) == 0)
		{
			unpackContexts2[rid].fileName = "/tmp/debug-out-recv-" + rid + "-" + std::to_string(time(nullptr)) + ".media";
		}
		return unpackContexts2[rid];
	}

	RTC::ProduceContext & RtpStream::GetProduceContext(const std::string & rid)
	{
		// assert(rid.size() != 0 && "bad rid");

		if (produceContexts.count(rid) == 0)
		{
			produceContexts[rid].ssrc     = params.ssrc;
			produceContexts[rid].sequence = random32(125) % 8096;
		}
		return produceContexts[rid];
	}

	RTC::DecodeContext & RtpStream::GetDecodeContext(const std::string & rid, bool onlyExisting)
	{
		if (decodeContexts.size() == 0)
		{
			av_register_all();
	        // av_log_set_level(AV_LOG_DEBUG);
	    }
		
		// assert(rid.size() != 0 && "bad rid");

		if (decodeContexts.count(rid) == 0)
		{
			MS_ASSERT(!onlyExisting, "context not exists");

			DecodeContext & c = decodeContexts[rid];

			// TODO codec id must be variable ( from stream ?)
			c.codec        = avcodec_find_decoder(AV_CODEC_ID_H264);
			MS_ASSERT(c.codec, "no codec");

			MS_WARN_TAG(dead, "found codec %s %s", c.codec->name, c.codec->long_name);

			c.codecContext.reset(avcodec_alloc_context3(c.codec));
			MS_ASSERT(c.codecContext, "alloc context failed");

            int result = avcodec_open2(c.codecContext.get(), c.codec, nullptr);
            if (result < 0)
            {
            	char errstr[80];
                MS_WARN_TAG(dead, "codec not opened %x %s", result, av_make_error_string(errstr, 80, result));
            }
            else
            {
                MS_WARN_TAG(dead, "codec OK");
                c.isOpened = true;
            }

            c.jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
            c.jpegContext.reset(avcodec_alloc_context3(c.jpegCodec));

		    c.jpegContext->height  = 180;
		    c.jpegContext->width   = 320;
		    c.jpegContext->time_base    = (AVRational){1, 25}; 
            c.jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;

			result = avcodec_open2(c.jpegContext.get(), c.jpegCodec, NULL);
            if (result < 0)
            {
            	char errstr[80];
                MS_WARN_TAG(dead, "jpeg codec not opened %x %s", result, av_make_error_string(errstr, 80, result));
            }
            else
            {
                MS_WARN_TAG(dead, "jpeg codec OK");
            }
		}
		return decodeContexts[rid];
	}

	RTC::EncodeContext  & RtpStream::GetEncodeContext(const std::string & rid)
	{
		if (encodeContexts.count(rid) == 0)
		{
			// DecodeContext & dc = GetDecodeContext(rid, true);

			EncodeContext & c = encodeContexts[rid];

			// TODO codec id must be variable ( from stream ?)
			c.codec        = avcodec_find_encoder(AV_CODEC_ID_H264);
			MS_ASSERT(c.codec, "no codec");

			MS_WARN_TAG(dead, "found codec %s %s", c.codec->name, c.codec->long_name);

			c.codecContext.reset(avcodec_alloc_context3(c.codec));
			MS_ASSERT(c.codecContext, "alloc context failed");

		    c.codecContext->height  = 180;
		    c.codecContext->width   = 320;
			c.codecContext->time_base    = (AVRational){1, 25};
			c.codecContext->pix_fmt      = AV_PIX_FMT_YUV420P;

            MS_WARN_TAG(dead, "codec params %dx%d timebase %d-%d", 
            			c.codecContext->width, c.codecContext->height,
            			c.codecContext->time_base.num, c.codecContext->time_base.den);

            int result = avcodec_open2(c.codecContext.get(), c.codec, nullptr);
            if (result < 0)
            {
            	char errstr[80];
                MS_WARN_TAG(dead, "codec not opened %x %s", result, av_make_error_string(errstr, 80, result));
            }
            else
            {
                MS_WARN_TAG(dead, "codec OK");
                c.isOpened = true;
            }
		}
		return encodeContexts[rid];
	}


} // namespace RTC
