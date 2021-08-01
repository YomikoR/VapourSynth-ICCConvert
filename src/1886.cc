#include "common.hpp"
#include <cmath>

// This part generates a BT.1886 profile, basically, taken from mpv
// https://github.com/mpv-player/mpv/blob/ec0006bfa1aaf608a7141929f2871c89ac7a15d6/video/out/gpu/lcms.c#L275-L326

cmsHPROFILE get_profile_playback(const cspData &csp, const double gamma, const cmsHPROFILE &lcmsProfileDisplay)
{
    cmsContext lcmsContext = cmsCreateContext(nullptr, nullptr);
    if (!lcmsContext) return nullptr;

    cmsCIExyY lcmsWP_xyY = {csp.xw, csp.yw, 1.0};
    cmsCIExyYTRIPLE lcmsPrim_xyY = {
        {csp.xr, csp.yr, 1.0},
        {csp.xg, csp.yg, 1.0},
        {csp.xb, csp.yb, 1.0}
    };
    cmsToneCurve *lcmsToneCurve[3] = {0};

    if (gamma <= 0.0) // BT.1886
    {
        // Find black point

        double src_black[3];

        cmsCIEXYZ lcmsBP_XYZ; // black pt
        if (!cmsDetectBlackPoint(&lcmsBP_XYZ, lcmsProfileDisplay, INTENT_RELATIVE_COLORIMETRIC, 0)) return nullptr;

        // XYZ value of the BP -> linear source space

        cmsToneCurve *lcmsLinear = cmsBuildGamma(lcmsContext, 1.0);
        cmsToneCurve *lcmsLinears[3] = {lcmsLinear, lcmsLinear, lcmsLinear};
        cmsHPROFILE lcmsProfileRev = cmsCreateRGBProfile(&lcmsWP_xyY, &lcmsPrim_xyY, lcmsLinears);

        cmsHPROFILE lcmsProfileXYZ = cmsCreateXYZProfile();
        cmsHTRANSFORM lcmsTransform_XYZ_src = cmsCreateTransform(lcmsProfileXYZ, TYPE_XYZ_DBL, lcmsProfileRev, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
        cmsFreeToneCurve(lcmsLinear);
        cmsCloseProfile(lcmsProfileRev);
        cmsCloseProfile(lcmsProfileXYZ);
        if (!lcmsTransform_XYZ_src) return nullptr;
        cmsDoTransform(lcmsTransform_XYZ_src, &lcmsBP_XYZ, src_black, 1);
        cmsDeleteTransform(lcmsTransform_XYZ_src);

        // Get contrast from black point

        double contrast = 3.0 / (src_black[0] + src_black[1] + src_black[2]);

        // Build the transfer curve of BT.1886

        for (int i = 0; i < 3; ++i)
        {
            const double gamma = 2.4;
            double binv = pow(src_black[i], 1.0 / gamma);
            cmsFloat64Number params[4] = {gamma, 1.0 - binv, binv, 0.0};
            if (!(lcmsToneCurve[i] = cmsBuildParametricToneCurve(lcmsContext, 6, params))) return nullptr;
        }
    }
    else // No sanity check here... see filterCreate
    {
        if (!(lcmsToneCurve[0] = cmsBuildGamma(lcmsContext, gamma))) return nullptr;
        lcmsToneCurve[1] = lcmsToneCurve[0];
        lcmsToneCurve[2] = lcmsToneCurve[0];
    }

    // Create profile for video

    cmsHPROFILE lcmsProfilePlayback = cmsCreateRGBProfile(&lcmsWP_xyY, &lcmsPrim_xyY, lcmsToneCurve);
    cmsFreeToneCurve(lcmsToneCurve[0]);
    if (lcmsToneCurve[1] != lcmsToneCurve[0]) cmsFreeToneCurve(lcmsToneCurve[1]);
    if (lcmsToneCurve[2] != lcmsToneCurve[0]) cmsFreeToneCurve(lcmsToneCurve[2]);

    return lcmsProfilePlayback;
}