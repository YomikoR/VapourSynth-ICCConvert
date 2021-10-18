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
    // A map for transforms. Since I wish IMWRI embeds ICC for every frame...
    std::unordered_map<inputICCData, cmsHTRANSFORM, inputICCHashFunction> transform_map;
    std::mutex transform_map_mutex;
    // Defaults
    cmsHPROFILE default_out;
    cmsHTRANSFORM default_transform; // This one is a copy from the map, don't free it directly
    cmsUInt32Number default_intent;
    cmsUInt32Number transform_flag;
    // Format: now either RGB24 or RGB48
    cmsUInt32Number datatype;
    p2p_packing p2p_type = p2p_packing_max;
    // Flag for using props
    bool prefer_props;
    void clear()
    {
        if (default_out) cmsCloseProfile(default_out);
        for (auto pair : transform_map)
        {
            if (pair.second) cmsDeleteTransform(pair.second);
        }
    }
};

static cmsHTRANSFORM getTransform(const inputICCData &ind, icccData *d)
{
    std::lock_guard<std::mutex> lock(d->transform_map_mutex);
    auto found = d->transform_map.find(ind);
    if (found == d->transform_map.end())
    {
        cmsHTRANSFORM transform = cmsCreateTransform(ind.profile, d->datatype, d->default_out, d->datatype, ind.intent, d->transform_flag);
        if (transform)
        {
            d->transform_map[ind] = transform;
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

        VSFrame *dst_frame = vsapi->newVideoFrame(&d->vi.format, width, height, frame, core);

        // Create or find transform
        cmsHTRANSFORM transform = d->default_transform;
        if (d->prefer_props)
        {
            VSMap *map = vsapi->getFramePropertiesRW(dst_frame);
            int err;
            int icc_len = vsapi->mapGetDataSize(map, "ICCProfile", 0, &err);
            if (!err && icc_len > 0)
            {
                const char *icc_data = vsapi->mapGetData(map, "ICCProfile", 0, &err);
                // Create profile
                cmsHPROFILE inp = cmsOpenProfileFromMem(icc_data, icc_len);
                if (inp)
                {
                    // Sanity checks
                    if ((cmsGetDeviceClass(inp) != cmsSigDisplayClass) && (cmsGetDeviceClass(inp) != cmsSigInputClass))
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dst_frame);
                        vsapi->setFilterError("iccc: The device class of the embedded ICC profile is not supported.", frameCtx);
                        return nullptr;
                    }
                    if (cmsGetColorSpace(inp) != cmsSigRgbData)
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dst_frame);
                        vsapi->setFilterError("iccc: The colorspace of the embedded ICC profile is not supported.", frameCtx);
                        return nullptr;
                    }
                    cmsUInt32Number intent = cmsGetHeaderRenderingIntent(inp);
                    inputICCData ind(inp, intent);
                    transform = getTransform(ind, d);
                    cmsCloseProfile(inp);
                    if (!transform)
                    {
                        vsapi->freeFrame(frame);
                        vsapi->freeFrame(dst_frame);
                        vsapi->setFilterError("iccc: Failed to create transform from embedded ICC profile.", frameCtx);
                        return nullptr;
                    }
                }
                else
                {
                    vsapi->freeFrame(frame);
                    vsapi->freeFrame(dst_frame);
                    vsapi->setFilterError("iccc: Unable to read embedded ICC profile. Corrupted?", frameCtx);
                    return nullptr;
                }
                vsapi->mapDeleteKey(map, "ICCProfile");
            }
        }

        if (!transform)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dst_frame);
            vsapi->setFilterError("iccc: Failed to construct transform. This may be caused by insufficient ICC profile info provided.", frameCtx);
            return nullptr;
        }

        void *packed = vsh::vsh_aligned_malloc(stride * height * 3, 32);
        if (!packed)
        {
            vsapi->freeFrame(frame);
            vsapi->freeFrame(dst_frame);
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
        p2p_src.packing = d->p2p_type;
        p2p_pack_frame(&p2p_src, 0);

        // Transform
        cmsDoTransformLineStride(transform, packed, packed, width, height, stride * 3, stride * 3, 0, 0);

        // Unpack
        p2p_buffer_param p2p_dst = {};
        p2p_dst.width = width;
        p2p_dst.height = height;
        for (int plane = 0; plane < 3; ++plane)
        {
            p2p_dst.dst[plane] = vsapi->getWritePtr(dst_frame, plane);
            p2p_dst.dst_stride[plane] = vsapi->getStride(dst_frame, plane);
        }
        p2p_dst.src[0] = packed;
        p2p_dst.src_stride[0] = stride * 3;
        p2p_dst.packing = d->p2p_type;
        p2p_unpack_frame(&p2p_dst, 0);

        vsh::vsh_aligned_free(packed);
        vsapi->freeFrame(frame);
        return dst_frame;
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
    uint32_t src_format = vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core);

    if (src_format == pfRGB24)
    {
        d->datatype = TYPE_BGR_8;
        d->p2p_type = p2p_rgb24;
    }
    else if (src_format == pfRGB48)
    {
        d->datatype = TYPE_BGR_16;
        d->p2p_type = p2p_rgb48;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24 and RGB48 input formats are well supported.");
        return;
    }

    d->vi = *vi;

    int err;

    d->prefer_props = vsapi->mapGetInt(in, "prefer_props", 0, &err) || err;

    cmsHPROFILE input_profile = nullptr;
    const char *src_profile = vsapi->mapGetData(in, "simulation_icc", 0, &err);
    if (err || !src_profile)
    {
        input_profile = nullptr;
    }
    else if (!(input_profile = cmsOpenProfileFromFile(src_profile, "r")))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input simulated profile seems to be invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(input_profile) != cmsSigDisplayClass) && (cmsGetDeviceClass(input_profile) != cmsSigInputClass))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input simulated profile must have 'display' ('mntr') or 'input' ('scnr') device class.");
        return;
    }
    else if (cmsGetColorSpace(input_profile) != cmsSigRgbData)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input simulated profile must be for RGB colorspace.");
        return;
    }
    if (!d->prefer_props && !input_profile)
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input simulated profile must be provided.");
        return;
    }

    const char *dst_profile = vsapi->mapGetData(in, "display_icc", 0, &err);
    if (err || !dst_profile)
    {
        d->default_out = get_profile_sys();
        if (!d->default_out)
        {
            input_profile && cmsCloseProfile(input_profile);
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Auto detection of display ICC failed. You should specify from file instead.");
            return;
        }
    }
    else if (!(d->default_out = cmsOpenProfileFromFile(dst_profile, "r")))
    {
        input_profile && cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile seems to be invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(d->default_out) != cmsSigDisplayClass) && (cmsGetDeviceClass(d->default_out) != cmsSigOutputClass))
    {
        cmsCloseProfile(d->default_out);
        input_profile && cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
        return;
    }
    else if (cmsGetColorSpace(d->default_out) != cmsSigRgbData)
    {
        cmsCloseProfile(d->default_out);
        input_profile && cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must be for RGB colorspace.");
        return;
    }

    const char* sim_intent = vsapi->mapGetData(in, "intent", 0, &err);
    if (err || !sim_intent)
    {
        if (input_profile) d->default_intent = cmsGetHeaderRenderingIntent(input_profile);
    }
    else if (strcmp(sim_intent, "perceptual") == 0) d->default_intent = INTENT_PERCEPTUAL;
    else if (strcmp(sim_intent, "relative") == 0) d->default_intent = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(sim_intent, "saturation") == 0) d->default_intent = INTENT_SATURATION;
    else if (strcmp(sim_intent, "absolute") == 0) d->default_intent = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(d->default_out);
        input_profile && cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC intent is not supported.");
        return;
    }

    d->transform_flag = cmsFLAGS_NONEGATIVES;

    bool gamut_warning = !!vsapi->mapGetInt(in, "gamut_warning", 0, &err);
    if (gamut_warning) d->transform_flag |= cmsFLAGS_GAMUTCHECK;

    bool black_point_compensation = !!vsapi->mapGetInt(in, "black_point_compensation", 0, &err);
    if (black_point_compensation) d->transform_flag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clut_size = vsh::int64ToIntS(vsapi->mapGetInt(in, "clut_size", 0, &err));
    if (err) clut_size = 1;
    if (clut_size == -1) clut_size = 17; // default for cmsFLAGS_LOWRESPRECALC
    else if (clut_size == 0) clut_size = 33; // default
    else if (clut_size == 1) clut_size = 49; // default for cmsFLAGS_HIGHRESPRECALC
    else if ((clut_size < -1) || (clut_size > 255))
    {
        cmsCloseProfile(d->default_out);
        input_profile && cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input clut size seems not valid.");
        return;
    }
    d->transform_flag |= cmsFLAGS_GRIDPOINTS(clut_size);

    // Create a default transform. If it's null, leave error report to the runtime.
    if (input_profile)
    {
        d->default_transform = cmsCreateTransform(input_profile, d->datatype, d->default_out, d->datatype, d->default_intent, d->transform_flag);
        inputICCData ind(input_profile, d->default_intent);
        d->transform_map[ind] = d->default_transform;
        cmsCloseProfile(input_profile);
    }
    else
    {
        d->default_transform = nullptr;
    }

    std::vector<VSFilterDependency> dep_req =
    {
        {d->node, rpStrictSpatial}
    };

    vsapi->createVideoFilter(out, "Convert", &d->vi, icccGetFrame, icccFree, fmParallel, dep_req.data(), dep_req.size(), d.get(), core);
    d.release();
}

