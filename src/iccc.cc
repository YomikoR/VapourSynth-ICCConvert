#include "common.hpp"
#include "vapoursynth/VapourSynth.h"
#include "vapoursynth/VSHelper.h"
#include "libp2p/p2p_api.h"

inline bool isLittleEndian()
{
    static constexpr uint32_t VAL = 0x00000001;
    return (0xFFFFFFFF & 1) == VAL;
}

struct icccData
{
    VSNodeRef *node = nullptr;
    const VSVideoInfo *vi = nullptr;
    cmsHTRANSFORM transform = nullptr;
};

void VS_CC icccInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = static_cast<icccData *>(*instanceData);
    vsapi->setVideoInfo(d->vi, 1, node);
}

const VSFrameRef *VS_CC icccGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = static_cast<icccData *>(*instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrameRef *frame = vsapi->getFrameFilter(n, d->node, frameCtx);
        int width = d->vi->width;
        int height = d->vi->height;
        VSFrameRef *dst_frame = vsapi->newVideoFrame(d->vi->format, width, height, frame, core);

        int bps = d->vi->format->bytesPerSample;
        int stride = vsapi->getStride(frame, 0);

        void *raw_src = vs_aligned_malloc(width * height * 3 * bps, 32);
        void *raw_dst = vs_aligned_malloc(width * height * 3 * bps, 32);

        // pack
        if (bps == 4)
        {
            const float *src_p0 = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 0));
            const float *src_p1 = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 1));
            const float *src_p2 = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 2));
            float *raw_src_f = reinterpret_cast<float *>(raw_src);
            for (int h = 0; h < height; ++h)
            {
                for (int w = 0; w < width; ++w)
                {
                    raw_src_f[(h * width + w) * 3 + isLittleEndian() ? 2 : 0] = src_p0[h * stride / 4 + w];
                }
                for (int w = 0; w < width; ++w)
                {
                    raw_src_f[(h * width + w) * 3 + 1] = src_p1[h * stride / 4 + w];
                }
                for (int w = 0; w < width; ++w)
                {
                    raw_src_f[(h * width + w) * 3 + isLittleEndian() ? 0 : 2] = src_p2[h * stride / 4 + w];
                }
            }
        }
        else
        {
            p2p_buffer_param p2p_src = {};
            p2p_src.width = width;
            p2p_src.height = height;
            for (int plane = 0; plane < 3; ++plane)
            {
                p2p_src.src[plane] = vsapi->getReadPtr(frame, plane);
                p2p_src.src_stride[plane] = vsapi->getStride(frame, plane);
            }
            p2p_src.dst[0] = raw_src;
            p2p_src.dst_stride[0] = width * 3 * bps;
            p2p_src.packing = (bps == 2) ? p2p_rgb48 : p2p_rgb24;
            p2p_pack_frame(&p2p_src, 0);
        }

        // transform
        cmsDoTransform(d->transform, raw_src, raw_dst, static_cast<cmsUInt32Number>(width * height));

        // unpack
        if (bps == 4)
        {
            float *dst_p0 = reinterpret_cast<float *>(vsapi->getWritePtr(dst_frame, 0));
            float *dst_p1 = reinterpret_cast<float *>(vsapi->getWritePtr(dst_frame, 1));
            float *dst_p2 = reinterpret_cast<float *>(vsapi->getWritePtr(dst_frame, 2));
            float *raw_dst_f = reinterpret_cast<float *>(raw_dst);
            for (int h = 0; h < height; ++h)
            {
                for (int w = 0; w < width; ++w)
                {
                    dst_p0[h * stride / 4 + w] = raw_dst_f[(h * width + w) * 3 + isLittleEndian() ? 2 : 0];
                }
                for (int w = 0; w < width; ++w)
                {
                    dst_p1[h * stride / 4 + w] = raw_dst_f[(h * width + w) * 3 + 1];
                }
                for (int w = 0; w < width; ++w)
                {
                    dst_p2[h * stride / 4 + w] = raw_dst_f[(h * width + w) * 3 + isLittleEndian() ? 0 : 2];
                }
            }
        }
        else
        {
            p2p_buffer_param p2p_dst = {};
            p2p_dst.width = width;
            p2p_dst.height = height;
            for (int plane = 0; plane < 3; ++plane)
            {
                p2p_dst.dst[plane] = vsapi->getWritePtr(dst_frame, plane);
                p2p_dst.dst_stride[plane] = vsapi->getStride(dst_frame, plane);
            }
            p2p_dst.src[0] = raw_dst;
            p2p_dst.src_stride[0] = width * 3 * bps;
            p2p_dst.packing = (bps == 2) ? p2p_rgb48 : p2p_rgb24;
            p2p_unpack_frame(&p2p_dst, 0);
        }

        vs_aligned_free(raw_src);
        vs_aligned_free(raw_dst);
        vsapi->freeFrame(frame);
        return dst_frame;
    }
    return nullptr;
}

