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

char *get_sys_color_profile()
{
    HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = { .cbSize = sizeof mi };
    GetMonitorInfoW(monitor, (MONITORINFO*)&mi);

    char *name = NULL;

    HDC ic = CreateICW(mi.szDevice, NULL, NULL, NULL);
    if (!ic)
        goto done;
    wchar_t wname[MAX_PATH + 1];
    if (!GetICMProfileW(ic, &(DWORD){ MAX_PATH }, wname))
        goto done;

    name = mp_to_utf8(wname);
done:
    if (ic)
        DeleteDC(ic);
    return name;
}

#else // not implemented

char *get_sys_color_profile()
{
    return NULL;
}

#endif // OS
