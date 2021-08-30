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


typedef cmsHPROFILE (*f_magick_load_icc)(const char *input);

#endif
