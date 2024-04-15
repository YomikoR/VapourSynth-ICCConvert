#include "common.hpp"
#include "libp2p/p2p_api.h"
#include "vapoursynth/VSConstants4.h"
#include <unordered_map>
#include <mutex>
#include <memory>

#ifndef TYPE_RGB_FLT_PLANAR
#define TYPE_RGB_FLT_PLANAR (FLOAT_SH(1)|COLORSPACE_SH(PT_RGB)|CHANNELS_SH(3)|BYTES_SH(4)|PLANAR_SH(1))
#endif

constexpr double REC709_ALPHA = 1.09929682680944;
constexpr double REC709_BETA = 0.018053968510807;

// Use it for hashing
struct inputICCData
{
    // The profile, should be closed after transform creation
    cmsHPROFILE profile;

    // Plugin ID from header
    cmsUInt32Number ID32[4];

    // Rendering intent
    cmsUInt32Number intent;

    // Equality
    bool operator==(const inputICCData &ind) const
    {
        for (int j = 0; j < 4; ++j)
        {
            if (ID32[j] != ind.ID32[j]) return false;
        }
        return intent == ind.intent;
    }

    // Hash on construction
    inputICCData(cmsHPROFILE profile, cmsUInt32Number intent) : profile{profile}, intent{intent}
    {
        // MD5 hashing will only fail when OOM, ignored
        cmsMD5computeID(profile);
        cmsGetHeaderProfileID(profile, reinterpret_cast<cmsUInt8Number *>(ID32));
    }
};

struct inputICCHashFunction
{
    size_t operator()(const inputICCData &ind) const
    {
        return ind.intent + ind.ID32[0] + ind.ID32[1] + ind.ID32[2] + ind.ID32[3];
    }
};

struct icccData
{
    // Video
    VSNode *node;
    VSVideoInfo vi;
    std::unordered_map<inputICCData, cmsHTRANSFORM, inputICCHashFunction> transformMap;
    std::mutex mutex;
    // Defaults
    VSColorPrimaries defaultPrimaries = VSC_PRIMARIES_UNSPECIFIED;
    VSTransferCharacteristics defaultTransfer = VSC_TRANSFER_UNSPECIFIED;
    cmsHPROFILE defaultOutputProfile;
    std::vector<char> defaultOutputProfileData;
    cmsHTRANSFORM defaultTransform; // This one is a copy from the map, don't free it directly
    cmsUInt32Number defaultIntent;
    cmsUInt32Number transformFlag;
    // Format: RGB24, RGB48, RGBS (slow)
    cmsUInt32Number lcmsInputDataType;
    cmsUInt32Number lcmsOutputDataType;
    p2p_packing p2pType = p2p_packing_max;
    // Flag for using props
    bool preferProps;
    // Proofing profile and intent
    cmsHPROFILE proofingProfile = nullptr;
    cmsUInt32Number proofingIntent;
    void clear()
    {
        if (defaultOutputProfile) cmsCloseProfile(defaultOutputProfile);
        defaultOutputProfileData.clear();
        for (auto pair : transformMap)
        {
            if (pair.second) cmsDeleteTransform(pair.second);
        }
        if (proofingProfile) cmsCloseProfile(proofingProfile);
    }
};

static cmsHTRANSFORM getTransform(const inputICCData &ind, icccData *d)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    auto found = d->transformMap.find(ind);
    if (found == d->transformMap.end())
    {
        cmsHTRANSFORM transform;
        if (d->proofingProfile)
        {
            transform = cmsCreateProofingTransform(ind.profile, d->lcmsInputDataType, d->defaultOutputProfile, d->lcmsOutputDataType, d->proofingProfile, d->defaultIntent, d->proofingIntent, d->transformFlag);
        }
        else
        {
            transform = cmsCreateTransform(ind.profile, d->lcmsInputDataType, d->defaultOutputProfile, d->lcmsOutputDataType, ind.intent, d->transformFlag);
        }
        if (transform)
        {
            d->transformMap[ind] = transform;
            return transform;
        }
        else return nullptr;
    }
    else return found->second;
}

struct PresetProfile
{
    cmsHPROFILE profile = nullptr;
    VSColorPrimaries primaries = VSC_PRIMARIES_UNSPECIFIED;
    VSTransferCharacteristics transfer = VSC_TRANSFER_UNSPECIFIED;
};

