//******************************************************************************
//******************************************************************************

#define MS_CLASS "RTC::FileProducer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/FileProducer.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Utils.hpp"
#include "Channel/Notifier.hpp"
#include "RTC/Codecs/Tools.hpp"
#include "RTC/RTCP/FeedbackPs.hpp"
#include "RTC/RTCP/FeedbackRtp.hpp"
#include "RTC/RTCP/XrReceiverReferenceTime.hpp"
#include <cstring>  // std::memcpy()
#include <iterator> // std::ostream_iterator
#include <sstream>  // std::ostringstream

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
FileProducer::FileProducer(const std::string & id, 
                           RTC::AbstractProducer::Listener * listener, 
                           json & data)
  : AbstractProducer(id, listener)
{
    MS_TRACE();

    json::iterator it = data.find("fileName");
    if (it == data.end() || !it->is_string())
    {
        MS_THROW_TYPE_ERROR("missing fileName");
    }

    m_fileName = it->get<std::string>();

}

//******************************************************************************
//******************************************************************************
FileProducer::~FileProducer()
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
void FileProducer::FillJson(json & jsonObject) const
{
    MS_TRACE();

    // AbstractProducer::FillJson(jsonObject);

    jsonObject["fileName"] = m_fileName;
}

//******************************************************************************
//******************************************************************************
void FileProducer::HandleRequest(Channel::Request* request)
{
    MS_TRACE();

    // switch (request->methodId)
    // {
    //     case Channel::Request::MethodId::PRODUCER_PAUSE:
    //     {
    //         if (this->paused)
    //         {
    //             request->Accept();
    //             return;
    //         }
    //     }

    //     case Channel::Request::MethodId::PRODUCER_RESUME:
    //     {
    //         if (!this->paused)
    //         {
    //             request->Accept();
    //             return;
    //         }
    //     }

    //     default: 
    //         break;
    // }

    // Producer::HandleRequest(request);
}

//******************************************************************************
//******************************************************************************
ReceiveRtpPacketResult FileProducer::ReceiveRtpPacket(RTC::RtpPacket * packet)
{
    MS_TRACE();

    // RTC::RtpStreamRecv * stream = GetRtpStream(packet);
    // if (!stream)
    // {
    //     MS_WARN_TAG(rtp, "no stream found for received packet [ssrc:%" PRIu32 "]", packet->GetSsrc());
    //     return ReceiveRtpPacketResult::DISCARDED;
    // }

    // // Media packet.
    // if (packet->GetSsrc() == stream->GetSsrc())
    // {
    //     MS_WARN_TAG(rtp, "received MEDIA packet stream name %s", stream->GetCname().c_str());
    //     return ReceiveRtpPacketResult::MEDIA;
    // }

    // return Producer::ReceiveRtpPacket(packet);
    return ReceiveRtpPacketResult::DISCARDED;
}

} // namespace RTC
