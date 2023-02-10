#include "common.hpp"
#include <cmath>

// This part generates a BT.1886 profile, basically, taken from mpv
// https://github.com/mpv-player/mpv/blob/ec0006bfa1aaf608a7141929f2871c89ac7a15d6/video/out/gpu/lcms.c#L275-L326

cmsHPROFILE getPlaybackProfile(const cspData &csp, const double gamma, const cmsHPROFILE &displayProfile)
{
    cmsContext context = cmsCreateContext(nullptr, nullptr);
    if (!context) return nullptr;

    cmsCIExyY wp_xyY = {csp.xw, csp.yw, 1.0};
    cmsCIExyYTRIPLE prim_xyY = {
        {csp.xr, csp.yr, 1.0},
        {csp.xg, csp.yg, 1.0},
        {csp.xb, csp.yb, 1.0}
    };
    cmsToneCurve *toneCurve[3] = {0};

    if (gamma <= 0.0) // BT.1886
    {
        // Find black point

        double srcBlack[3];

        cmsCIEXYZ bp_XYZ; // black pt
        if (!cmsDetectDestinationBlackPoint(&bp_XYZ, displayProfile, INTENT_RELATIVE_COLORIMETRIC, 0)) return nullptr;

        // XYZ value of the BP -> linear source space

        cmsToneCurve *linearCurve = cmsBuildGamma(context, 1.0);
        cmsToneCurve *linearCurves[3] = {linearCurve, linearCurve, linearCurve};
        cmsHPROFILE revProfile = cmsCreateRGBProfile(&wp_xyY, &prim_xyY, linearCurves);

        cmsHPROFILE XYZProfile = cmsCreateXYZProfile();
        cmsHTRANSFORM transform_XYZ_src = cmsCreateTransform(XYZProfile, TYPE_XYZ_DBL, revProfile, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
        cmsFreeToneCurve(linearCurve);
        cmsCloseProfile(revProfile);
        cmsCloseProfile(XYZProfile);
        if (!transform_XYZ_src) return nullptr;
        cmsDoTransform(transform_XYZ_src, &bp_XYZ, srcBlack, 1);
        cmsDeleteTransform(transform_XYZ_src);

        // Build the transfer curve of BT.1886

        for (int i = 0; i < 3; ++i)
        {
            const double gamma = 2.4;
            double binv = pow(srcBlack[i], 1.0 / gamma);
            cmsFloat64Number params[4] = {gamma, 1.0 - binv, binv, 0.0};
            if (!(toneCurve[i] = cmsBuildParametricToneCurve(context, 6, params))) return nullptr;
        }
    }
    else // No sanity check here... see filterCreate
    {
        if (!(toneCurve[0] = cmsBuildGamma(context, gamma))) return nullptr;
        toneCurve[1] = toneCurve[0];
        toneCurve[2] = toneCurve[0];
    }

    // Create profile for video

    cmsHPROFILE playbackProfile = cmsCreateRGBProfile(&wp_xyY, &prim_xyY, toneCurve);
    cmsFreeToneCurve(toneCurve[0]);
    if (toneCurve[1] != toneCurve[0]) cmsFreeToneCurve(toneCurve[1]);
    if (toneCurve[2] != toneCurve[0]) cmsFreeToneCurve(toneCurve[2]);

    return playbackProfile;
}
