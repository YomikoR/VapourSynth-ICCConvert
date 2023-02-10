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
    // Get the relative path to libiccc_cocoa.dylib
    Dl_info dl_info;
    dladdr((void *)getSystemProfile, &dl_info);
    char this_dll_path[4000];
    strcpy(this_dll_path, dl_info.dli_fname);
    char *this_dll_dir = dirname(this_dll_path);
    char cocoa_dll_path[4000];
    sprintf(cocoa_dll_path, "%s/libiccc_cocoa.dylib", this_dll_dir);

    void *dll_handle = dlopen(cocoa_dll_path, RTLD_LAZY);
    if (!dll_handle)
    {
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
