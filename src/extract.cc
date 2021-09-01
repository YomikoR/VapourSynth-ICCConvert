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
#endif

void VS_CC immxCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
#if defined(_WIN32)
    f_magick_load_icc magick_load_icc;
    f_magick_close_icc magick_close_icc;
    std::string *magick_dll_path_p = static_cast<std::string *>(userData);
    HMODULE mmodule = LoadLibraryA(magick_dll_path_p->c_str());
    if (!mmodule)
    {
        vsapi->setError(out, "iccc: Failed to load the associated libiccc_magick.dll.");
        return;
    }
    magick_load_icc = (f_magick_load_icc)GetProcAddress(mmodule, "magick_load_icc");
    magick_close_icc = (f_magick_close_icc)GetProcAddress(mmodule, "magick_close_icc");
    if (!magick_load_icc || !magick_close_icc)
    {
        vsapi->setError(out, "iccc: Failed to resolve functions from the associated libiccc_magick.dll.");
        return;
    }
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
#if defined(_WIN32)
        FreeModule(mmodule);
#endif
        return;
    }

    // Fallback to sRGB?
    bool has_embedded = true;
    if (!mprofile.icc)
    {
        has_embedded = false;
        bool fallback_srgb = vsapi->propGetInt(in, "fallback_srgb", 0, &err);
        if (err) fallback_srgb = true;
        if (fallback_srgb)
        {
            mprofile.icc = cmsCreate_sRGBProfile();
        }
        else
        {
#if defined(_WIN32)
            FreeModule(mmodule);
#endif
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
    if (write_icc && !cmsSaveProfileToFile(mprofile.icc, output))
    {
        has_embedded ? magick_close_icc(mprofile.icc) : cmsCloseProfile(mprofile.icc);
#if defined(_WIN32)
        FreeModule(mmodule);
#endif
        vsapi->setError(out, "iccc: Failed to write profile to destination.");
        return;
    }

    // Rendering intent from header
    int intent = cmsGetHeaderRenderingIntent(mprofile.icc);
    const char *intent_name = print_intent(intent);

#if defined(_WIN32)
    // If the profile comes from DLL, we must close in DLL
    has_embedded ? magick_close_icc(mprofile.icc) : cmsCloseProfile(mprofile.icc);
    FreeModule(mmodule);
#else
    cmsCloseProfile(mprofile.icc);
#endif

    vsapi->propSetData(out, "path", output, std::strlen(output), paReplace);
    vsapi->propSetData(out, "intent", intent_name, std::strlen(intent_name), paReplace);
}

#else
# error This file should not be compiled.
#endif
