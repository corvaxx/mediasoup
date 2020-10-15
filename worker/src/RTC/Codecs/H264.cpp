#define MS_CLASS "RTC::Codecs::H264"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Codecs/H264.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#define H264_NAL(v)	(v & 0x1F)
#define FU_START(v) (v & 0x80)
#define FU_END(v)	(v & 0x40)
#define FU_NAL(v)	(v & 0x1F)

namespace RTC
{
	namespace Codecs
	{
		/* Class methods. */

		H264::PayloadDescriptor* H264::Parse(
		  const uint8_t* data, size_t len, RTC::RtpPacket::FrameMarking* frameMarking, uint8_t frameMarkingLen)
		{
			MS_TRACE();

			if (len < 2)
				return nullptr;

			std::unique_ptr<PayloadDescriptor> payloadDescriptor(new PayloadDescriptor());

			// Use frame-marking.
			if (frameMarking)
			{
				// Read fields.
				payloadDescriptor->s   = frameMarking->start;
				payloadDescriptor->e   = frameMarking->end;
				payloadDescriptor->i   = frameMarking->independent;
				payloadDescriptor->d   = frameMarking->discardable;
				payloadDescriptor->b   = frameMarking->base;
				payloadDescriptor->tid = frameMarking->tid;

				payloadDescriptor->hasTid = true;

				if (frameMarkingLen >= 2)
				{
					payloadDescriptor->hasLid = true;
					payloadDescriptor->lid    = frameMarking->lid;
				}

				if (frameMarkingLen == 3)
				{
					payloadDescriptor->hasTl0picidx = true;
					payloadDescriptor->tl0picidx    = frameMarking->tl0picidx;
				}

				// Detect key frame.
				if (frameMarking->start && frameMarking->independent)
					payloadDescriptor->isKeyFrame = true;
			}

			// NOTE: Unfortunately libwebrtc produces wrong Frame-Marking (without i=1 in
			// keyframes) when it uses H264 hardware encoder (at least in Mac):
			//   https://bugs.chromium.org/p/webrtc/issues/detail?id=10746
			//
			// As a temporal workaround, always do payload parsing to detect keyframes if
			// there is no frame-marking or if there is but keyframe was not detected above.
			if (!frameMarking || !payloadDescriptor->isKeyFrame)
			{
				uint8_t nal = *data & 0x1F;

				switch (nal)
				{
					// Single NAL unit packet.
					// IDR (instantaneous decoding picture).
					case 7:
					{
						payloadDescriptor->isKeyFrame = true;

						break;
					}

					// Aggreation packet.
					// STAP-A.
					case 24:
					{
						size_t offset{ 1 };

						len -= 1;

						// Iterate NAL units.
						while (len >= 3)
						{
							auto naluSize  = Utils::Byte::Get2Bytes(data, offset);
							uint8_t subnal = *(data + offset + sizeof(naluSize)) & 0x1F;

							if (subnal == 7)
							{
								payloadDescriptor->isKeyFrame = true;

								break;
							}

							// Check if there is room for the indicated NAL unit size.
							if (len < (naluSize + sizeof(naluSize)))
								break;

							offset += naluSize + sizeof(naluSize);
							len -= naluSize + sizeof(naluSize);
						}

						break;
					}

					// Aggreation packet.
					// FU-A, FU-B.
					case 28:
					case 29:
					{
						uint8_t subnal   = *(data + 1) & 0x1F;
						uint8_t startBit = *(data + 1) & 0x80;

						if (subnal == 7 && startBit == 128)
							payloadDescriptor->isKeyFrame = true;

						break;
					}
				}
			}

			return payloadDescriptor.release();
		}

		void H264::ProcessRtpPacket(RTC::RtpPacket* packet)
		{
			MS_TRACE();

			auto* data = packet->GetPayload();
			auto len   = packet->GetPayloadLength();
			RtpPacket::FrameMarking* frameMarking{ nullptr };
			uint8_t frameMarkingLen{ 0 };

			// Read frame-marking.
			packet->ReadFrameMarking(&frameMarking, frameMarkingLen);

			PayloadDescriptor* payloadDescriptor = H264::Parse(data, len, frameMarking, frameMarkingLen);

			if (!payloadDescriptor)
				return;

			auto* payloadDescriptorHandler = new PayloadDescriptorHandler(payloadDescriptor);

			packet->SetPayloadDescriptorHandler(payloadDescriptorHandler);
		}