void VS_CC icccFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = static_cast<icccData *>(instanceData);
    vsapi->freeNode(d->node);
    if (d->transform) cmsDeleteTransform(d->transform);
    delete d;
}

void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    icccData d;

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi))
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Only constant format input is supported.");
        return;
    }

    cmsUInt32Number lcmsDataType;
    if (d.vi->format->id == pfRGB48) lcmsDataType = TYPE_BGR_16;
    else if (d.vi->format->id == pfRGB24) lcmsDataType = TYPE_BGR_8;
    else if (d.vi->format->id == pfRGBS) lcmsDataType = TYPE_BGR_FLT;
    else
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Currently only RGB24, RGB48 and RGBS input formats are well supported.");
        return;
    }

    int err;

    cmsHPROFILE lcmsProfileSimulation;
    const char *src_profile = vsapi->propGetData(in, "simulation_icc", 0, &err);
    if (err || !(lcmsProfileSimulation = cmsOpenProfileFromFile(src_profile, "r")))
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input destination profile seems to be invalid.");
        return;
    }
    cmsHPROFILE lcmsProfileDisplay;
    const char *dst_profile = vsapi->propGetData(in, "display_icc", 0, &err);
    if (err || !dst_profile)
    {
        lcmsProfileDisplay = get_profile_sys();
        if (!lcmsProfileDisplay)
        {
            cmsCloseProfile(lcmsProfileSimulation);
            vsapi->freeNode(d.node);
            vsapi->setError(out, "iccc: Auto detection of display ICC failed. You should specify from file instead.");
            return;
        }
    }
    else if (!(lcmsProfileDisplay = cmsOpenProfileFromFile(dst_profile, "r")))
    {
        cmsCloseProfile(lcmsProfileSimulation);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input display profile seems to be invalid.");
        return;
    }

    bool soft_proofing = vsapi->propGetInt(in, "soft_proofing", 0, &err);
    if (err)
    {
        soft_proofing = true;
    }

    cmsUInt32Number lcmsIntentSimulation;
    const char *sim_intent = vsapi->propGetData(in, "simulation_intent", 0, &err);
    if (err)
    {
        lcmsIntentSimulation = INTENT_RELATIVE_COLORIMETRIC;
    }
    else if (strcmp(sim_intent, "perceptual") == 0) lcmsIntentSimulation = INTENT_PERCEPTUAL;
    else if (strcmp(sim_intent, "relative") == 0) lcmsIntentSimulation = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(sim_intent, "saturation") == 0) lcmsIntentSimulation = INTENT_SATURATION;
    else if (strcmp(sim_intent, "absolute") == 0) lcmsIntentSimulation = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(lcmsProfileSimulation);
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input ICC intent for simulation is not supported.");
        return;
    }

    cmsUInt32Number lcmsIntentDisplay;
    if (soft_proofing)
    {
        const char *dis_intent = vsapi->propGetData(in, "display_intent", 0, &err);
        if (err)
        {
            lcmsIntentDisplay = INTENT_PERCEPTUAL;
        }
        else if (strcmp(dis_intent, "perceptual") == 0) lcmsIntentDisplay = INTENT_PERCEPTUAL;
        else if (strcmp(dis_intent, "relative") == 0) lcmsIntentDisplay = INTENT_RELATIVE_COLORIMETRIC;
        else if (strcmp(dis_intent, "saturation") == 0) lcmsIntentDisplay = INTENT_SATURATION;
        else if (strcmp(dis_intent, "absolute") == 0) lcmsIntentDisplay = INTENT_ABSOLUTE_COLORIMETRIC;
        else
        {
            cmsCloseProfile(lcmsProfileSimulation);
            cmsCloseProfile(lcmsProfileDisplay);
            vsapi->freeNode(d.node);
            vsapi->setError(out, "iccc: Input ICC intent for display is not supported.");
            return;
        }
    }

    cmsUInt32Number dwFlag = cmsFLAGS_NONEGATIVES;

    bool gamut_warning = vsapi->propGetInt(in, "gamut_warning", 0, &err);
    if (err)
    {
        gamut_warning = false;
    }
    if (gamut_warning) dwFlag = dwFlag | cmsFLAGS_GAMUTCHECK;

    bool bpc = vsapi->propGetInt(in, "black_point_compensation", 0, &err);
    if (err)
    {
        bpc = false;
    }
    if (bpc) dwFlag = dwFlag | cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clut_size = int64ToIntS(vsapi->propGetInt(in, "clut_size", 0, &err));
    if (err)
    {
        clut_size = 1;
    }
    if (clut_size == -1)
    {
        clut_size = 17; // default for cmsFLAGS_LOWRESPRECALC
    }
    else if (clut_size == 0)
    {
        clut_size = 33; // default
    }
    else if (clut_size == 1)
    {
        clut_size = 49; // default for cmsFLAGS_HIGHRESPRECALC
    }
    else if ((clut_size < -1) || (clut_size > 255))
    {
        cmsCloseProfile(lcmsProfileSimulation);
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input clut size seems not valid.");
        return;
    }

    dwFlag = dwFlag | cmsFLAGS_GRIDPOINTS(clut_size);

    if (soft_proofing)
    {
        dwFlag = dwFlag | cmsFLAGS_SOFTPROOFING;
        d.transform = cmsCreateProofingTransform(lcmsProfileSimulation, lcmsDataType, lcmsProfileDisplay, lcmsDataType, lcmsProfileSimulation, lcmsIntentDisplay, lcmsIntentSimulation, dwFlag);
    }
    else
    {
        d.transform = cmsCreateTransform(lcmsProfileSimulation, lcmsDataType, lcmsProfileDisplay, lcmsDataType, lcmsIntentSimulation, dwFlag);
    }

    cmsCloseProfile(lcmsProfileSimulation);
    cmsCloseProfile(lcmsProfileDisplay);

    icccData *data = new icccData(d);

    vsapi->createFilter(in, out, "ICCConvert", icccInit, icccGetFrame, icccFree, fmParallel, 0, data, core);
}

