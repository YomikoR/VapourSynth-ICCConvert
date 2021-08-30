#if defined(HAVE_MAGICK)
#include "magick/magick.hpp"
#include <string>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
extern cmsHPROFILE magick_load_icc(const char *input);
#endif

void VS_CC immxCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
#if defined(_WIN32)
    f_magick_load_icc magick_load_icc;

    std::string *magick_dll_path_p = static_cast<std::string *>(userData);
    HMODULE mmodule = LoadLibraryA(magick_dll_path_p->c_str());
    if (!mmodule)
    {
        vsapi->setError(out, "iccc: Failed to load the associated libiccc_magick.dll.");
        return;
    }
    magick_load_icc = (f_magick_load_icc)GetProcAddress(mmodule, "magick_load_icc");
    if (!magick_load_icc)
    {
        vsapi->setError(out, "iccc: Failed to resolve function from the associated libiccc_magick.dll.");
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
    const char *output_c = vsapi->propGetData(in, "output", 0, &err);
    std::string output; // cannot be constructed from nullptr
    if (err || !output_c)
    {
        output = input.append(".icc");
    }
    else
    {
        output = output_c;
    }
    if (std::filesystem::exists(output))
    {
        bool overwrite = vsapi->propGetInt(in, "overwrite", 0, &err);
        if (err) overwrite = false;
        if (!overwrite)
        {
            vsapi->propSetData(out, "path", output.c_str(), output.size(), paAppend);
            return;
        }
    }
    bool fallback_srgb = vsapi->propGetInt(in, "fallback_srgb", 0, &err);
    if (err) fallback_srgb = true;
    cmsHPROFILE profile = magick_load_icc(input.c_str());
    if (!profile)
    {
        if (fallback_srgb) profile = cmsCreate_sRGBProfile();
        else
        {
            vsapi->setError(out, "iccc: Failed to extract color profile.");
            return;
        }
    }
    if (!cmsSaveProfileToFile(profile, output.c_str()))
    {
        vsapi->setError(out, "iccc: Failed to write profile to destination.");
        return;
    }
    vsapi->propSetData(out, "path", output.c_str(), output.size(), paReplace);
#if defined(_WIN32)
    FreeModule(mmodule);
#endif
}

#else
# error This file should not be compiled.
#endif