static PresetProfile createPresetProfile(const char *name)
{
    PresetProfile pp;
    if (strcmp(name, "srgb") == 0 || strcmp(name, "sRGB") == 0)
    {
        pp.primaries = VSC_PRIMARIES_BT709;
        pp.transfer = VSC_TRANSFER_IEC_61966_2_1;
        pp.profile = cmsCreate_sRGBProfile();
    }
    else if (strcmp(name, "709") == 0)
    {
        pp.primaries = VSC_PRIMARIES_BT709;
        pp.transfer = VSC_TRANSFER_BT709;
        cmsCIExyY wp = csp_709.white();
        cmsCIExyYTRIPLE prim = csp_709.prim();
        cmsFloat64Number params[5] = {1.0 / 0.45, 1.0 / REC709_ALPHA, 1.0 - 1.0 / REC709_ALPHA, 1.0 / 4.5, REC709_BETA * 4.5};
        cmsToneCurve *curve = cmsBuildParametricToneCurve(nullptr, 4, params);
        cmsToneCurve *curves[3] = {curve, curve, curve};
        pp.profile = cmsCreateRGBProfile(&wp, &prim, curves);
        cmsFreeToneCurve(curve);
    }
    else if (strcmp(name, "170m") == 0 || strcmp(name, "170M") == 0 || strcmp(name, "601-525") == 0)
    {
        pp.primaries = VSC_PRIMARIES_ST170_M;
        pp.transfer = VSC_TRANSFER_BT601;
        cmsCIExyY wp = csp_601_525.white();
        cmsCIExyYTRIPLE prim = csp_601_525.prim();
        cmsFloat64Number params[5] = {1.0 / 0.45, 1.0 / REC709_ALPHA, 1.0 - 1.0 / REC709_ALPHA, 1.0 / 4.5, REC709_BETA * 4.5};
        cmsToneCurve *curve = cmsBuildParametricToneCurve(nullptr, 4, params);
        cmsToneCurve *curves[3] = {curve, curve, curve};
        pp.profile = cmsCreateRGBProfile(&wp, &prim, curves);
        cmsFreeToneCurve(curve);
    }
    else if (strcmp(name, "2020") == 0)
    {
        pp.primaries = VSC_PRIMARIES_BT2020;
        pp.transfer = VSC_TRANSFER_BT2020_10;
        cmsCIExyY wp = csp_2020.white();
        cmsCIExyYTRIPLE prim = csp_2020.prim();
        cmsFloat64Number params[5] = {1.0 / 0.45, 1.0 / REC709_ALPHA, 1.0 - 1.0 / REC709_ALPHA, 1.0 / 4.5, REC709_BETA * 4.5};
        cmsToneCurve *curve = cmsBuildParametricToneCurve(nullptr, 4, params);
        cmsToneCurve *curves[3] = {curve, curve, curve};
        pp.profile = cmsCreateRGBProfile(&wp, &prim, curves);
        cmsFreeToneCurve(curve);
    }
    else if (strcmp(name, "xyz") == 0 || strcmp(name, "XYZ") == 0)
    {
        pp.primaries = VSC_PRIMARIES_ST428;
        pp.transfer = VSC_TRANSFER_LINEAR;
        cmsCIExyYTRIPLE prim = {
            {1.0, 0.0, 1.0},
            {0.0, 1.0, 1.0},
            {0.0, 0.0, 1.0},
        };
        cmsFloat64Number paramsX[3] = {1.0, 1.0 / 0.964212, 0.0};
        cmsFloat64Number paramsZ[3] = {1.0, 1.0 / 0.825188, 0.0};
        cmsToneCurve *curveX = cmsBuildParametricToneCurve(nullptr, 2, paramsX);
        cmsToneCurve *curveY = cmsBuildGamma(nullptr, 1.0);
        cmsToneCurve *curveZ = cmsBuildParametricToneCurve(nullptr, 2, paramsZ);
        cmsToneCurve *curves[3] = {curveX, curveY, curveZ};
        pp.profile = cmsCreateRGBProfile(cmsD50_xyY(), &prim, curves);
        cmsFreeToneCurve(curveX);
        cmsFreeToneCurve(curveY);
        cmsFreeToneCurve(curveZ);
        cmsSetHeaderRenderingIntent(pp.profile, INTENT_RELATIVE_COLORIMETRIC);
        cmsSetPCS(pp.profile, cmsSigXYZData);
        cmsSetHeaderAttributes(pp.profile, cmsTransparency);
    }
    return pp;
}

