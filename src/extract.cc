#if defined(HAVE_MAGICK)
#include "magick/magick.hpp"
#include <string>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
extern magick_icc_profile magick_load_icc(const char *input);
extern cmsBool magick_close_icc(cmsHPROFILE profile);
extern cmsBool magick_write_icc(cmsHPROFILE profile, const char *output);
extern cmsHPROFILE magick_create_srgb_icc();
#endif

void VS_CC immxCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
#if defined(_WIN32)
    f_magick_load_icc magick_load_icc;
    f_magick_close_icc magick_close_icc;
    f_magick_write_icc magick_write_icc;
    f_magick_create_srgb_icc magick_create_srgb_icc;
    std::string *magick_dll_path_p = static_cast<std::string *>(userData);
    HMODULE mmodule = LoadLibraryA(magick_dll_path_p->c_str());
    if (!mmodule)
    {
        vsapi->setError(out, "iccc: Failed to load the associated libiccc_magick.dll.");
        return;
    }
#define RESOLVE_FUNCTION(fname) fname = (f_##fname)GetProcAddress(mmodule, #fname); \
    if (!(fname)) \
    { \
        vsapi->setError(out, "iccc: Failed to resolve function "#fname" from the associated libiccc_magick.dll."); \
        return; \
    } \

    RESOLVE_FUNCTION(magick_load_icc)
    RESOLVE_FUNCTION(magick_close_icc)
    RESOLVE_FUNCTION(magick_write_icc)
    RESOLVE_FUNCTION(magick_create_srgb_icc)
#endif
    int err = 0;
    std::string input = vsapi->propGetData(in, "filename", 0, &err);
    if (err || !std::filesystem::exists(input))
    {
        vsapi->setError(out, "iccc: Input image path seems not valid.");
        return;
    }

    // Get icc and intent from ImageMagick
    magick_icc_profile mprofile = magick_load_icc(input.c_str());

    // Exception by ImageMagick (e.g. file not found)
    if (!mprofile.error_info.empty())
    {
        vsapi->setError(out, (std::string("iccc: ImageMagick reports the following error:\n") + mprofile.error_info).c_str());
        if (!mprofile.icc) magick_close_icc(mprofile.icc);
        return;
    }

    // Fallback to sRGB?
    if (!mprofile.icc)
    {
        bool fallback_srgb = vsapi->propGetInt(in, "fallback_srgb", 0, &err);
        if (err) fallback_srgb = true;
        if (fallback_srgb)
        {
            mprofile.icc = magick_create_srgb_icc();
        }
        else
        {
            vsapi->setError(out, "iccc: Failed to extract color profile.");
            return;
        }
    }

    // Write icc to file
    bool write_icc = true;
    const char *output = vsapi->propGetData(in, "output", 0, &err);
    if (err || !output)
    {
        output = (input + ".icc").c_str();
    }
    if (std::filesystem::exists(output))
    {
        write_icc = vsapi->propGetInt(in, "overwrite", 0, &err);
        if (err) write_icc = false;
    }
    if (write_icc && !magick_write_icc(mprofile.icc, output))
    {
        magick_close_icc(mprofile.icc);
        vsapi->setError(out, "iccc: Failed to write profile to destination.");
        return;
    }

    // Rendering intent from header
    int intent = cmsGetHeaderRenderingIntent(mprofile.icc);
    const char *intent_name = print_intent(intent);
    magick_close_icc(mprofile.icc);
    vsapi->propSetData(out, "path", output, std::strlen(output), paReplace);
    vsapi->propSetData(out, "intent", intent_name, std::strlen(intent_name), paReplace);
}

#else
# error This file should not be compiled.
#endif
