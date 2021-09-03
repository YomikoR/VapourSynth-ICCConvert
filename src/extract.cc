#include "magick/magick.hpp"
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
extern cmsHPROFILE magick_load_image_icc(const std::string &input, std::string &error_info);
extern cmsBool magick_close_icc(cmsHPROFILE profile);
extern cmsBool magick_write_icc(cmsHPROFILE profile, const std::string &output);
extern cmsHPROFILE magick_create_srgb_icc();
#endif

void VS_CC immxCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
#if defined(_WIN32)
    f_magick_load_image_icc magick_load_image_icc;
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
        FreeLibrary(mmodule); \
        vsapi->setError(out, "iccc: Failed to resolve function "#fname" from the associated libiccc_magick.dll."); \
        return; \
    } \

    RESOLVE_FUNCTION(magick_load_image_icc)
    RESOLVE_FUNCTION(magick_close_icc)
    RESOLVE_FUNCTION(magick_write_icc)
    RESOLVE_FUNCTION(magick_create_srgb_icc)
#endif
    int err = 0;
    std::string input = vsapi->propGetData(in, "filename", 0, &err);
    if (err || !std::filesystem::exists(input))
    {
#if defined(_WIN32)
        FreeLibrary(mmodule);
#endif
        vsapi->setError(out, "iccc: Input image path seems not valid.");
        return;
    }

    std::string error_info;
    // Get icc from ImageMagick
    cmsHPROFILE profile = magick_load_image_icc(input, error_info);

    // Exception by ImageMagick
    if (!error_info.empty())
    {
        vsapi->setError(out, (std::string("iccc: ImageMagick reports the following error:\n") + error_info).c_str());
        if (!profile) magick_close_icc(profile);
#if defined(_WIN32)
        FreeLibrary(mmodule);
#endif
        return;
    }

    // Fallback to sRGB?
    if (!profile)
    {
        bool fallback_srgb = vsapi->propGetInt(in, "fallback_srgb", 0, &err);
        if (err) fallback_srgb = true;
        if (fallback_srgb)
        {
            profile = magick_create_srgb_icc();
        }
        else
        {
#if defined(_WIN32)
            FreeLibrary(mmodule);
#endif
            vsapi->setError(out, "iccc: Failed to extract color profile.");
            return;
        }
    }

    // Write icc to file
    bool write_icc = true;
    std::string output;
    const char *output_c = vsapi->propGetData(in, "output", 0, &err);
    if (err || !output_c)
    {
        output.assign(input + ".icc");
    }
    else
    {
        output.assign(output_c);
    }
    if (std::filesystem::exists(output))
    {
        write_icc = vsapi->propGetInt(in, "overwrite", 0, &err);
        if (err) write_icc = false;
    }
    if (write_icc && std::filesystem::equivalent(output, input))
    {
        magick_close_icc(profile);
#if defined(_WIN32)
        FreeLibrary(mmodule);
#endif
        vsapi->setError(out, "iccc: Output path is identical to input path. This is not allowed.");
        return;
    }
    if (write_icc && !magick_write_icc(profile, output))
    {
        magick_close_icc(profile);
#if defined(_WIN32)
        FreeLibrary(mmodule);
#endif
        vsapi->setError(out, "iccc: Failed to write profile to destination.");
        return;
    }

    // Rendering intent from header
    int intent = cmsGetHeaderRenderingIntent(profile);
    const char *intent_name = print_intent(intent);
    magick_close_icc(profile);
#if defined(_WIN32)
    FreeLibrary(mmodule);
#endif
    vsapi->propSetData(out, "path", output.c_str(), output.size(), paReplace);
    vsapi->propSetData(out, "intent", intent_name, std::strlen(intent_name), paReplace);
}
