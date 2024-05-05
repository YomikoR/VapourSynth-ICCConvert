#if defined (__APPLE__) && defined (AUTO_PROFILE_COCOA)
#define CMS_NO_REGISTER_KEYWORD 1
#include <lcms2.h>

#include <string.h>
#include <stdio.h>

#define __USE_GNU
#include <dlfcn.h>
#include <libgen.h>

cmsHPROFILE getSystemProfile()
{
    void *dll_handle = dlopen("libiccc_cocoa.dylib", RTLD_LAZY);
    if (!dll_handle)
    {
        fprintf(stderr, "loading iccc_cocoa failed: %s\n", dlerror());
        return NULL;
    }

    cmsHPROFILE (*getProfileFunc) () = NULL;
    if ((getProfileFunc = dlsym(dll_handle, "getProfileByCocoa")))
    {
        return getProfileFunc();
    }

    return NULL;
}

#else
# error This file should not be compiled.
#endif
