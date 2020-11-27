//******************************************************************************
//******************************************************************************

#define MS_CLASS "RTC::Mixer"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/Mixer.hpp"
#include "Channel/Request.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
Mixer::Mixer()
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
Mixer::~Mixer()
{
    MS_TRACE();
}

//******************************************************************************
//******************************************************************************
void Mixer::HandleRequest(Channel::Request * request)
{
    MS_TRACE();

    switch (request->methodId)
    {
        case Channel::Request::MethodId::MIXER_PRODUCE:
        {
            request->Accept();
            break;
        }

        case Channel::Request::MethodId::MIXER_CLOSE:
        {
            request->Accept();
            break;
        }

        default:
        {
            MS_THROW_ERROR("unknown method '%s'", request->method.c_str());
        }
    }
}

} // namespace RTC