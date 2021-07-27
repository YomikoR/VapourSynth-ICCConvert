#include "common.hpp"

// This part generates a BT.1886 profile, basically, taken from mpv
// https://github.com/mpv-player/mpv/blob/ec0006bfa1aaf608a7141929f2871c89ac7a15d6/video/out/gpu/lcms.c#L275-L326

cmsHPROFILE profile_1886(const cspData &csp, const cmsHPROFILE &lcmsProfileDisplay)
{
    cmsContext lcmsContext = cmsCreateContext(nullptr, nullptr);
    assert (lcmsContext);

    cmsCIExyY lcmsWP_xyY = {csp.xw, csp.yw, 1.0};
    cmsCIExyYTRIPLE lcmsPrim_xyY = {
        .Red = {csp.xr, csp.yr, 1.0},
        .Green = {csp.xg, csp.yg, 1.0},
        .Blue = {csp.xb, csp.yb, 1.0}
    };
    cmsToneCurve *lcmsToneCurve[3] = {0};

    // Find black point

    double src_black[3];

    cmsCIEXYZ lcmsBP_XYZ; // black pt
    assert (cmsDetectBlackPoint(&lcmsBP_XYZ, lcmsProfileDisplay, INTENT_RELATIVE_COLORIMETRIC, 0));

    // XYZ value of the BP -> linear source space

    cmsToneCurve *lcmsLinear = cmsBuildGamma(lcmsContext, 1.0);
    cmsToneCurve *lcmsLinears[3] = {lcmsLinear, lcmsLinear, lcmsLinear};
    cmsHPROFILE lcmsProfileRev = cmsCreateRGBProfile(&lcmsWP_xyY, &lcmsPrim_xyY, lcmsLinears);

    cmsHPROFILE lcmsProfileXYZ = cmsCreateXYZProfile();
    cmsHTRANSFORM lcmsTransform_XYZ_src = cmsCreateTransform(lcmsProfileXYZ, TYPE_XYZ_DBL, lcmsProfileRev, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, 0);
    cmsFreeToneCurve(lcmsLinear);
    cmsCloseProfile(lcmsProfileRev);
    cmsCloseProfile(lcmsProfileXYZ);
    assert (lcmsTransform_XYZ_src);
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
        assert (lcmsToneCurve[i] = cmsBuildParametricToneCurve(lcmsContext, 6, params));
    }

    // Create profile for video

    cmsHPROFILE lcmsProfileVideo = cmsCreateRGBProfile(&lcmsWP_xyY, &lcmsPrim_xyY, lcmsToneCurve);
    for (int i = 0; i < 3; ++i)
    {
        cmsFreeToneCurve(lcmsToneCurve[i]);
    }
    return lcmsProfileVideo;

}