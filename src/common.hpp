#ifndef _ICCC_COMMON
#define _ICCC_COMMON

#define CMS_NO_REGISTER_KEYWORD 1
#include <lcms2.h>
#include "vapoursynth/VapourSynth4.h"
#include "vapoursynth/VSHelper4.h"

#include <string>
#include <cstring>
#include <vector>

#define ICCC_PLUGIN_ID "yomiko.collection.iccconvert"
#define ICCC_PLUGIN_VERSION VS_MAKE_VERSION(4, 0)

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
extern "C" cmsHPROFILE getSystemProfile();
#else
inline cmsHPROFILE getSystemProfile()
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

cmsHPROFILE getPlaybackProfile(const cspData &csp, const double gamma, const cmsHPROFILE &displayProfile);

#endif