// Playback won't respect frame properties for variable transforms. That's evil.
void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    std::unique_ptr<icccData> d(new icccData());

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    uint32_t src_format = vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core);

    if (src_format == pfRGB24)
    {
        d->datatype = TYPE_BGR_8;
        d->p2p_type = p2p_rgb24;
    }
    else if (src_format == pfRGB48)
    {
        d->datatype = TYPE_BGR_16;
        d->p2p_type = p2p_rgb48;
    }
    else
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Currently only RGB24 and RGB48 input formats are well supported.");
        return;
    }

    d->vi = *vi;

    int err;

    const char *dst_profile = vsapi->mapGetData(in, "display_icc", 0, &err);
    if (err || !dst_profile)
    {
        d->default_out = get_profile_sys();
        if (!d->default_out)
        {
            vsapi->freeNode(d->node);
            vsapi->mapSetError(out, "iccc: Auto detection of display ICC failed. You should specify from file instead.");
            return;
        }
    }
    else if (!(d->default_out = cmsOpenProfileFromFile(dst_profile, "r")))
    {
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile seems to be invalid.");
        return;
    }
    else if ((cmsGetDeviceClass(d->default_out) != cmsSigDisplayClass) && (cmsGetDeviceClass(d->default_out) != cmsSigOutputClass))
    {
        cmsCloseProfile(d->default_out);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input display profile must have 'display' ('mntr') or 'output' ('prtr') device class.");
        return;
    }
    else if (cmsGetColorSpace(d->default_out) != cmsSigRgbData)
    {
        cmsCloseProfile(d->default_out);
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
        cmsCloseProfile(d->default_out);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input gamma value is allowed between 0.01 and 100.0.");
        return;
    }

    cmsHPROFILE input_profile = nullptr;
    const char *src_profile = vsapi->mapGetData(in, "playback_csp", 0, &err);
    if (err || strcmp(src_profile, "709") == 0)
    {
        input_profile = get_profile_playback(csp_709, gamma, d->default_out);
    }
    else if ((strcmp(src_profile, "170m") == 0) || (strcmp(src_profile, "601-525") == 0))
    {
        input_profile = get_profile_playback(csp_601_525, gamma, d->default_out);
    }
    else if ((strcmp(src_profile, "2020") == 0) || (strcmp(src_profile, "2020-10") == 0) || (strcmp(src_profile, "2020-12") == 0))
    {
        input_profile = get_profile_playback(csp_2020, gamma, d->default_out);
    }
    else if (strcmp(src_profile, "srgb") == 0)
    {
        input_profile = cmsCreate_sRGBProfile();
    }
    else
    {
        cmsCloseProfile(d->default_out);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input color space not yet supported.");
        return;
    }
    if (!input_profile)
    {
        cmsCloseProfile(d->default_out);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Failed to generate ICC for playback.");
        return;
    }

    const char *intent = vsapi->mapGetData(in, "intent", 0, &err);
    if (err) // Default of mpv
    {
        d->default_intent = INTENT_RELATIVE_COLORIMETRIC;
    }
    else if (strcmp(intent, "perceptual") == 0) d->default_intent = INTENT_PERCEPTUAL;
    else if (strcmp(intent, "relative") == 0) d->default_intent = INTENT_RELATIVE_COLORIMETRIC;
    else if (strcmp(intent, "saturation") == 0) d->default_intent = INTENT_SATURATION;
    else if (strcmp(intent, "absolute") == 0) d->default_intent = INTENT_ABSOLUTE_COLORIMETRIC;
    else
    {
        cmsCloseProfile(d->default_out);
        cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input ICC intent is not supported.");
        return;
    }

    d->transform_flag = cmsFLAGS_NONEGATIVES;

    bool gamut_warning = vsapi->mapGetInt(in, "gamut_warning", 0, &err);
    if (err || gamut_warning) d->transform_flag |= cmsFLAGS_GAMUTCHECK;

    bool black_point_compensation = !!vsapi->mapGetInt(in, "black_point_compensation", 0, &err);
    if (black_point_compensation) d->transform_flag |= cmsFLAGS_BLACKPOINTCOMPENSATION;

    int clut_size = vsh::int64ToIntS(vsapi->mapGetInt(in, "clut_size", 0, &err));
    if (err) clut_size = 1;
    if (clut_size == -1) clut_size = 17; // default for cmsFLAGS_LOWRESPRECALC
    else if (clut_size == 0) clut_size = 33; // default
    else if (clut_size == 1) clut_size = 49; // default for cmsFLAGS_HIGHRESPRECALC
    else if ((clut_size < -1) || (clut_size > 255))
    {
        cmsCloseProfile(d->default_out);
        cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Input clut size seems not valid.");
        return;
    }
    d->transform_flag |= cmsFLAGS_GRIDPOINTS(clut_size);

    d->default_transform = cmsCreateTransform(input_profile, d->datatype, d->default_out, d->datatype, d->default_intent, d->transform_flag);
    if (!d->default_transform)
    {
        cmsCloseProfile(d->default_out);
        cmsCloseProfile(input_profile);
        vsapi->freeNode(d->node);
        vsapi->mapSetError(out, "iccc: Failed to create transform for playback.");
        return;
    }
    // This is not necessary but we are going to free default_transform there
    inputICCData ind(input_profile, d->default_intent);
    d->transform_map[ind] = d->default_transform;
    cmsCloseProfile(input_profile);

    d->prefer_props = false;

    std::vector<VSFilterDependency> dep_req =
    {
        {d->node, rpStrictSpatial}
    };

    vsapi->createVideoFilter(out, "Playback", &d->vi, icccGetFrame, icccFree, fmParallel, dep_req.data(), dep_req.size(), d.get(), core);
    d.release();
}
