//******************************************************************************
//******************************************************************************

#ifndef MS_RTC_CODECS_OPUS_HPP
#define MS_RTC_CODECS_OPUS_HPP

#include "common.hpp"
#include "RTC/RtpPacket.hpp"

//******************************************************************************
//******************************************************************************
namespace RTC
{
namespace Codecs
{

//******************************************************************************
//******************************************************************************
class Opus
{
public:

    static void ProcessRtpPacket(RTC::RtpPacket * packet);

    static bool UnpackRtpPacket(RTC::UnpackContext & context,
                                const RTC::RtpPacket* packet,
                                std::vector<std::pair<const uint8_t *, size_t> > & nalptrs);
    static bool ProduceRtpPacket(RTC::ProduceContext & context,
                                 const uint8_t * data, const size_t size, 
                                 const uint32_t timestamp,
                                 std::vector<RTC::RtpPacketPtr> & packets);

    static bool DecodePacket(RTC::DecodeContext & context,
                                const uint8_t * data, const size_t & size,
                                std::vector<AVFramePtr> & frames);

    static bool EncodePacket(RTC::EncodeContext & context,
                                const std::vector<AVFramePtr> & frames,
                                std::vector<AVPacketPtr> & packets);

}; // class Opus

} // namespace Codecs
} // namespace RTC

#endif // MS_RTC_CODECS_OPUS_HPP