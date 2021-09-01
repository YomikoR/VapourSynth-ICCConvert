#if defined(HAVE_MAGICK)
#include "magick.hpp"
#include <iostream>

EXPORT_WHEN_W32(magick_icc_profile) magick_load_icc(const char *input)
{
    magick_icc_profile mprofile;
    try
    {
        Magick::InitializeMagick("");
        Magick::Image image(input);
        const MagickCore::StringInfo *profile_s = MagickCore::GetImageProfile(image.constImage(), "icc");
        if (profile_s) // has embedded
        {
            mprofile.icc = cmsOpenProfileFromMem(profile_s->datum, profile_s->length);
        }
        Magick::TerminateMagick();
    }
    catch (Magick::Exception &e)
    {
        mprofile.error_info.assign(e.what());
    }
    return mprofile;
}

EXPORT_WHEN_W32(cmsBool) magick_close_icc(cmsHPROFILE profile)
{
    return cmsCloseProfile(profile);
}

EXPORT_WHEN_W32(cmsBool) magick_write_icc(cmsHPROFILE profile, const char* output)
{
    return cmsSaveProfileToFile(profile, output);
}

EXPORT_WHEN_W32(cmsHPROFILE) magick_create_srgb_icc()
{
    return cmsCreate_sRGBProfile();
}

#else
# error This file should not be compiled.
#endif