static const VSFrame *VS_CC icccGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = reinterpret_cast<icccData *>(instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrame *frame = vsapi->getFrameFilter(n, d->node, frameCtx);
        int width = vsapi->getFrameWidth(frame, 0);
        int height = vsapi->getFrameHeight(frame, 0);
        int stride = vsapi->getStride(frame, 0);

        VSFrame *dstFrame = vsapi->newVideoFrame(&d->vi.format, width, height, frame, core);
        int dstStride = vsapi->getStride(dstFrame, 0);
        VSMap *map = vsapi->getFramePropertiesRW(dstFrame);

        // Create or find transform
        cmsHTRANSFORM transform = d->defaultTransform;
        if (d->preferProps)
        {
            int err;
            int iccLength = vsapi->mapGetDataSize(map, "ICCProfile", 0, &err);
            if (!err && iccLength > 0)
            {
                const char *iccData = vsapi->mapGetData(map, "ICCProfile", 0, &err);
                // Create profile
                cmsHPROFILE inp = cmsOpenProfileFromMem(iccData, iccLength);
                if (inp)
                {
                    // Sanity checks
                    if ((cmsGetDeviceClass(inp) != cmsSigDisplayClass) && (cmsGetDeviceClass(inp) != cmsSigInputClass))
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dstFrame);
                        vsapi->setFilterError("iccc: The device class of the embedded ICC profile is not supported.", frameCtx);
                        return nullptr;
                    }
                    if (cmsGetColorSpace(inp) != cmsSigRgbData)
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dstFrame);
                        vsapi->setFilterError("iccc: The colorspace of the embedded ICC profile is not supported.", frameCtx);
                        return nullptr;
                    }
                    inputICCData ind(inp, cmsGetHeaderRenderingIntent(inp));
                    transform = getTransform(ind, d);
                    cmsCloseProfile(inp);
                    if (!transform)
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dstFrame);
                        vsapi->setFilterError("iccc: Failed to create transform from embedded ICC profile.", frameCtx);
                        return nullptr;
                    }
                }
                else
                {
                    vsapi->freeFrame(frame);
                    vsapi->freeFrame(dstFrame);
                    vsapi->setFilterError("iccc: Unable to read embedded ICC profile. Corrupted?", frameCtx);
                    return nullptr;
                }
            }
        }

        if (!transform)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dstFrame);
            vsapi->setFilterError("iccc: Failed to construct transform. This may be caused by insufficient ICC profile info provided.", frameCtx);
            return nullptr;
        }

        uint8_t *buffer = reinterpret_cast<uint8_t *>(vsh::vsh_aligned_malloc(stride * 1 * 3, 32));
        if (!buffer)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dstFrame);
            vsapi->setFilterError("iccc: Out of memory when constructing transform.", frameCtx);
            return nullptr;
        }

        const uint8_t *srcFrame0 = vsapi->getReadPtr(frame, 0);
        const uint8_t *srcFrame1 = vsapi->getReadPtr(frame, 1);
        const uint8_t *srcFrame2 = vsapi->getReadPtr(frame, 2);
        uint8_t *dstFrame0 = vsapi->getWritePtr(dstFrame, 0);
        uint8_t *dstFrame1 = vsapi->getWritePtr(dstFrame, 1);
        uint8_t *dstFrame2 = vsapi->getWritePtr(dstFrame, 2);

        bool usePacking = d->p2pType != p2p_packing_max;
        if (usePacking)
        {
            p2p_buffer_param p2p_src = {};
            p2p_src.width = width;
            p2p_src.height = 1;
            p2p_src.dst[0] = buffer;
            p2p_src.dst_stride[0] = stride * 3;
            p2p_src.packing = d->p2pType;

            p2p_buffer_param p2p_dst = {};
            p2p_dst.width = width;
            p2p_dst.height = 1;
            p2p_dst.src[0] = buffer;
            p2p_dst.src_stride[0] = stride * 3;
            p2p_dst.packing = d->p2pType;

            const uint8_t *srcFrames[3] = {srcFrame0, srcFrame1, srcFrame2};
            uint8_t *dstFrames[3] = {dstFrame0, dstFrame1, dstFrame2};

            for (int h = 0; h < height; ++h)
            {
                for (int plane = 0; plane < 3; ++plane)
                {
                    const uint8_t *start = srcFrames[plane];
                    p2p_src.src[plane] = &start[h * stride];
                    p2p_src.src_stride[plane] = vsapi->getStride(frame, plane);
                }
                p2p_pack_frame(&p2p_src, 0);

                cmsDoTransformLineStride(transform, buffer, buffer, width, 1, stride * 3, stride * 3, 0, 0);

                for (int plane = 0; plane < 3; ++plane)
                {
                    uint8_t *start = dstFrames[plane];
                    p2p_dst.dst[plane] = &start[h * stride];
                    p2p_dst.dst_stride[plane] = vsapi->getStride(dstFrame, plane);
                }
                p2p_unpack_frame(&p2p_dst, 0);
            }
        }
        else
        {
            int widthBit = width * vsapi->getVideoFrameFormat(frame)->bytesPerSample;
            int dstWidthBit = width * d->vi.format.bytesPerSample;

            for (int h = 0; h < height; ++h)
            {
                vsh::bitblt(buffer, stride, &srcFrame0[h * stride], stride, widthBit, 1);
                vsh::bitblt(&buffer[stride * 1], stride, &srcFrame1[h * stride], stride, widthBit, 1);
                vsh::bitblt(&buffer[2 * stride * 1], stride, &srcFrame2[h * stride], stride, widthBit, 1);

                cmsDoTransformLineStride(transform, buffer, buffer, width, 1, stride, dstStride, stride * 1, dstStride * 1);

                vsh::bitblt(&dstFrame0[h * stride], dstStride, buffer, dstStride, dstWidthBit, 1);
                vsh::bitblt(&dstFrame1[h * stride], dstStride, &buffer[dstStride * 1], dstStride, dstWidthBit, 1);
                vsh::bitblt(&dstFrame2[h * stride], dstStride, &buffer[2 * dstStride * 1], dstStride, dstWidthBit, 1);
            }
        }

        vsh::vsh_aligned_free(buffer);
        vsapi->freeFrame(frame);

        // Set frame props
        vsapi->mapSetInt(map, "_Primaries", d->defaultPrimaries, maReplace);
        vsapi->mapSetInt(map, "_Transfer", d->defaultTransfer, maReplace);
        if (d->defaultOutputProfileData.size() > 0)
            vsapi->mapSetData(map, "ICCProfile", d->defaultOutputProfileData.data(), d->defaultOutputProfileData.size(), dtBinary, maReplace);
        else
            vsapi->mapDeleteKey(map, "ICCProfile");

        return dstFrame;
    }
    return nullptr;
}

