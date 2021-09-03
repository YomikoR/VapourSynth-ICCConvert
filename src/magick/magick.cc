#if defined(HAVE_MAGICK)
#include "magick.hpp"

EXPORT_WHEN_W32(cmsHPROFILE) magick_load_image_icc(const std::string &input, std::string &error_info)
{
    cmsHPROFILE profile = nullptr;
    try
    {
        Magick::InitializeMagick("");
        Magick::Image image(input);
        const MagickCore::StringInfo *profile_s = MagickCore::GetImageProfile(image.constImage(), "icc");
        if (profile_s) // has embedded
        {
            profile = cmsOpenProfileFromMem(profile_s->datum, static_cast<cmsUInt32Number>(profile_s->length));
        }
        Magick::TerminateMagick();
        return profile;
    }
    catch (Magick::Exception &e)
    {
        error_info.assign(e.what());
        return nullptr;
    }
}

EXPORT_WHEN_W32(cmsBool) magick_close_icc(cmsHPROFILE profile)
{
    return cmsCloseProfile(profile);
}

EXPORT_WHEN_W32(cmsBool) magick_write_icc(cmsHPROFILE profile, const std::string &output)
{
    return cmsSaveProfileToFile(profile, output.c_str());
}

EXPORT_WHEN_W32(cmsHPROFILE) magick_create_srgb_icc()
{
    return cmsCreate_sRGBProfile();
}

#else
# error This file should not be compiled.
#endif
