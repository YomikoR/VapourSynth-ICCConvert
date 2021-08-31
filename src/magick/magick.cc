#if defined(HAVE_MAGICK)
#include "magick.hpp"
#include <iostream>

EXPORT_WHEN_W32(cmsHPROFILE) magick_load_icc(const char *input)
{
    cmsHPROFILE profile = nullptr;
    try
    {
        Magick::InitializeMagick("");
        Magick::Image image(input);
        Magick::Blob blob(image.profile("ICC"));
        if (blob.length() > 0)
        {
            profile = cmsOpenProfileFromMem(blob.data(), blob.length());
        }
        Magick::TerminateMagick();
    }
    catch (Magick::Exception &e)
    {
        std::cerr << "iccc: Exception from ImageMagick: \n" << e.what() << std::endl;
        return nullptr;
    }
    return profile;
}

#else
# error This file should not be compiled.
#endif