		static inline uint16_t rtp_read_uint16(const uint8_t* ptr)
		{
			return (((uint16_t)ptr[0]) << 8) | ptr[1];
		}

		static inline uint32_t rtp_read_uint32(const uint8_t* ptr)
		{
			return (((uint32_t)ptr[0]) << 24) | (((uint32_t)ptr[1]) << 16) | (((uint32_t)ptr[2]) << 8) | ptr[3];
		}

		// 5.7.1. Single-Time Aggregation Packet (STAP) (p23)
		//  0               1               2               3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                           RTP Header                          |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |STAP-B NAL HDR |            DON                |  NALU 1 Size  |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// | NALU 1 Size   | NALU 1 HDR    |         NALU 1 Data           |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
		// :                                                               :
		// +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |               | NALU 2 Size                   |   NALU 2 HDR  |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                            NALU 2 Data                        |
		// :                                                               :
		// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                               :    ...OPTIONAL RTP padding    |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		static int rtp_h264_unpack_stap(RTC::UnpackContext & context,
										const uint8_t * ptr, int bytes, 
										uint32_t timestamp, int stap_b, 
										packetHandler_t handler)
		{
			int n;
		    uint16_t len;
		    uint16_t don;

		    n = stap_b ? 3 : 1;
			if (bytes < n)
			{
				assert(0);
				return -EINVAL;
			}
			don = stap_b ? rtp_read_uint16(ptr + 1) : 0;
			ptr += n; // STAP-A / STAP-B HDR + DON

			for(bytes -= n; bytes > 2; bytes -= len + 2)
			{
				len = rtp_read_uint16(ptr);
				if(len + 2 > bytes)
				{
					// assert(0);
					MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

					context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
					context.size = 0;

					return -EINVAL;
				}

				assert(H264_NAL(ptr[2]) > 0 && H264_NAL(ptr[2]) < 24);

				handler(ptr + 2, len, context.flags);
				context.flags = 0;
				context.size = 0;

				// move to next NALU
				ptr += len + 2;
				don = (don + 1) % 65536;
			}

			// packet handled
			return 1;
		}

