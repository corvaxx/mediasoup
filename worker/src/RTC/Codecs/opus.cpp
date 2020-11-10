//******************************************************************************
//******************************************************************************

#include "RTC/Codecs/opus.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

//******************************************************************************
//******************************************************************************
namespace RTC
{
namespace Codecs
{


//******************************************************************************
//******************************************************************************
// static 
void Opus::ProcessRtpPacket(RTC::RtpPacket * /*packet*/)
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
// static 
bool Opus::UnpackRtpPacket(RTC::UnpackContext & context,
                            const RTC::RtpPacket* packet,
                            std::vector<std::pair<const uint8_t *, size_t> > & nalptrs)
{
    MS_TRACE();
    return false;
}

//******************************************************************************
//******************************************************************************
// static 
bool Opus::ProduceRtpPacket(RTC::ProduceContext & context,
                             const uint8_t * data, const size_t size, 
                             const uint32_t timestamp,
                             std::vector<RTC::RtpPacketPtr> & packets)
{
    MS_TRACE();
    return false;
}

//******************************************************************************
//******************************************************************************
// static 
bool Opus::DecodePacket(RTC::DecodeContext & context,
                            const uint8_t * data, const size_t & size,
                            std::vector<AVFramePtr> & frames)
{
    MS_TRACE();
    return false;
}

//******************************************************************************
//******************************************************************************
// static 
bool Opus::EncodePacket(RTC::EncodeContext & context,
                            const std::vector<AVFramePtr> & frames,
                            std::vector<AVPacketPtr> & packets)
{
    MS_TRACE();
    return false;
}


} // namespace Codecs
} // namespace RTC
