#ifndef _MAGICK_COMMON
#define _MAGICK_COMMON

#include "../common.hpp"

#if defined(HAVE_MAGICK)
#include <Magick++.h>
#ifndef MAGICKCORE_LCMS_DELEGATE
# error ImageMagick is not built with Little CMS support.
#endif

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
    std::string error_info;
};

typedef magick_icc_profile (*f_magick_load_icc)(const char *input);
typedef cmsBool (*f_magick_close_icc)(cmsHPROFILE profile);
typedef cmsBool (*f_magick_write_icc)(cmsHPROFILE profile, const char* output);
typedef cmsHPROFILE(*f_magick_create_srgb_icc)(void);

constexpr const char* magick_function_list[] = {
    "magick_load_icc",
    "magick_close_icc",
    "magick_write_icc",
    "magick_create_srgb_icc"
};

#endif
