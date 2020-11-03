#define MS_CLASS "RTC::Codecs::H264"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Codecs/H264.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/utsname.h>
#include <openssl/md5.h>

extern "C"
{
    #include <libavcodec/avcodec.h>
}

#define H264_NAL(v) (v & 0x1F)
#define FU_START(v) (v & 0x80)
#define FU_END(v)   (v & 0x40)
#define FU_NAL(v)   (v & 0x1F)

#define P_FU_START    0x80
#define P_FU_END      0x40

#define N_FU_HEADER 2

#define RTP_PACKET_MAX_SIZE 1456

unsigned long md_32(char * string, int length)
{
    MD5_CTX context;
    union
    {
        char     c[16];
        unsigned long x[4];
    } digest;

    MD5_Init(&context);
    MD5_Update(&context, string, length);
    MD5_Final((unsigned char *)&digest, &context);

    unsigned long r = 0;
    for (int i = 0; i < 3; ++i)
    {
        r ^= digest.x[i];
    }
    return r;
}

uint32_t random32(int type)
{
    struct {
        int           type;
        timeval       tv;
        clock_t       cpu;
        pid_t         pid;
        unsigned long hid;
        uid_t         uid;
        gid_t         gid;
        utsname       name;
    } s;

    gettimeofday(&s.tv, 0);
    uname(&s.name);
    s.type = type;
    s.cpu  = clock();
    s.pid  = getpid();
    s.hid  = gethostid();
    s.uid  = getuid();
    s.gid  = getgid();
    /* also: system uptime */

    return md_32((char *)&s, sizeof(s));
}

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

        const uint8_t * h264_nalu_find(const uint8_t * p, const uint8_t* end)
        {
            for (p += 2; p + 1 < end; ++p)
            {
                if (0x01 == *p && 0x00 == *(p - 1) && 0x00 == *(p - 2))
                {
                    return p + 1;
                }
            }
            return end;
        }

        int rtp_h264_pack_nalu(RTC::ProduceContext & context,
                               const uint8_t * nalu, const size_t bytes,
                               const uint32_t timestamp,
                               std::vector<RTC::RtpPacketPtr> & packets)
        {
            uint8_t padding = 0;
            size_t  csrcSize = 0;

            std::size_t size = sizeof(RTC::RtpPacket::Header) +
                                csrcSize +
                                // (headerExtension ? 4 + extensionSize : 0) +
                                bytes +
                                static_cast<size_t>(padding);

            // TODO corrupted if alloc uint8_t[size]
            uint8_t * buffer = new uint8_t[size + 32];
            // uint8_t * buffer = new uint8_t[RTP_PAYLOAD_MAX_SIZE];

            memcpy(buffer + sizeof(RTC::RtpPacket::Header), nalu, bytes);

            RTC::RtpPacket::Header * header = reinterpret_cast<RTC::RtpPacket::Header *>(buffer);

            header->version        = 2;
            header->csrcCount      = 0;
            header->extension      = 0;
            header->marker         = (*nalu & 0x1f) <= 5 ? 1 : 0; // ??? VCL only
            header->padding        = 0;
            header->payloadType    = context.payloadType;
            header->sequenceNumber = htons(++context.sequence);
            header->timestamp      = htonl(timestamp);
            header->ssrc           = htonl(context.ssrc);

            // MS_WARN_TAG(dead, "PRODUCE PACKET SEQ %d", context.sequence);

            // TODO no extensions, temporary
            // std::shared_ptr<RTC::RtpPacket::HeaderExtension> hext(new RTC::RtpPacket::HeaderExtension())
            // RTC::RtpPacket::HeaderExtension * headerExtension = nullptr;
            // size_t extensionSize = 0;

            // packets.emplace_back(new RTC::RtpPacket(header, headerExtension, nalu, bytes, padding, size));
            packets.emplace_back(RTC::RtpPacket::Parse(buffer, size));
            packets.back()->SetBuffer(buffer);

            return packets.size();
        }

        static int rtp_h264_pack_fu_a(RTC::ProduceContext & context,
                                      const uint8_t * nalu, const size_t _bytes,
                                      const uint32_t timestamp,
                                      std::vector<RTC::RtpPacketPtr> & packets)
        {
            // RFC6184 5.3. NAL Unit Header Usage: Table 2 (p15)
            // RFC6184 5.8. Fragmentation Units (FUs) (p29)
            uint8_t fu_indicator = (*nalu & 0xE0) | 28; // FU-A
            uint8_t fu_header    =  *nalu & 0x1F;

            nalu  += 1; // skip NAL Unit Type byte
            size_t bytes = _bytes - 1;
            MS_ASSERT(bytes > 0, "bad buffer");

            size_t payloadLen = 0;

            // FU-A start
            for (fu_header |= P_FU_START; bytes > 0; )
            {
                if (bytes + RTP_FIXED_HEADER <= RTP_PACKET_MAX_SIZE - N_FU_HEADER)
                {
                    assert(0 == (fu_header & P_FU_START));
                    fu_header = P_FU_END | (fu_header & 0x1F); // FU-A end
                    payloadLen = bytes;
                }
                else
                {
                    payloadLen = RTP_PACKET_MAX_SIZE - RTP_FIXED_HEADER - N_FU_HEADER;
                }

                int size = RTP_FIXED_HEADER + N_FU_HEADER + payloadLen;

                // TODO corrupted if alloc uint8_t[size]
                uint8_t * buffer = new uint8_t[size + 32];
                // uint8_t * buffer = new uint8_t[RTP_PAYLOAD_MAX_SIZE];

                RTC::RtpPacket::Header * header = reinterpret_cast<RTC::RtpPacket::Header *>(buffer);

                header->version        = 2;
                header->csrcCount      = 0;
                header->extension      = 0;
                header->marker         = (P_FU_END & fu_header) ? 1 : 0; // set marker flag
                header->padding        = 0;
                header->payloadType    = context.payloadType;
                header->sequenceNumber = htons(++context.sequence);
                header->timestamp      = htonl(timestamp);
                header->ssrc           = htonl(context.ssrc);

                // MS_WARN_TAG(dead, "PRODUCE PACKET SEQ %d", context.sequence);

                // fu_indicator + fu_header
                buffer[RTP_FIXED_HEADER + 0] = fu_indicator;
                buffer[RTP_FIXED_HEADER + 1] = fu_header;

                memcpy(buffer + RTP_FIXED_HEADER + N_FU_HEADER, nalu, payloadLen);

                bytes -= payloadLen;
                nalu  += payloadLen;

                packets.emplace_back(RTC::RtpPacket::Parse(buffer, size));
                packets.back()->SetBuffer(buffer);

                fu_header &= 0x1F; // clear flags
            }

            // if (context.lastTimestamp != 0)
            // {
            //     uint32_t delta = (timestamp - context.lastTimestamp) / packets.size();
            //     for (RTC::RtpPacketPtr & p : packets)
            //     {
            //         p->SetTimestamp(context.lastTimestamp += delta);
            //     }
            // }
            // context.lastTimestamp = timestamp;

            return packets.size();
        }

        bool H264::ProduceRtpPacket(RTC::ProduceContext & context,
                                    const uint8_t * data, const size_t size,
                                    const uint32_t timestamp,
                                    std::vector<RTC::RtpPacketPtr> & packets)
        {
            MS_TRACE();

            packets.clear();

            // context.timestamp = timestamp; //(uint32_t)time * KHz; // ms -> 90KHZ

            int r = 0;
            const uint8_t * pend = data + size;
            const uint8_t * p1   = h264_nalu_find(data, pend);
            const uint8_t * p2   = p1;

            for (; 0 == r && p1 < pend && 0 == r; p1 = p2)
            {
                // filter H.264 start code(0x00000001)
                assert(0 < (*p1 & 0x1F) && (*p1 & 0x1F) < 24);

                p2 = h264_nalu_find(p1 + 1, pend);
                size_t nalu_size = p2 - p1;

                // filter suffix '00' bytes
                if (p2 != pend)
                {
                    --nalu_size;
                }

                while (0 == p1[nalu_size-1])
                {
                    --nalu_size;
                }

                if (nalu_size + RTP_FIXED_HEADER <= RTP_PACKET_MAX_SIZE)
                {
                    // single NAl unit packet
                    // MS_WARN_TAG(dead, "produce single nal unit");
                    r = rtp_h264_pack_nalu(context, p1, nalu_size, timestamp, packets);
                }
                else
                {
                    // MS_WARN_TAG(dead, "produce FU");
                    r = rtp_h264_pack_fu_a(context, p1, nalu_size, timestamp, packets);
                }
            }

            return true;
        }

        inline uint16_t rtp_read_uint16(const uint8_t* ptr)
        {
            return (((uint16_t)ptr[0]) << 8) | ptr[1];
        }

        inline uint32_t rtp_read_uint32(const uint8_t* ptr)
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
        bool rtp_h264_unpack_stap(RTC::UnpackContext & context,
                                  const uint8_t * ptr, int bytes, bool is_stap_b,
                                  std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
        {
            MS_TRACE();

            int n = is_stap_b ? 3 : 1;
            if (bytes < n)
            {
                assert(0);
                return false;
            }

            uint16_t don = is_stap_b ? rtp_read_uint16(ptr + 1) : 0;

            ptr += n; // STAP-A / STAP-B HDR + DON

            uint16_t len = 0;
            for (bytes -= n; bytes > 2; bytes -= len + 2)
            {
                len = rtp_read_uint16(ptr);
                if(len + 2 > bytes)
                {
                    // assert(0);
                    MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

                    context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
                    return false;
                }

                assert(H264_NAL(ptr[2]) > 0 && H264_NAL(ptr[2]) < 24);

                nalptrs.emplace_back(std::make_pair(ptr + 2, len));
                context.flags = 0;

                // move to next NALU
                ptr += len + 2;
                don = (don + 1) % 65536;
            }

            // packet handled
            return true;
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
        bool rtp_h264_unpack_mtap(RTC::UnpackContext & context,
                                  const uint8_t* ptr, int bytes, bool is_mtap24,
                                  std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
        {
            MS_TRACE();

            int n = is_mtap24 ? 3 : 2;

            if (bytes < 3)
            {
                assert(0);
                return false;
            }

            ptr += 3; // MTAP16/MTAP24 HDR + DONB

            uint16_t len = 0;
            for (bytes -= 3; n + 3 < bytes; bytes -= len + 2)
            {
                len = rtp_read_uint16(ptr);
                if(len + 2 > bytes || len < 1 /*DOND*/ + n /*TS offset*/ + 1 /*NALU*/)
                {
                    // assert(0);
                    MS_WARN_TAG(dead, "RTP_PAYLOAD_FLAG_PACKET_LOST");

                    context.flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
                    return false;
                }

                assert(H264_NAL(ptr[n + 3]) > 0 && H264_NAL(ptr[n + 3]) < 24);

                nalptrs.emplace_back(std::make_pair(ptr + 1 + n, len - 1 - n));
                context.flags = 0;

                // move to next NALU
                ptr += len + 2;
            }

            // packet handled
            return true;
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
        bool rtp_h264_unpack_fu(RTC::UnpackContext & context,
                                const uint8_t * ptr, int bytes, bool is_fu_b,
                                std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
        {
            MS_TRACE();

            int n = is_fu_b ? 4 : 2;
            if (bytes < n || context.size + bytes - n > RTP_PAYLOAD_MAX_SIZE)
            {
                assert(false);
                return false;
            }

            uint8_t fuheader = ptr[1];
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
                    return false; // packet discard
                }
                assert(context.size > 0);
            }

            if (bytes > n)
            {
                memmove(context.ptr + context.size, ptr + n, bytes - n);
                context.size += bytes - n;
            }

            if (FU_END(fuheader))
            {
                // MS_WARN_TAG(dead, "FU END");

                if(context.size > 0)
                {
                    nalptrs.emplace_back(std::make_pair(context.ptr, context.size));
                }
                context.flags = 0;
                context.size = 0;

                // packet handled
                return true;
            }

            return false;
        }

        bool H264::UnpackRtpPacket(RTC::UnpackContext & context,
                                   const RTC::RtpPacket * packet,
                                   std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
        {
            MS_TRACE();

            nalptrs.clear();

            const uint8_t * buf   = packet->GetPayload();
            const size_t    len   = packet->GetPayloadLength();

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
                        // MS_WARN_TAG(dead, "STAP-A %" PRIu64, len);
                        rtp_h264_unpack_stap(context, buf, len, false, nalptrs);
                        break;
                    case 25: // STAP-B
                        // MS_WARN_TAG(dead, "STAP-B %" PRIu64, len);
                        rtp_h264_unpack_stap(context, buf, len, true, nalptrs);
                        break;
                    case 26: // MTAP16
                        // MS_WARN_TAG(dead, "MTAP16 %" PRIu64, len);
                        rtp_h264_unpack_mtap(context, buf, len, false, nalptrs);
                        break;
                    case 27: // MTAP24
                        // MS_WARN_TAG(dead, "MTAP24 %" PRIu64, len);
                        rtp_h264_unpack_mtap(context, buf, len, true, nalptrs);
                        break;
                    case 28: // FU-A
                        // MS_WARN_TAG(dead, "FU_A %" PRIu64, len);
                        rtp_h264_unpack_fu(context, buf, len, false, nalptrs);
                        break;
                    case 29: // FU-B
                        // MS_WARN_TAG(dead, "FU_B %" PRIu64, len);
                        rtp_h264_unpack_fu(context, buf, len, true, nalptrs);
                        break;
                    default: // 1-23 NAL unit
                        // MS_WARN_TAG(dead, "NAL %d %" PRIu64, type, len);
                        nalptrs.emplace_back(buf, len);
                        context.flags = 0;
                        break;
                }
            }

            return true;
        }

        bool H264::DecodePacket(RTC::DecodeContext & context,
                                 const uint8_t * data, const size_t & size,
                                 std::vector<AVFramePtr> & frames)
        {
            MS_TRACE();

            // 4 byte header
            uint8_t type = data[4] & 0x1f;
            // MS_WARN_TAG(dead, "DECODE type %d", type);

            if (type ==7)
            {
                // SPS
                context.sps.resize(size);
                memcpy(&context.sps[0], data, size);
            }
            else if (type == 8)
            {
                // PPS
                context.pps.resize(size);
                memcpy(&context.pps[0], data, size);
            }

            if (context.isOpened)
            {
                AVPacket pkt;
                av_init_packet(&pkt);

                // TODO const_cast
                pkt.data = const_cast<uint8_t *>(data);
                pkt.size = size;

                AVFramePtr frame(av_frame_alloc());

                int gotFrame = 0;
                int length = avcodec_decode_video2(context.codecContext.get(), frame.get(), &gotFrame, &pkt);
                if (length < 0)
                {
                    // MS_ASSERT(false, "avcodec_decode_video2 failed");
                    return false;
                }
                if (gotFrame)
                {
                    // MS_WARN_TAG(dead, "DecodePacket FRAME");
                    frames.emplace_back(frame);
                }

                av_free_packet(&pkt);
            }

            return frames.size() > 0;
        }

        bool H264::EncodePacket(RTC::EncodeContext & context,
                                const std::vector<AVFramePtr> & frames)
        {
            MS_TRACE();

            return false;
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
            //  this->payloadDescriptor->hasTid &&
            //  this->payloadDescriptor->tid > context->GetCurrentTemporalLayer() &&
            //  !this->payloadDescriptor->b
            // )
            // // clang-format on
            // {
            //  return false;
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
