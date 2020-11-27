//******************************************************************************
//******************************************************************************

#ifndef MS_RTC_MIXER_HPP
#define MS_RTC_MIXER_HPP

#include "Channel/Request.hpp"

#include <json.hpp>
#include <string>

using json = nlohmann::json;

//******************************************************************************
//******************************************************************************
namespace RTC
{

//******************************************************************************
//******************************************************************************
class Mixer
{
public:
    class Listener
    {

    };

public:
    Mixer();
    virtual ~Mixer();

public:
        virtual void HandleRequest(Channel::Request * request);
};

} // namespace RTC

#endif // MS_RTC_MIXER_HPP
