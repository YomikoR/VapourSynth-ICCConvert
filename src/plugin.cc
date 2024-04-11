#include "common.hpp"

extern void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
extern void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
extern void VS_CC tagCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi)
{
    vspapi->configPlugin(ICCC_PLUGIN_ID, "iccc", "ICC Conversion", ICCC_PLUGIN_VERSION, VAPOURSYNTH_API_VERSION, 0, plugin);

    vspapi->registerFunction("Convert",
        "clip:vnode;"
        "input_icc:data:opt;"
        "display_icc:data:opt;"
        "intent:data:opt;"
        "proofing_icc:data:opt;"
        "proofing_intent:data:opt;"
        "gamut_warning:int:opt;"
        "gamut_warning_color:int[]:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt;"
        "prefer_props:int:opt;",
        "clip:vnode;",
        icccCreate, nullptr, plugin
    );

    vspapi->registerFunction("Playback",
        "clip:vnode;"
        "csp:data:opt;"
        "display_icc:data:opt;"
        "gamma:float:opt;"
        "contrast:float:opt;"
        "intent:data:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt;"
        "inverse:int:opt;",
        "clip:vnode;",
        iccpCreate, nullptr, plugin
    );

    vspapi->registerFunction("Tag",
        "clip:vnode;"
        "icc:data;",
        "clip:vnode;",
        tagCreate, nullptr, plugin
    );
}
