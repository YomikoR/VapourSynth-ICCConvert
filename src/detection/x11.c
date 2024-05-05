#if defined (__linux__) && defined (AUTO_PROFILE_X11)
#define CMS_NO_REGISTER_KEYWORD 1
#include <lcms2.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <string.h>
#include <stdio.h>
#if (RANDR_MAJOR < 1) || ((RANDR_MAJOR == 1) && (RANDR_MINOR < 2))
# error X11 RandR version should be at least 1.2
#endif

# if defined (AUTO_PROFILE_COLORD)
#  define __USE_GNU
#include <dlfcn.h>
#include <libgen.h>
# else
#include <X11/Xatom.h>
# endif

cmsHPROFILE getSystemProfile()
{
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return NULL;

    // Required for colord
    char xrandr_device_name[230];
    Bool found_monitor = False;

    Window curr = None;
    Window root = None;

    // The atom name will be _ICC_PROFILE for index 0
    // And _ICC_PROFILE_n for index n starting from 1
    // Following the convention of Xinerama (how?)
    int atom_idx = -1;

    XRRScreenResources *resources = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    XRROutputInfo *output_info = NULL;

    /* If we have two monitors, the root window is the combination of them, e.g.
     *     |<-1->|
     *  |<-2->|   
     * will in total have a rectangular shape as 
     *  |..|<-1->|
     *  |<-2->|..|
     */

    // Get Focus

    int revert_to;
    XGetInputFocus(dpy, &curr, &revert_to);
    if (!curr)
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    int x, y;
    unsigned int width, height;
    unsigned int border_width, depth;

    if (!XGetGeometry(dpy, curr, &root, &x, &y, &width, &height, &border_width, &depth) || !root)
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    // Center of the current window relative to the window itself, used for detection

    unsigned int xwc = (x + width) / 2;
    unsigned int ywc = (y + height) / 2;

    // Transform to the coordinates in the root window

    int wx, wy;
    Window child;
    if (!XTranslateCoordinates(dpy, curr, root, xwc, ywc, &wx, &wy, &child))
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    // XRRGetScreenResourcesCurrent provides cached result (fast but...)
    if (!(resources = XRRGetScreenResourcesCurrent(dpy, root)))
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    for (int i = 0; i < resources->ncrtc; ++i)
    {
        crtc_info = NULL;
        if (!(crtc_info = XRRGetCrtcInfo(dpy, resources, resources->crtcs[i]))) continue;

        if (crtc_info->mode == None || !crtc_info->noutput)
        {
            XRRFreeCrtcInfo(crtc_info);
            continue;
        }

        ++atom_idx;

        if (wx >= crtc_info->x && wx <= crtc_info->x + crtc_info->width && wy >= crtc_info->y && wy <= crtc_info->y + crtc_info->height)
        {
            // Window locates in this crtc
            // Outputs should share the same vcgt (or not?)
            // Search in outputs until we get a name
            for (int j = 0; j < crtc_info->noutput; ++j)
            {
                output_info = XRRGetOutputInfo(dpy, resources, crtc_info->outputs[j]);
                if (!output_info) continue;
                if (output_info->connection == RR_Disconnected)
                {
                    XRRFreeOutputInfo(output_info);
                    continue;
                }
                strcpy(xrandr_device_name, output_info->name);
                XRRFreeOutputInfo(output_info);
                if (strlen(xrandr_device_name) > 0)
                {
                    found_monitor = True;
                    break;
                }
            }
        }
        XRRFreeCrtcInfo(crtc_info);
        if (found_monitor) break;
    }

    if (resources) XRRFreeScreenResources(resources);

    // If monitor not found (I won't pay attention to EDID)
    if (!found_monitor || atom_idx < 0)
    {
        XCloseDisplay(dpy);
        return NULL;
    }

# if defined (AUTO_PROFILE_COLORD)

    char *icc_file = NULL;

    void *dll_handle = dlopen("libiccc_colord.so", RTLD_LAZY);

    if (!dll_handle)
    {
        fprintf(stderr, "loading iccc_colord failed: %s\n", dlerror());
        XCloseDisplay(dpy);
        return NULL;
    }
    char *(*cdfunc) (const char *) = NULL;
    if ((cdfunc = dlsym(dll_handle, "cd_get_position")))
    {
        icc_file = (*cdfunc)(xrandr_device_name);
    }

    // YOU ARE OVER IF YOU CLOSE IT
    //dlclose(dll_handle);

    XCloseDisplay(dpy);
    return icc_file ? cmsOpenProfileFromFile(icc_file, "r") : NULL;

# else // X11
    char iccAtomName[32];
    if (atom_idx == 0)
    {
        strcpy(iccAtomName, "_ICC_PROFILE");
    }
    else
    {
        sprintf(iccAtomName, "_ICC_PROFILE_%d", atom_idx);
    }

    Atom iccAtom = XInternAtom(dpy, iccAtomName, True);
    if (!iccAtom) // No ICC
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    Atom retAtom;
    int retFormat;
    unsigned long retLength, retAfter;
    unsigned char *retProperty;
    int resFlag = XGetWindowProperty(dpy, root, iccAtom, 0, INT_MAX, False, XA_CARDINAL, &retAtom, &retFormat, &retLength, &retAfter, &retProperty);
    if (resFlag != Success || retAtom != XA_CARDINAL || retFormat != 8 || retLength < 1) // Got something that is not ICC
    {
        XCloseDisplay(dpy);
        return NULL;
    }

    XCloseDisplay(dpy);
    cmsHPROFILE profile = cmsOpenProfileFromMem(retProperty, retLength);
    XFree(retProperty);
    return profile;

# endif // AUTO_PROFILE_COLORD
}

#else
# error This file should not be compiled.
#endif // __linux__