static void VS_CC icccFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = reinterpret_cast<icccData *>(instanceData);
    vsapi->freeNode(d->node);
    d->clear();
    delete d;
}

void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    std::unique_ptr<icccData> d(new icccData());

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);
    uint32_t srcFormat = vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core);
    d->vi = *vi;

    if (srcFormat == pfRGB24)
    {
        d->lcmsInputDataType = TYPE_BGR_8;
        d->lcmsOutputDataType = d->lcmsInputDataType;
        d->p2pType = p2p_rgb24;
    }
    else if (srcFormat == pfRGB48)
    {
        d->lcmsInputDataType = TYPE_BGR_16;
        d->lcmsOutputDataType = d->lcmsInputDataType;
        d->p2pType = p2p_rgb48;
    }
    else if (srcFormat == pfRGBS)
    {
        d->lcmsInputDataType = TYPE_RGB_FLT_PLANAR;
        d->lcmsOutputDataType = d->lcmsInputDataType;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24, RGB48 and RGBS input formats are well supported.");
        return;
    }

    int err;

    d->preferProps = vsapi->mapGetInt(in, "prefer_props", 0, &err) || err;

    cmsHPROFILE inputProfile = nullptr;
    const char *srcProfilePath = vsapi->mapGetData(in, "input_icc", 0, &err);
    if (err || !srcProfilePath)
    {
        inputProfile = nullptr;
    }
    else if (!(inputProfile = cmsOpenProfileFromFile(srcProfilePath, "r")))
    {
        PresetProfile pp = createPresetProfile(srcProfilePath);
        if (pp.profile)
        {
            inputProfile = pp.profile;
        }
        else
        {
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Input profile seems invalid.");
            return;
        }
    }
    if (inputProfile && (cmsGetDeviceClass(inputProfile) != cmsSigDisplayClass) && (cmsGetDeviceClass(inputProfile) != cmsSigInputClass))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input profile must have 'display' ('mntr') or 'input' ('scnr') device class.");
        return;
    }
    if (inputProfile && cmsGetColorSpace(inputProfile) != cmsSigRgbData)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input profile must be for RGB colorspace.");
        return;
    }
    if (!d->preferProps && !inputProfile)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input profile must be provided unless frame properties are preferred.");
        return;
    }

    const char *dstProfile = vsapi->mapGetData(in, "display_icc", 0, &err);
    if (err || !dstProfile)
    {
        d->defaultOutputProfile = getSystemProfile();
        if (!d->defaultOutputProfile)
        {
            inputProfile && cmsCloseProfile(inputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Auto detection of display ICC failed. You should specify the output profile from file instead.");
            return;
        }
    }
    else if (!(d->defaultOutputProfile = cmsOpenProfileFromFile(dstProfile, "r")))
    {
        PresetProfile pp = createPresetProfile(dstProfile);
        if (pp.profile)
        {
            d->defaultOutputProfile = pp.profile;
            d->defaultPrimaries = pp.primaries;
            d->defaultTransfer = pp.transfer;
        }
        else
        {
            inputProfile && cmsCloseProfile(inputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Display (output) profile seems invalid.");
            return;
        }
    }
    if ((cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigDisplayClass) && (cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigOutputClass))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Display (output) profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
        return;
    }
    if (cmsGetColorSpace(d->defaultOutputProfile) != cmsSigRgbData)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Display profile must be for RGB colorspace.");
        return;
    }

    cmsUInt32Number outputProfileSize = 0;
    cmsSaveProfileToMem(d->defaultOutputProfile, nullptr, &outputProfileSize);
    if (outputProfileSize > 0)
    {
        d->defaultOutputProfileData.resize(outputProfileSize);
        if(!cmsSaveProfileToMem(d->defaultOutputProfile, d->defaultOutputProfileData.data(), &outputProfileSize))
        {
            d->defaultOutputProfileData.clear();
        }
    }
    if (outputProfileSize <= 0 || d->defaultOutputProfileData.size() == 0)
    {
        vsapi->logMessage(mtWarning, "iccc: Won't set ICC frame props.", core);
    }

    const char *intentString = vsapi->mapGetData(in, "intent", 0, &err);
    if (err || !intentString)
    {
        if (inputProfile) d->defaultIntent = cmsGetHeaderRenderingIntent(inputProfile);
    }
    else if (strcmp(intentString, "perceptual") == 0) d->defaultIntent = INTENT_PERCEPTUAL;
    else if (strcmp(intentString, "relative") == 0) d->defaultIntent = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(intentString, "saturation") == 0) d->defaultIntent = INTENT_SATURATION;
    else if (strcmp(intentString, "absolute") == 0) d->defaultIntent = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC intent is not supported.");
        return;
    }

    d->transformFlag = cmsFLAGS_NONEGATIVES;

    const char *proofingProfilePath = vsapi->mapGetData(in, "proofing_icc", 0, &err);
    if (proofingProfilePath)
    {
        if (!(d->proofingProfile = cmsOpenProfileFromFile(proofingProfilePath, "r")))
        {
            PresetProfile pp = createPresetProfile(proofingProfilePath);
            if (pp.profile)
                d->proofingProfile = pp.profile;
            else
            {
                cmsCloseProfile(d->defaultOutputProfile);
                inputProfile && cmsCloseProfile(inputProfile);
                vsapi->freeNode(d->node);
                vsapi->mapSetError(out, "iccc: Proofing profile seems invalid.");
                return;
            }
        }
        if (cmsGetDeviceClass(d->proofingProfile) != cmsSigDisplayClass || cmsGetDeviceClass(d->proofingProfile) != cmsSigOutputClass)
        {
            cmsCloseProfile(d->defaultOutputProfile);
            inputProfile && cmsCloseProfile(inputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Proofing profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
            return;
        }
        d->transformFlag |= cmsFLAGS_SOFTPROOFING;
    }

    const char *proofingIntentString = nullptr;
    if (d->proofingProfile)
    {
        proofingIntentString = vsapi->mapGetData(in, "proofing_intent", 0, &err);
        if (err || !proofingIntentString)
        {
            d->proofingIntent = cmsGetHeaderRenderingIntent(d->proofingProfile);
        }
        else if (strcmp(proofingIntentString, "perceptual") == 0) d->proofingIntent = INTENT_PERCEPTUAL;
        else if (strcmp(proofingIntentString, "relative") == 0) d->proofingIntent = INTENT_RELATIVE_COLORIMETRIC;
        else if (strcmp(proofingIntentString, "saturation") == 0) d->proofingIntent = INTENT_SATURATION;
        else if (strcmp(proofingIntentString, "absolute") == 0) d->proofingIntent = INTENT_ABSOLUTE_COLORIMETRIC;
        else
        {
            cmsCloseProfile(d->defaultOutputProfile);
            inputProfile && cmsCloseProfile(inputProfile);
            cmsCloseProfile(d->proofingProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Input proofing intent is not supported.");
            return;
        }
    }

    bool gamutWarning = !!vsapi->mapGetInt(in, "gamut_warning", 0, &err);
    if (gamutWarning)
    {
        d->transformFlag |= cmsFLAGS_GAMUTCHECK;
        assert(cmsMAXCHANNELS > 3);
        cmsUInt16Number gamutWarningColor[cmsMAXCHANNELS] = {65535, 0, 65535};
        if (vsapi->mapNumElements(in, "gamut_warning_color") == 3)
        {
            for (int i = 0; i < 3; ++i)
            {
                gamutWarningColor[i] = static_cast<cmsUInt16Number>(vsapi->mapGetInt(in, "gamut_warning_color", i, nullptr));
            }
        }
        cmsSetAlarmCodes(gamutWarningColor);
    }

    bool blackPointCompensation = !!vsapi->mapGetInt(in, "black_point_compensation", 0, &err);
    if (blackPointCompensation) d->transformFlag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clutSize = vsh::int64ToIntS(vsapi->mapGetInt(in, "clut_size", 0, &err));
    if (err) clutSize = 1;
    if (clutSize == -1) clutSize = 17; // default for cmsFLAGS_LOWRESPRECALC
    else if (clutSize == 0) clutSize = 33; // default
    else if (clutSize == 1) clutSize = 49; // default for cmsFLAGS_HIGHRESPRECALC
    else if ((clutSize < -1) || (clutSize > 255))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        cmsCloseProfile(d->proofingProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input clut size seems invalid.");
        return;
    }
    d->transformFlag |= cmsFLAGS_GRIDPOINTS(clutSize);

    // Create a default transform. If it's null, leave error report to the runtime.
    if (inputProfile)
    {
        if (d->proofingProfile)
        {
            d->defaultTransform = cmsCreateProofingTransform(inputProfile, d->lcmsInputDataType, d->defaultOutputProfile, d->lcmsOutputDataType, d->proofingProfile, d->defaultIntent, d->proofingIntent, d->transformFlag);
        }
        else
        {
            d->defaultTransform = cmsCreateTransform(inputProfile, d->lcmsInputDataType, d->defaultOutputProfile, d->lcmsOutputDataType, d->defaultIntent, d->transformFlag);
        }
        inputICCData ind(inputProfile, d->defaultIntent);
        d->transformMap[ind] = d->defaultTransform;
        cmsCloseProfile(inputProfile);
    }
    else
    {
        d->defaultTransform = nullptr;
    }

    std::vector<VSFilterDependency> depReq =
    {
        {d->node, rpStrictSpatial}
    };

    vsapi->createVideoFilter(out, "Convert", &d->vi, icccGetFrame, icccFree, fmParallel, depReq.data(), depReq.size(), d.get(), core);
    d.release();
}

// Playback won't respect frame properties for variable transforms. That's evil.
void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    std::unique_ptr<icccData> d(new icccData());

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    uint32_t srcFormat = vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core);

    if (srcFormat == pfRGB24)
    {
        d->lcmsInputDataType = TYPE_BGR_8;
        d->lcmsOutputDataType = d->lcmsInputDataType;
        d->p2pType = p2p_rgb24;
    }
    else if (srcFormat == pfRGB48)
    {
        d->lcmsInputDataType = TYPE_BGR_16;
        d->lcmsOutputDataType = d->lcmsInputDataType;
        d->p2pType = p2p_rgb48;
    }
    else if (srcFormat == pfRGBS)
    {
        d->lcmsInputDataType = TYPE_RGB_FLT_PLANAR;
        d->lcmsOutputDataType = d->lcmsInputDataType;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24 and RGB48 input formats are well supported.");
        return;
    }

    d->vi = *vi;

    int err;

    bool inverse = !!vsapi->mapGetInt(in, "inverse", 0, &err);
    if (err)
        inverse = false;

    const char *dstProfile = vsapi->mapGetData(in, "display_icc", 0, &err);
    if (err || !dstProfile)
    {
        d->defaultOutputProfile = getSystemProfile();
        if (!d->defaultOutputProfile)
        {
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Auto detection of display ICC failed. You should specify from file instead.");
            return;
        }
    }
    else if (!(d->defaultOutputProfile = cmsOpenProfileFromFile(dstProfile, "r")))
    {
        PresetProfile pp = createPresetProfile(dstProfile);
        if (pp.profile)
        {
            d->defaultOutputProfile = pp.profile;
            if (!inverse)
                d->defaultPrimaries = pp.primaries;
        }
        else
        {
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Input display profile seems invalid.");
            return;
        }
    }
    if (cmsGetColorSpace(d->defaultOutputProfile) != cmsSigRgbData)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must be for RGB colorspace.");
        return;
    }

    cmsProfileClassSignature deviceClass = cmsGetDeviceClass(d->defaultOutputProfile);
    if (deviceClass != cmsSigDisplayClass)
    {
        if (inverse && deviceClass != cmsSigInputClass)
        {
            cmsCloseProfile(d->defaultOutputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Display profile must have 'display' ('mntr') or 'input' ('scnr') device class in inverse mode.");
            return;
        }
        else if (!inverse && deviceClass != cmsSigOutputClass)
        {
            cmsCloseProfile(d->defaultOutputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Display profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
            return;
        }
    }

    double gamma = vsapi->mapGetFloat(in, "gamma", 0, &err);
    if (err)
    {
        gamma = -1.0;
    }
    else if ((gamma < 0.01) || (gamma > 100.0))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input gamma value is only allowed between 0.01 and 100.0.");
        return;
    }

    double contrast = vsapi->mapGetFloat(in, "contrast", 0, &err);
    if (err)
        contrast = 0.0;
    else if (contrast < 0.0)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input contrast value must be positive.");
        return;
    }

    cmsHPROFILE inputProfile = nullptr;
    const char *srcProfilePath = vsapi->mapGetData(in, "csp", 0, &err);
    if (err || strcmp(srcProfilePath, "709") == 0)
    {
        inputProfile = getPlaybackProfile(csp_709, gamma, contrast, d->defaultOutputProfile);
        if (inverse)
        {
            d->defaultPrimaries = VSC_PRIMARIES_BT709;
            d->defaultTransfer = VSC_TRANSFER_BT709;
        }
    }
    else if ((strcmp(srcProfilePath, "170m") == 0) || (strcmp(srcProfilePath, "170M") == 0) || (strcmp(srcProfilePath, "601-525") == 0))
    {
        inputProfile = getPlaybackProfile(csp_601_525, gamma, contrast, d->defaultOutputProfile);
        if (inverse)
        {
            d->defaultPrimaries = VSC_PRIMARIES_ST170_M;
            d->defaultTransfer = VSC_TRANSFER_BT601;
        }
    }
    else if (strcmp(srcProfilePath, "2020") == 0)
    {
        inputProfile = getPlaybackProfile(csp_2020, gamma, contrast, d->defaultOutputProfile);
        if (inverse)
        {
            d->defaultPrimaries = VSC_PRIMARIES_BT2020;
            d->defaultTransfer = VSC_TRANSFER_BT2020_10;
        }
    }
    else
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input color space is not yet supported.");
        return;
    }
    if (!inputProfile)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Failed to generate ICC for playback.");
        return;
    }

    const char *intentString = vsapi->mapGetData(in, "intent", 0, &err);
    if (err) // Default of mpv
    {
        d->defaultIntent = INTENT_RELATIVE_COLORIMETRIC;
    }
    else if (strcmp(intentString, "perceptual") == 0) d->defaultIntent = INTENT_PERCEPTUAL;
    else if (strcmp(intentString, "relative") == 0) d->defaultIntent = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(intentString, "saturation") == 0) d->defaultIntent = INTENT_SATURATION;
    else if (strcmp(intentString, "absolute") == 0) d->defaultIntent = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(d->defaultOutputProfile);
        cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC intent is not supported.");
        return;
    }

    d->transformFlag = cmsFLAGS_NONEGATIVES;

    bool blackPointCompensation = vsapi->mapGetInt(in, "black_point_compensation", 0, &err);
    if (err)
        blackPointCompensation = true;
    if (blackPointCompensation)
        d->transformFlag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clutSize = vsh::int64ToIntS(vsapi->mapGetInt(in, "clut_size", 0, &err));
    if (err) clutSize = 1;
    if (clutSize == -1) clutSize = 17; // default for cmsFLAGS_LOWRESPRECALC
    else if (clutSize == 0) clutSize = 33; // default
    else if (clutSize == 1) clutSize = 49; // default for cmsFLAGS_HIGHRESPRECALC
    else if ((clutSize < -1) || (clutSize > 255))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input clut size seems invalid.");
        return;
    }
    d->transformFlag |= cmsFLAGS_GRIDPOINTS(clutSize);

    if (inverse)
    {
        d->defaultTransform = cmsCreateTransform(d->defaultOutputProfile, d->lcmsInputDataType, inputProfile, d->lcmsOutputDataType, d->defaultIntent, d->transformFlag);

        cmsUInt32Number outputProfileSize = 0;
        cmsSaveProfileToMem(inputProfile, nullptr, &outputProfileSize);
        if (outputProfileSize > 0)
        {
            d->defaultOutputProfileData.resize(outputProfileSize);
            if(!cmsSaveProfileToMem(inputProfile, d->defaultOutputProfileData.data(), &outputProfileSize))
            {
                d->defaultOutputProfileData.clear();
            }
        }
        if (outputProfileSize <= 0 || d->defaultOutputProfileData.size() == 0)
        {
            vsapi->logMessage(mtWarning, "iccc: Won't set ICC frame props.", core);
        }
    }
    else
    {
        d->defaultTransform = cmsCreateTransform(inputProfile, d->lcmsInputDataType, d->defaultOutputProfile, d->lcmsOutputDataType, d->defaultIntent, d->transformFlag);
        cmsUInt32Number outputProfileSize = 0;
        cmsSaveProfileToMem(d->defaultOutputProfile, nullptr, &outputProfileSize);
        if (outputProfileSize > 0)
        {
            d->defaultOutputProfileData.resize(outputProfileSize);
            if(!cmsSaveProfileToMem(d->defaultOutputProfile, d->defaultOutputProfileData.data(), &outputProfileSize))
            {
                d->defaultOutputProfileData.clear();
            }
        }
        if (outputProfileSize <= 0 || d->defaultOutputProfileData.size() == 0)
        {
            vsapi->logMessage(mtWarning, "iccc: Won't set ICC frame props.", core);
        }
    }
    if (!d->defaultTransform)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Failed to create transform for playback.");
        return;
    }
    // This is not necessary but we are going to free defaultTransform there
    inputICCData ind(inputProfile, d->defaultIntent);
    d->transformMap[ind] = d->defaultTransform;
    cmsCloseProfile(inputProfile);

    d->preferProps = false;

    std::vector<VSFilterDependency> depReq =
    {
        {d->node, rpStrictSpatial}
    };

    vsapi->createVideoFilter(out, "Playback", &d->vi, icccGetFrame, icccFree, fmParallel, depReq.data(), depReq.size(), d.get(), core);
    d.release();
}

