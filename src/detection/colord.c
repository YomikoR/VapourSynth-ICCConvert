#if defined (__linux__) && defined (AUTO_PROFILE_COLORD)
#include <colord-1/colord.h>

char *cd_get_position(const char *xrandr_device_name)
{
    CdClient *client = NULL;
    if (!(client = cd_client_new())) return NULL;
    if (!cd_client_connect_sync(client, NULL, NULL))
    {
        g_object_unref(client);
        return NULL;
    }

    CdDevice *device = NULL;
    if (!(device = cd_client_find_device_by_property_sync(client, CD_DEVICE_METADATA_XRANDR_NAME, xrandr_device_name, NULL, NULL)))
    {
        g_object_unref(client);
        return NULL;
    }
    if (!cd_device_connect_sync(device, NULL, NULL))
    {
        g_object_unref(device);
        g_object_unref(client);
        return NULL;
    }

    CdProfile *profile = NULL;
    if (!(profile = cd_device_get_default_profile(device)))
    {
        g_object_unref(device);
        g_object_unref(client);
        return NULL;
    }
    if (!cd_profile_connect_sync(profile, NULL, NULL))
    {
        g_object_unref(profile);
        g_object_unref(device);
        g_object_unref(client);
        return NULL;
    }

    CdIcc *icc = NULL;
    if (!(icc = cd_profile_load_icc(profile, CD_ICC_LOAD_FLAGS_FALLBACK_MD5, NULL, NULL)))
    {
        g_object_unref(profile);
        g_object_unref(device);
        g_object_unref(client);
        return NULL;
    }

    char *icc_file = g_strdup(cd_icc_get_filename(icc));

    g_object_unref(icc);
    g_object_unref(profile);
    g_object_unref(device);
    g_object_unref(client);

    return icc_file;
}
#else
# error This file should not be compiled.
#endif // __linux__ && AUTO_PROFILE_COLORD