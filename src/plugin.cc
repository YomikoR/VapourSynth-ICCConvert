#include "vapoursynth/VapourSynth.h"

extern void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
extern void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

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