void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    icccData d;

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi))
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Only constant format input is supported.");
        return;
    }

    cmsUInt32Number lcmsDataType;
    if (d.vi->format->id == pfRGB48) lcmsDataType = TYPE_BGR_16;
    else if (d.vi->format->id == pfRGB24) lcmsDataType = TYPE_BGR_8;
    else if (d.vi->format->id == pfRGBS) lcmsDataType = TYPE_BGR_FLT;
    else
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Currently only RGB24, RGB48 and RGBS input formats are well supported.");
        return;
    }

    int err;

    cmsHPROFILE lcmsProfileDisplay;
    const char *dst_profile = vsapi->propGetData(in, "display_icc", 0, &err);
    if (err || !dst_profile)
    {
        lcmsProfileDisplay = get_profile_sys();
        if (!lcmsProfileDisplay)
        {
            vsapi->freeNode(d.node);
            vsapi->setError(out, "iccc: Auto detection of display ICC failed. You should specify from file instead.");
            return;
        }
    }
    else if (!(lcmsProfileDisplay = cmsOpenProfileFromFile(dst_profile, "r")))
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input display profile seems to be invalid.");
        return;
    }

    double gamma = vsapi->propGetFloat(in, "gamma", 0, &err);
    if (err)
    {
        gamma = -1.0;
    }
    else if ((gamma < 0.01) || (gamma > 100.0))
    {
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input gamma value is allowed between 0.01 and 100.0.");
        return;
    }

    cmsHPROFILE lcmsProfileSimulation;
    const char *src_profile = vsapi->propGetData(in, "playback_csp", 0, &err);
    if (err || strcmp(src_profile, "709") == 0)
    {
        lcmsProfileSimulation = get_profile_playback(csp_709, gamma, lcmsProfileDisplay);
    }
    else if ((strcmp(src_profile, "601-525") == 0) || (strcmp(src_profile, "170m") == 0) || (strcmp(src_profile, "240m") == 0))
    {
        lcmsProfileSimulation = get_profile_playback(csp_601_525, gamma, lcmsProfileDisplay);
    }
    else if ((strcmp(src_profile, "601-625") == 0) || (strcmp(src_profile, "470bg") == 0))
    {
        lcmsProfileSimulation = get_profile_playback(csp_601_625, gamma, lcmsProfileDisplay);
    }
    else if ((strcmp(src_profile, "2020") == 0) || (strcmp(src_profile, "2020-10") == 0) || (strcmp(src_profile, "2020-12") == 0))
    {
        lcmsProfileSimulation = get_profile_playback(csp_2020, gamma, lcmsProfileDisplay);
    }
    else if (strcmp(src_profile, "srgb") == 0)
    {
        lcmsProfileSimulation = cmsCreate_sRGBProfile();
    }
    else
    {
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input color space not yet supported.");
        return;
    }

    cmsUInt32Number lcmsIntent;
    const char *intent = vsapi->propGetData(in, "intent", 0, &err);
    if (err)
    {
        lcmsIntent = INTENT_RELATIVE_COLORIMETRIC;
    }
    else if (strcmp(intent, "perceptual") == 0) lcmsIntent = INTENT_PERCEPTUAL;
    else if (strcmp(intent, "relative") == 0) lcmsIntent = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(intent, "saturation") == 0) lcmsIntent = INTENT_SATURATION;
    else if (strcmp(intent, "absolute") == 0) lcmsIntent = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(lcmsProfileSimulation);
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input ICC intent is not supported.");
        return;
    }

    cmsUInt32Number dwFlag = cmsFLAGS_NONEGATIVES;

    bool gamut_warning = vsapi->propGetInt(in, "gamut_warning", 0, &err);
    if (err)
    {
        gamut_warning = false;
    }
    if (gamut_warning) dwFlag = dwFlag | cmsFLAGS_GAMUTCHECK;

    bool bpc = vsapi->propGetInt(in, "black_point_compensation", 0, &err);
    if (err)
    {
        bpc = true;
    }
    if (bpc) dwFlag = dwFlag | cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clut_size = int64ToIntS(vsapi->propGetInt(in, "clut_size", 0, &err));
    if (err)
    {
        clut_size = 1;
    }
    if (clut_size == -1)
    {
        clut_size = 17; // default for cmsFLAGS_LOWRESPRECALC
    }
    else if (clut_size == 0)
    {
        clut_size = 33; // default
    }
    else if (clut_size == 1)
    {
        clut_size = 49; // default for cmsFLAGS_HIGHRESPRECALC
    }
    else if ((clut_size < -1) || (clut_size > 255))
    {
        cmsCloseProfile(lcmsProfileSimulation);
        cmsCloseProfile(lcmsProfileDisplay);
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input clut size seems not valid.");
        return;
    }

    dwFlag = dwFlag | cmsFLAGS_GRIDPOINTS(clut_size);

    d.transform = cmsCreateTransform(lcmsProfileSimulation, lcmsDataType, lcmsProfileDisplay, lcmsDataType, lcmsIntent, dwFlag);

    cmsCloseProfile(lcmsProfileSimulation);
    cmsCloseProfile(lcmsProfileDisplay);

    icccData *data = new icccData(d);

    vsapi->createFilter(in, out, "ICCPlayback", icccInit, icccGetFrame, icccFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("Yomiko.collection.iccconvert", "iccc", "ICC Conversion", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("ICCConvert",
        "clip:clip;"
        "simulation_icc:data;"
        "display_icc:data:opt;"
        "soft_proofing:int:opt;"
        "simulation_intent:data:opt;"
        "display_intent:data:opt;"
        "gamut_warning:int:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt",
        icccCreate, nullptr, plugin);

    registerFunc("ICCPlayback",
        "clip:clip;"
        "display_icc:data:opt;"
        "playback_csp:data:opt;"
        "gamma:float:opt;"
        "intent:data:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt",
        iccpCreate, nullptr, plugin);
}
