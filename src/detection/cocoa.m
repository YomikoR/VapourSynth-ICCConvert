#if defined (__APPLE__) && defined (AUTO_PROFILE_COCOA)
#define CMS_NO_REGISTER_KEYWORD 1
#include <lcms2.h>
#include <Cocoa/cocoa.h>

cmsHPROFILE getProfileByCocoa()
{
    NSWindow *window = [[NSApplication sharedApplication] mainWindow];
    NSColorSpace *cs = [window colorSpace];
    if (cs)
    {
        NSData *iccData = [cs ICCProfileData];
        if (iccData)
        {
            cmsHPROFILE profile = cmsOpenProfileFromMem([iccData bytes], [iccData length]);
            return profile;
        }
    }
    return NULL;
}

#else
# error This file should not be compiled.
#endif
