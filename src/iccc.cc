#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>
#include <lcms2.h>
#include "libp2p/p2p_api.h"
#include <string>
#include <algorithm>

typedef struct
{
    VSNodeRef *node = nullptr;
    const VSVideoInfo *vi = nullptr;
    cmsHTRANSFORM transform = nullptr;
} icccData;

static void VS_CC icccInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = static_cast<icccData *>(*instanceData);
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC icccGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
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

        void *raw_src = vs_aligned_malloc(width * height * 3 * bps, 32);
        void *raw_dst = vs_aligned_malloc(width * height * 3 * bps, 32);

        // pack
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
        p2p_src.packing = (bps == 2) ? p2p_rgb48_le : p2p_rgb24_le;
        p2p_pack_frame(&p2p_src, 0);

        // transform
        cmsDoTransform(d->transform, raw_src, raw_dst, static_cast<cmsUInt32Number>(width * height));

        // unpack
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
        p2p_dst.packing = (bps == 2) ? p2p_rgb48_le : p2p_rgb24_le;
        p2p_unpack_frame(&p2p_dst, 0);

        vs_aligned_free(raw_src);
        vs_aligned_free(raw_dst);
        vsapi->freeFrame(frame);
        return dst_frame;
    }
    return nullptr;
}

static void VS_CC icccFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    icccData *d = static_cast<icccData *>(instanceData);
    vsapi->freeNode(d->node);
    if (d->transform) cmsDeleteTransform(d->transform);
    delete d;
}

static void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
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
    else
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Currently only vs.RGB48 and vs.RGB24 are well supported.");
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
    if (err || !(lcmsProfileDisplay = cmsOpenProfileFromFile(dst_profile, "r")))
    {
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input display profile seems to be invalid.");
        return;
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
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input ICC intent for simulation is not supported.");
        return;
    }

    cmsUInt32Number lcmsIntentDisplay;
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
        vsapi->freeNode(d.node);
        vsapi->setError(out, "iccc: Input ICC intent for display is not supported.");
        return;
    }

    cmsUInt32Number dwFlag = cmsFLAGS_HIGHRESPRECALC | cmsFLAGS_SOFTPROOFING;

    bool gamut_warning = vsapi->propGetInt(in, "gamut_warning", 0, &err);
    if (err)
    {
        gamut_warning = false;
    }
    if (gamut_warning) dwFlag = dwFlag | cmsFLAGS_GAMUTCHECK;

    d.transform = cmsCreateProofingTransform(lcmsProfileSimulation, lcmsDataType, lcmsProfileDisplay, lcmsDataType, lcmsProfileSimulation, lcmsIntentDisplay, lcmsIntentSimulation, dwFlag);

    cmsCloseProfile(lcmsProfileSimulation);
    cmsCloseProfile(lcmsProfileDisplay);

    icccData *data = new icccData(d);

    vsapi->createFilter(in, out, "ICCConvert", icccInit, icccGetFrame, icccFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("Yomiko.collection.iccconvert", "iccc", "ICC Conversion", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("ICCConvert",
        "clip:clip;"
        "simulation_icc:data;"
        "display_icc:data;"
        "simulation_intent:data:opt;"
        "display_intent:data:opt;"
        "gamut_warning:int:opt",
        icccCreate, nullptr, plugin);
}
