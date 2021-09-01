#ifndef _MAGICK_COMMON
#define _MAGICK_COMMON

#include "../common.hpp"

#if defined(HAVE_MAGICK)
#include <Magick++.h>
#ifndef MAGICKCORE_LCMS_DELEGATE
# error ImageMagick is not built with Little CMS support.
#endif

inline int magick_2_lcms_intent(MagickCore::RenderingIntent intent)
{
    switch (intent)
    {
    case MagickCore::RelativeIntent:
        return INTENT_RELATIVE_COLORIMETRIC;
    case MagickCore::SaturationIntent:
        return INTENT_SATURATION;
    case MagickCore::PerceptualIntent:
        return INTENT_PERCEPTUAL;
    case MagickCore::AbsoluteIntent:
        return INTENT_ABSOLUTE_COLORIMETRIC;
    default:
        return -1;
    }
}

#endif // HAVE_MAGICK

// for Windows we will build a standalone DLL linking ImageMagick
#if defined(_WIN32)
#define EXPORT_WHEN_W32(ret) VS_EXTERNAL_API(ret)
#else
#define EXPORT_WHEN_W32(ret) ret VS_CC
#endif

struct magick_icc_profile
{
    cmsHPROFILE icc = nullptr;
    int intent = -1;
    std::string error_info;
};

typedef magick_icc_profile (*f_magick_load_icc)(const char *input);
typedef bool (*f_magick_close_icc)(cmsHPROFILE profile);

constexpr const char* magick_function_list[2] = {
    "magick_load_icc",
    "magick_close_icc"
};

#endif
