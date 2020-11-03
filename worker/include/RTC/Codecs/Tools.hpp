#ifndef MS_RTC_CODECS_TOOLS_HPP
#define MS_RTC_CODECS_TOOLS_HPP

#include "common.hpp"
#include "RTC/Codecs/H264.hpp"
#include "RTC/Codecs/PayloadDescriptorHandler.hpp"
#include "RTC/Codecs/VP8.hpp"
#include "RTC/Codecs/VP9.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/RtpPacket.hpp"

namespace RTC
{
    namespace Codecs
    {
        class Tools
        {
        public:
            static bool CanBeKeyFrame(const RTC::RtpCodecMimeType& mimeType)
            {
                MS_TRACE();

                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            case RTC::RtpCodecMimeType::Subtype::H264:
                                return true;
                            default:
                                return false;
                        }
                    }

                    default:
                    {
                        return false;
                    }
                }
            }

            static bool ProduceRtpPacket(RTC::ProduceContext & context,
                                         const uint8_t * data, const size_t size, 
                                         const uint32_t timestamp,
                                         const RTC::RtpCodecMimeType & mimeType, 
                                         std::vector<RTC::RtpPacketPtr> & packets)
            {
                packets.clear();

                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            {
                                assert(false || "unsupported codec");
                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::H264:
                            {
                                return RTC::Codecs::H264::ProduceRtpPacket(context, data, size, timestamp, packets);
                            }

                            default:
                            {
                                // MS_WARN_TAG(dead, "unsupported mime sub %d", static_cast<int>(mimeType.subtype));
                                break;
                            }

                        }
                        break;
                    }
                    case RTC::RtpCodecMimeType::Type::AUDIO:
                    {
                        // MS_WARN_TAG(dead, "Type::AUDIO (not implemented)");
                        break;
                    }

                    default:
                    {
                        // MS_WARN_TAG(dead, "unsupported mime %d", static_cast<int>(mimeType.type));
                        break;
                    }
                }

                return true;
            }

            static bool UnpackRtpPacket(RTC::UnpackContext & context,
                                        RTC::RtpPacket * packet, 
                                        const RTC::RtpCodecMimeType & mimeType, 
                                        std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
            {
                nalptrs.clear();

                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            {
                                assert(false || "unsupported codec");
                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::H264:
                            {
                                return RTC::Codecs::H264::UnpackRtpPacket(context, packet, nalptrs);
                            }

                            default:
                            {
                                // MS_WARN_TAG(dead, "unsupported mime sub %d", static_cast<int>(mimeType.subtype));
                                break;
                            }

                        }
                        break;
                    }
                    case RTC::RtpCodecMimeType::Type::AUDIO:
                    {
                        // MS_WARN_TAG(dead, "Type::AUDIO (not implemented)");
                        break;
                    }

                    default:
                    {
                        // MS_WARN_TAG(dead, "unsupported mime %d", static_cast<int>(mimeType.type));
                        break;
                    }
                }

                return true;
            }

            static bool DecodePacket(RTC::DecodeContext & context,
                                        const RTC::RtpCodecMimeType & mimeType, 
                                        const uint8_t * data, const size_t & size,
                                        std::vector<AVFramePtr> & frames)
            {
                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            {
                                assert(false || "unsupported codec");
                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::H264:
                            {
                                return RTC::Codecs::H264::DecodePacket(context, data, size, frames);
                            }

                            default:
                            {
                                // MS_WARN_TAG(dead, "unsupported mime sub %d", static_cast<int>(mimeType.subtype));
                                break;
                            }

                        }
                        break;
                    }
                    case RTC::RtpCodecMimeType::Type::AUDIO:
                    {
                        // MS_WARN_TAG(dead, "Type::AUDIO (not implemented)");
                        break;
                    }

                    default:
                    {
                        // MS_WARN_TAG(dead, "unsupported mime %d", static_cast<int>(mimeType.type));
                        break;
                    }
                }

                return true;
            }

            static bool EncodePacket(RTC::EncodeContext & context,
                                        const RTC::RtpCodecMimeType & mimeType, 
                                        const std::vector<AVFramePtr> & frames)
            {
                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            {
                                assert(false || "unsupported codec");
                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::H264:
                            {
                                return RTC::Codecs::H264::EncodePacket(context, frames);
                            }

                            default:
                            {
                                // MS_WARN_TAG(dead, "unsupported mime sub %d", static_cast<int>(mimeType.subtype));
                                break;
                            }

                        }
                        break;
                    }
                    case RTC::RtpCodecMimeType::Type::AUDIO:
                    {
                        // MS_WARN_TAG(dead, "Type::AUDIO (not implemented)");
                        break;
                    }

                    default:
                    {
                        // MS_WARN_TAG(dead, "unsupported mime %d", static_cast<int>(mimeType.type));
                        break;
                    }
                }

                return true;
            }

            static void ProcessRtpPacket(RTC::RtpPacket* packet, const RTC::RtpCodecMimeType& mimeType)
            {
                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                            {
                                RTC::Codecs::VP8::ProcessRtpPacket(packet);

                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::VP9:
                            {
                                RTC::Codecs::VP9::ProcessRtpPacket(packet);

                                break;
                            }

                            case RTC::RtpCodecMimeType::Subtype::H264:
                            {
                                RTC::Codecs::H264::ProcessRtpPacket(packet);

                                break;
                            }

                            default:;
                        }
                    }

                    default:;
                }
            }

            static bool IsValidTypeForCodec(RTC::RtpParameters::Type type, const RTC::RtpCodecMimeType& mimeType)
            {
                switch (type)
                {
                    case RTC::RtpParameters::Type::NONE:
                    {
                        return false;
                    }

                    case RTC::RtpParameters::Type::SIMPLE:
                    {
                        return true;
                    }

                    case RTC::RtpParameters::Type::SIMULCAST:
                    {
                        switch (mimeType.type)
                        {
                            case RTC::RtpCodecMimeType::Type::VIDEO:
                            {
                                switch (mimeType.subtype)
                                {
                                    case RTC::RtpCodecMimeType::Subtype::VP8:
                                    case RTC::RtpCodecMimeType::Subtype::H264:
                                        return true;
                                    default:
                                        return false;
                                }
                            }

                            default:
                            {
                                return false;
                            }
                        }
                    }

                    case RTC::RtpParameters::Type::SVC:
                    {
                        switch (mimeType.type)
                        {
                            case RTC::RtpCodecMimeType::Type::VIDEO:
                            {
                                switch (mimeType.subtype)
                                {
                                    case RTC::RtpCodecMimeType::Subtype::VP9:
                                        return true;
                                    default:
                                        return false;
                                }
                            }

                            default:
                            {
                                return false;
                            }
                        }
                    }

                    case RTC::RtpParameters::Type::PIPE:
                    {
                        return true;
                    }

                    default:
                    {
                        return false;
                    }
                }
            }

            static EncodingContext* GetEncodingContext(
              const RTC::RtpCodecMimeType& mimeType, RTC::Codecs::EncodingContext::Params& params)
            {
                switch (mimeType.type)
                {
                    case RTC::RtpCodecMimeType::Type::VIDEO:
                    {
                        switch (mimeType.subtype)
                        {
                            case RTC::RtpCodecMimeType::Subtype::VP8:
                                return new RTC::Codecs::VP8::EncodingContext(params);
                            case RTC::RtpCodecMimeType::Subtype::VP9:
                                return new RTC::Codecs::VP9::EncodingContext(params);
                            case RTC::RtpCodecMimeType::Subtype::H264:
                                return new RTC::Codecs::H264::EncodingContext(params);
                            default:
                                return nullptr;
                        }
                    }

                    default:
                    {
                        return nullptr;
                    }
                }
            }
        };
    } // namespace Codecs
} // namespace RTC

#endif