		// 5.7.2. Multi-Time Aggregation Packets (MTAPs) (p27)
		//  0               1               2               3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                          RTP Header                           |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |MTAP16 NAL HDR |   decoding order number base  |  NALU 1 Size  |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// | NALU 1 Size   | NALU 1 DOND   |         NALU 1 TS offset      |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// | NALU 1 HDR    |                NALU 1 DATA                    |
		// +-+-+-+-+-+-+-+-+                                               +
		// :                                                               :
		// +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |               | NALU 2 SIZE                   |   NALU 2 DOND |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// | NALU 2 TS offset              | NALU 2 HDR    |  NALU 2 DATA  |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               |
		// :                                                               :
		// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                               :    ...OPTIONAL RTP padding    |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		static int rtp_h264_unpack_mtap(RTC::UnpackContext & context,
										const uint8_t* ptr, int bytes, 
										uint32_t timestamp, int n, 
										packetHandler_t handler)
		{
			uint16_t dond;
			uint16_t donb;
			uint16_t len;
			uint32_t ts;

			if (bytes < 3)
			{
				assert(0);
				return -EINVAL;
			}
		    
			donb = rtp_read_uint16(ptr + 1);
			ptr += 3; // MTAP16/MTAP24 HDR + DONB

			for(bytes -= 3; n + 3 < bytes; bytes -= len + 2)
			{
				len = rtp_read_uint16(ptr);
				if(len + 2 > bytes || len < 1 /*DOND*/ + n /*TS offset*/ + 1 /*NALU*/)
				{
					// assert(0);
					MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

					context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
					context.size = 0;
					return -EINVAL;
				}

				dond = (ptr[2] + donb) % 65536;
				ts = (uint16_t)rtp_read_uint16(ptr + 3);
				if (3 == n) ts = (ts << 16) | ptr[5]; // MTAP24

				// if the NALU-time is larger than or equal to the RTP timestamp of the packet, 
				// then the timestamp offset equals (the NALU - time of the NAL unit - the RTP timestamp of the packet).
				// If the NALU - time is smaller than the RTP timestamp of the packet,
				// then the timestamp offset is equal to the NALU - time + (2 ^ 32 - the RTP timestamp of the packet).
				{
					ts += timestamp; // wrap 1 << 32
				}

				assert(H264_NAL(ptr[n + 3]) > 0 && H264_NAL(ptr[n + 3]) < 24);

				handler(ptr + 1 + n, len - 1 - n, context.flags);
				context.flags = 0;
				context.size = 0;

				// move to next NALU
				ptr += len + 2;
			}

			// packet handled
			return 1;
		}

		// 5.8. Fragmentation Units (FUs) (p29)
		//  0               1               2               3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |  FU indicator |   FU header   |              DON              |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
		// |                                                               |
		// |                          FU payload                           |
		// |                                                               |
		// |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                               :   ...OPTIONAL RTP padding     |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		static int rtp_h264_unpack_fu(RTC::UnpackContext & context,
									  const uint8_t * ptr, int bytes, 
									  uint32_t timestamp, int fu_b, 
								      packetHandler_t handler)
		{
			int n;
			uint8_t fuheader;
			//uint16_t don;

			n = fu_b ? 4 : 2;
			if (bytes < n || context.size + bytes - n > RTP_PAYLOAD_MAX_SIZE)
			{
				assert(false);
				return -EINVAL;
			}

			if (context.size + bytes - n + 1 /*NALU*/ > context.capacity)
			{
				void* p = NULL;
				int size = context.size + bytes + 1;
				size += size / 4 > 128000 ? size / 4 : 128000;
				p = realloc(context.ptr, size);
				if (!p)
				{
					// set packet lost flag
					MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

					context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
					context.size = 0;
					return -ENOMEM; // error
				}
				context.ptr = (uint8_t*)p;
				context.capacity = size;
			}

			fuheader = ptr[1];
			//don = nbo_r16(ptr + 2);
			if (FU_START(fuheader))
			{
#if 0
				if (context.size > 0)
				{
					context.flags |= RTP_PAYLOAD_FLAG_PACKET_CORRUPT;
					handler(context.ptr, context.size, context.flags);
					context->flags = 0;
					context->size = 0; // reset
				}
#endif

				context.size = 1; // NAL unit type byte
				context.ptr[0] = (ptr[0]/*indicator*/ & 0xE0) | (fuheader & 0x1F);
				assert(H264_NAL(context.ptr[0]) > 0 && H264_NAL(context.ptr[0]) < 24);
			}
else
			{
				if (0 == context.size)
				{
					MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

					context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
					return 0; // packet discard
				}
				assert(context.size > 0);
			}

			context.timestamp = timestamp;
			if (bytes > n)
			{
				assert(context.capacity >= context.size + bytes - n);
				memmove(context.ptr + context.size, ptr + n, bytes - n);
				context.size += bytes - n;
			}

			if(FU_END(fuheader))
			{
				if(context.size > 0)
				{
					handler(context.ptr, context.size, context.flags);
				}
				context.flags = 0;
				context.size = 0;
			}

			// packet handled
			return 1;
		}

		bool H264::UnpackRtpPacket(const RTC::RtpPacket * packet, 
								   RTC::UnpackContext & context,
								   packetHandler_t handler)
		{
			MS_TRACE();

			const uint8_t * buf   = packet->GetPayload();
			const size_t    len   = packet->GetPayloadLength();
			const uint32_t  tstmp = packet->GetTimestamp();

			if (!buf)
			{
				MS_WARN_TAG(dead, "received packet with empty payload");
			}
			else
			{
				uint8_t nal  = buf[0];
		     	uint8_t type = (nal & 0x1f);

				switch(type)
				{
					case 0: // reserved
					case 31: // reserved
						assert(false || "reserved");
						return false; // packet discard

					case 24: // STAP-A
						MS_WARN_TAG(dead, "STAP-A");
						rtp_h264_unpack_stap(context, buf, len, tstmp, 0, handler);
					case 25: // STAP-B
						MS_WARN_TAG(dead, "STAP-B");
						rtp_h264_unpack_stap(context, buf, len, tstmp, 1, handler);
					case 26: // MTAP16
						MS_WARN_TAG(dead, "MTAP16");
						rtp_h264_unpack_mtap(context, buf, len, tstmp, 2, handler);
					case 27: // MTAP24
						MS_WARN_TAG(dead, "MTAP24");
						rtp_h264_unpack_mtap(context, buf, len, tstmp, 3, handler);
					case 28: // FU-A
						MS_WARN_TAG(dead, "FU_A");
						rtp_h264_unpack_fu(context, buf, len, tstmp, 0, handler);
					case 29: // FU-B
						MS_WARN_TAG(dead, "FU_B");
						rtp_h264_unpack_fu(context, buf, len, tstmp, 1, handler);

					default: // 1-23 NAL unit
						MS_WARN_TAG(dead, "NAL %d", type);
						handler(buf, len, 0);
						context.flags = 0;
						context.size = 0;
						break;
				}
			}

			return true;
		}

		/* Instance methods. */

		void H264::PayloadDescriptor::Dump() const
		{
			MS_TRACE();

			MS_DUMP("<PayloadDescriptor>");
			MS_DUMP(
			  "  s:%" PRIu8 "|e:%" PRIu8 "|i:%" PRIu8 "|d:%" PRIu8 "|b:%" PRIu8,
			  this->s,
			  this->e,
			  this->i,
			  this->d,
			  this->b);
			if (this->hasTid)
				MS_DUMP("  tid        : %" PRIu8, this->tid);
			if (this->hasLid)
				MS_DUMP("  lid        : %" PRIu8, this->lid);
			if (this->hasTl0picidx)
				MS_DUMP("  tl0picidx  : %" PRIu8, this->tl0picidx);
			MS_DUMP("  isKeyFrame : %s", this->isKeyFrame ? "true" : "false");
			MS_DUMP("</PayloadDescriptor>");
		}

		H264::PayloadDescriptorHandler::PayloadDescriptorHandler(H264::PayloadDescriptor* payloadDescriptor)
		{
			MS_TRACE();

			this->payloadDescriptor.reset(payloadDescriptor);
		}

		bool H264::PayloadDescriptorHandler::Process(
		  RTC::Codecs::EncodingContext* encodingContext, uint8_t* /*data*/, bool& /*marker*/)
		{
			MS_TRACE();

			auto* context = static_cast<RTC::Codecs::H264::EncodingContext*>(encodingContext);

			MS_ASSERT(context->GetTargetTemporalLayer() >= 0, "target temporal layer cannot be -1");

			// Check if the payload should contain temporal layer info.
			if (context->GetTemporalLayers() > 1 && !this->payloadDescriptor->hasTid)
			{
				MS_WARN_DEV("stream is supposed to have >1 temporal layers but does not have tid field");
			}

			// clang-format off
			if (
				this->payloadDescriptor->hasTid &&
				this->payloadDescriptor->tid > context->GetTargetTemporalLayer()
			)
			// clang-format on
			{
				return false;
			}
			// Upgrade required. Drop current packet if base flag is not set.
			// TODO: Cannot enable this until this issue is fixed (in libwebrtc?):
			//   https://github.com/versatica/mediasoup/issues/306
			//
			// clang-format off
			// else if (
			// 	this->payloadDescriptor->hasTid &&
			// 	this->payloadDescriptor->tid > context->GetCurrentTemporalLayer() &&
			// 	!this->payloadDescriptor->b
			// )
			// // clang-format on
			// {
			// 	return false;
			// }

			// Update/fix current temporal layer.
			// clang-format off
			if (
				this->payloadDescriptor->hasTid &&
				this->payloadDescriptor->tid > context->GetCurrentTemporalLayer()
			)
			// clang-format on
			{
				context->SetCurrentTemporalLayer(this->payloadDescriptor->tid);
			}
			else if (!this->payloadDescriptor->hasTid)
			{
				context->SetCurrentTemporalLayer(0);
			}

			if (context->GetCurrentTemporalLayer() > context->GetTargetTemporalLayer())
				context->SetCurrentTemporalLayer(context->GetTargetTemporalLayer());

			return true;
		}

		void H264::PayloadDescriptorHandler::Restore(uint8_t* /*data*/)
		{
			MS_TRACE();
		}
	} // namespace Codecs
} // namespace RTC
