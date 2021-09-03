#ifndef _ICCC_COMMON
#define _ICCC_COMMON

#include <lcms2.h>
#include "vapoursynth/VapourSynth.h"
#include "vapoursynth/VSHelper.h"

#include <string>
#include <cstring>

#define ICCC_PLUGIN_ID "yomiko.collection.iccconvert"

#if defined (_WIN32)
# define DETECTION_IMPLEMENTED 1
#elif defined (__linux__)
# if defined (AUTO_PROFILE_X11)
#  if defined (AUTO_PROFILE_COLORD)
#   define DETECTION_IMPLEMENTED 2
#  else
#   define DETECTION_IMPLEMENTED 3
#  endif
# else
// TODO: Wayland (?)
# endif
#endif

#if defined (DETECTION_IMPLEMENTED)
extern "C" cmsHPROFILE get_profile_sys();
#else
inline cmsHPROFILE get_profile_sys()
{
    return nullptr;
}
#endif

struct cspData
{
    cmsFloat64Number xw;
    cmsFloat64Number yw;
    cmsFloat64Number xr;
    cmsFloat64Number yr;
    cmsFloat64Number xg;
    cmsFloat64Number yg;
    cmsFloat64Number xb;
    cmsFloat64Number yb;
};

const cspData csp_709 = {0.3127, 0.3290, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06};

const cspData csp_601_525 = {0.3127, 0.3290, 0.63, 0.34, 0.31, 0.595, 0.155, 0.07};

const cspData csp_601_625 = {0.3127, 0.3290, 0.64, 0.33, 0.29, 0.6, 0.15, 0.06};

const cspData csp_2020 = {0.3127, 0.3290, 0.708, 0.292, 0.17, 0.797, 0.131, 0.046};

cmsHPROFILE get_profile_playback(const cspData &csp, const double gamma, const cmsHPROFILE &lcmsProfileDisplay);

inline const char *print_intent(int intent)
{
    static const char *intent_names[4] = {
        "perceptual",
        "relative",
        "saturation",
        "absolute"
    };
    if (intent >= 0 && intent <= 3) return intent_names[intent];
    else return "unknown";
}

#endif
