#include <lcms2.h>

#if defined (_WIN32)
#include <Windows.h>

// This part gets the color profile currently used by Windows
// Taken from mpv

char *mp_to_utf8(const wchar_t *s)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (count <= 0)
        abort();
    char *ret = (char *)malloc(count * sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, s, -1, ret, count, NULL, NULL);
    return ret;
}

cmsHPROFILE get_sys_color_profile()
{
    HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = { .cbSize = sizeof mi };
    GetMonitorInfoW(monitor, (MONITORINFO*)&mi);

    cmsHPROFILE profile = NULL;
    char *name = NULL;

    HDC ic = CreateICW(mi.szDevice, NULL, NULL, NULL);
    if (!ic)
        goto done;
    wchar_t wname[MAX_PATH + 1];
    if (!GetICMProfileW(ic, &(DWORD){ MAX_PATH }, wname))
        goto done;

    name = mp_to_utf8(wname);
    profile = cmsOpenProfileFromFile(name, "r");
    free(name);
done:
    if (ic)
        DeleteDC(ic);
    return profile;
}

#elif defined (__linux__) && defined (AUTO_PROFILE_X11)

// This part gets the color profile currently (?) used by X11
// In Linux it's a mess
// (How should I properly connect to the colord daemon in a plugin?)

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

#if (RANDR_MAJOR < 1) || ((RANDR_MAJOR == 1) && (RANDR_MINOR < 2))
# error X11 RandR version should be at least 1.2
#endif

cmsHPROFILE get_sys_color_profile()
{
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return NULL;

    // Required if implementing with colord
    char xrandr_device_name[100];
    Bool found_monitor = False;

    Window root = None;

    // The atom name will be _ICC_PROFILE for index 0
    // And _ICC_PROFILE_n for index n starting from 1
    // Following the convention of Xinerama (how?)
    int atom_idx = -1;

    XRRScreenResources *resources = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    XRROutputInfo *output_info = NULL;

    for (int scr = 0; scr < ScreenCount(dpy); ++scr)
    {
        if (!(root = RootWindow(dpy, scr))) continue;

        int x, y;
        unsigned int width, height;
        unsigned int border_width, depth;

        if (XGetGeometry(dpy, root, &root, &x, &y, &width, &height, &border_width, &depth))
        if (!root) continue;

        unsigned int wx = (x + width) / 2;
        unsigned int wy = (y + height) / 2;

        // XRRGetScreenResourcesCurrent provides cached result (fast but...)
        if (!(resources = XRRGetScreenResourcesCurrent(dpy, root))) continue;

        for (int i = 0; i < resources->ncrtc; ++i)
        {
            crtc_info = NULL;
            if (!(crtc_info = XRRGetCrtcInfo(dpy, resources, resources->crtcs[i]))) continue;

            if (crtc_info->mode == None || !crtc_info->noutput) continue;

            ++atom_idx;

            if (wx >= crtc_info->x && wx <= crtc_info->x + crtc_info->width && wy >= crtc_info->y && wy <= crtc_info->y + crtc_info->height)
            {
                // Root window locates in this crtc
                // Outputs should share the same vcgt (or not?)
                // Search in outputs until we get a name
                for (int j = 0; j < crtc_info->noutput; ++j)
                {
                    output_info = XRRGetOutputInfo(dpy, resources, crtc_info->outputs[j]);
                    strcpy(xrandr_device_name, output_info->name);
                    if (strlen(xrandr_device_name) > 0)
                    {
                        found_monitor = True;
                        break;
                    }
                }
            }

            if (found_monitor) break;
        }

        if (found_monitor) break;
    }

    if (crtc_info) XRRFreeCrtcInfo(crtc_info);
    if (output_info) XRRFreeOutputInfo(output_info);
    if (resources) XRRFreeScreenResources(resources);

    // If monitor not found (I won't pay attention to EDID)
    if (!found_monitor || atom_idx < 0)
    {
        XCloseDisplay(dpy);
        return NULL;
    }

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
    return cmsOpenProfileFromMem(retProperty, retLength);
}

#else // not implemented

cmsHPROFILE get_sys_color_profile()
{
    return NULL;
}

#endif // OS
