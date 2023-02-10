#include "common.hpp"
#include "libp2p/p2p_api.h"
#include <unordered_map>
#include <mutex>
#include <memory>

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
    cmsHPROFILE defaultOutputProfile;
    cmsHTRANSFORM defaultTransform; // This one is a copy from the map, don't free it directly
    cmsUInt32Number defaultIntent;
    cmsUInt32Number transformFlag;
    // Format: now either RGB24 or RGB48
    cmsUInt32Number lcmsDataType;
    p2p_packing p2pType = p2p_packing_max;
    // Flag for using props
    bool preferProps;
    // Proofing profile and intent
    cmsHPROFILE proofingProfile = nullptr;
    cmsUInt32Number proofingIntent;
    void clear()
    {
        if (defaultOutputProfile) cmsCloseProfile(defaultOutputProfile);
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
            transform = cmsCreateProofingTransform(ind.profile, d->lcmsDataType, d->defaultOutputProfile, d->lcmsDataType, d->proofingProfile, d->defaultIntent, d->proofingIntent, d->transformFlag);
        }
        else
        {
            transform = cmsCreateTransform(ind.profile, d->lcmsDataType, d->defaultOutputProfile, d->lcmsDataType, ind.intent, d->transformFlag);
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

        // Create or find transform
        cmsHTRANSFORM transform = d->defaultTransform;
        if (d->preferProps)
        {
            VSMap *map = vsapi->getFramePropertiesRW(dstFrame);
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
                vsapi->mapDeleteKey(map, "ICCProfile");
            }
        }

        if (!transform)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dstFrame);
            vsapi->setFilterError("iccc: Failed to construct transform. This may be caused by insufficient ICC profile info provided.", frameCtx);
            return nullptr;
        }

        void *packed = vsh::vsh_aligned_malloc(stride * height * 3, 32);
        if (!packed)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dstFrame);
            vsapi->setFilterError("iccc: Out of memory when constructing transform.", frameCtx);
            return nullptr;
        }

        // Pack
        p2p_buffer_param p2p_src = {};
        p2p_src.width = width;
        p2p_src.height = height;
        for (int plane = 0; plane < 3; ++plane)
        {
            p2p_src.src[plane] = vsapi->getReadPtr(frame, plane);
            p2p_src.src_stride[plane] = vsapi->getStride(frame, plane);
        }
        p2p_src.dst[0] = packed;
        p2p_src.dst_stride[0] = stride * 3;
        p2p_src.packing = d->p2pType;
        p2p_pack_frame(&p2p_src, 0);

        // Transform
        cmsDoTransformLineStride(transform, packed, packed, width, height, stride * 3, stride * 3, 0, 0);

        // Unpack
        p2p_buffer_param p2p_dst = {};
        p2p_dst.width = width;
        p2p_dst.height = height;
        for (int plane = 0; plane < 3; ++plane)
        {
            p2p_dst.dst[plane] = vsapi->getWritePtr(dstFrame, plane);
            p2p_dst.dst_stride[plane] = vsapi->getStride(dstFrame, plane);
        }
        p2p_dst.src[0] = packed;
        p2p_dst.src_stride[0] = stride * 3;
        p2p_dst.packing = d->p2pType;
        p2p_unpack_frame(&p2p_dst, 0);

        vsh::vsh_aligned_free(packed);
        vsapi->freeFrame(frame);
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

    if (srcFormat == pfRGB24)
    {
        d->lcmsDataType = TYPE_BGR_8;
        d->p2pType = p2p_rgb24;
    }
    else if (srcFormat == pfRGB48)
    {
        d->lcmsDataType = TYPE_BGR_16;
        d->p2pType = p2p_rgb48;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24 and RGB48 input formats are well supported.");
        return;
    }

    d->vi = *vi;

    int err;

    d->preferProps = vsapi->mapGetInt(in, "prefer_props", 0, &err) || err;

    cmsHPROFILE inputProfile = nullptr;
    const char *srcProfilePath = vsapi->mapGetData(in, "input_icc", 0, &err);
    if (err || !srcProfilePath)
    {
        srcProfilePath = vsapi->mapGetData(in, "simulation_icc", 0, &err);
    }
    if (err || !srcProfilePath)
    {
        inputProfile = nullptr;
    }
    else if (!(inputProfile = cmsOpenProfileFromFile(srcProfilePath, "r")))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input profile seems invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(inputProfile) != cmsSigDisplayClass) && (cmsGetDeviceClass(inputProfile) != cmsSigInputClass))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input profile must have 'display' ('mntr') or 'input' ('scnr') device class.");
        return;
    }
    else if (cmsGetColorSpace(inputProfile) != cmsSigRgbData)
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
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Display (output) profile seems invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigDisplayClass) && (cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigOutputClass))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Display (output) profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
        return;
    }
    else if (cmsGetColorSpace(d->defaultOutputProfile) != cmsSigRgbData)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        inputProfile && cmsCloseProfile(inputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Display profile must be for RGB colorspace.");
        return;
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
            cmsCloseProfile(d->defaultOutputProfile);
            inputProfile && cmsCloseProfile(inputProfile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Proofing profile seems invalid.");
            return;
        }
        else if (cmsGetDeviceClass(d->proofingProfile) != cmsSigDisplayClass || cmsGetDeviceClass(d->proofingProfile) != cmsSigOutputClass)
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
            d->defaultTransform = cmsCreateProofingTransform(inputProfile, d->lcmsDataType, d->defaultOutputProfile, d->lcmsDataType, d->proofingProfile, d->defaultIntent, d->proofingIntent, d->transformFlag);
        }
        else
        {
            d->defaultTransform = cmsCreateTransform(inputProfile, d->lcmsDataType, d->defaultOutputProfile, d->lcmsDataType, d->defaultIntent, d->transformFlag);
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
        d->lcmsDataType = TYPE_BGR_8;
        d->p2pType = p2p_rgb24;
    }
    else if (srcFormat == pfRGB48)
    {
        d->lcmsDataType = TYPE_BGR_16;
        d->p2pType = p2p_rgb48;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24 and RGB48 input formats are well supported.");
        return;
    }

    d->vi = *vi;

    int err;

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
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile seems invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigDisplayClass) && (cmsGetDeviceClass(d->defaultOutputProfile) != cmsSigOutputClass))
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
        return;
    }
    else if (cmsGetColorSpace(d->defaultOutputProfile) != cmsSigRgbData)
    {
        cmsCloseProfile(d->defaultOutputProfile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must be for RGB colorspace.");
        return;
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

    cmsHPROFILE inputProfile = nullptr;
    bool inputIsSRGB = false;
    const char *srcProfilePath = vsapi->mapGetData(in, "csp", 0, &err);
    if (err || strcmp(srcProfilePath, "709") == 0)
    {
        inputProfile = getPlaybackProfile(csp_709, gamma, d->defaultOutputProfile);
    }
    else if ((strcmp(srcProfilePath, "170m") == 0) || (strcmp(srcProfilePath, "601-525") == 0))
    {
        inputProfile = getPlaybackProfile(csp_601_525, gamma, d->defaultOutputProfile);
    }
    else if (strcmp(srcProfilePath, "2020") == 0)
    {
        inputProfile = getPlaybackProfile(csp_2020, gamma, d->defaultOutputProfile);
    }
    else if (strcmp(srcProfilePath, "srgb") == 0)
    {
        inputProfile = cmsCreate_sRGBProfile();
        inputIsSRGB = true;
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
    {
        blackPointCompensation = !inputIsSRGB;
    }
    if (blackPointCompensation) d->transformFlag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

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

    d->defaultTransform = cmsCreateTransform(inputProfile, d->lcmsDataType, d->defaultOutputProfile, d->lcmsDataType, d->defaultIntent, d->transformFlag);
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