struct tagData
{
    VSNode *node;
    std::vector<char> profileData;
    VSColorPrimaries primaries = VSC_PRIMARIES_UNSPECIFIED;
    VSTransferCharacteristics transfer = VSC_TRANSFER_UNSPECIFIED;
};

static const VSFrame *VS_CC tagGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    tagData *d = reinterpret_cast<tagData *>(instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrame *frame = vsapi->getFrameFilter(n, d->node, frameCtx);

        VSFrame *dstFrame = vsapi->copyFrame(frame, core);
        VSMap *map = vsapi->getFramePropertiesRW(dstFrame);
        vsapi->freeFrame(frame);

        vsapi->mapSetData(map, "ICCProfile", d->profileData.data(), d->profileData.size(), dtBinary, maReplace);
        vsapi->mapSetInt(map, "_Primaries", d->primaries, maReplace);
        vsapi->mapSetInt(map, "_Transfer", d->transfer, maReplace);
        return dstFrame;
    }
    return nullptr;
}

static void VS_CC tagFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    tagData *d = reinterpret_cast<tagData *>(instanceData);
    vsapi->freeNode(d->node);
    d->profileData.clear();
    delete d;
}

void VS_CC tagCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    std::unique_ptr<tagData> d(new tagData());

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);

    int err;
    const char *iccFile = vsapi->mapGetData(in, "icc", 0, &err);
    if (err)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC must be provided.");
        return;
    }
    cmsHPROFILE profile = nullptr;
    if (!(profile = cmsOpenProfileFromFile(iccFile, "r")))
    {
        PresetProfile pp = createPresetProfile(iccFile);
        if (pp.profile)
        {
            profile = pp.profile;
            d->primaries = pp.primaries;
            d->transfer = pp.transfer;
        }
        else
        {
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Input ICC seems invalid.");
            return;
        }
    }

    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    if (size <= 0)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC has no content. Corrupted?");
        return;
    }
    d->profileData.resize(size);
    if (!cmsSaveProfileToMem(profile, d->profileData.data(), &size))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Error occured when loading input ICC.");
        return;
    }
    cmsCloseProfile(profile);

    std::vector<VSFilterDependency> depReq =
    {
        {d->node, rpStrictSpatial}
    };

    vsapi->createVideoFilter(out, "Tag", vsapi->getVideoInfo(d->node), tagGetFrame, tagFree, fmParallel, depReq.data(), depReq.size(), d.get(), core);
    d.release();
}
