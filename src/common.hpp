#ifndef _ICCC_COMMON
#define _ICCC_COMMON

#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>
#include <lcms2.h>
#include "libp2p/p2p_api.h"
#include <string>
#include <algorithm>

typedef struct
{
    cmsFloat64Number xw;
    cmsFloat64Number yw;
    cmsFloat64Number xr;
    cmsFloat64Number yr;
    cmsFloat64Number xg;
    cmsFloat64Number yg;
    cmsFloat64Number xb;
    cmsFloat64Number yb;
} cspData;

const cspData csp_709 = {0.3127, 0.3290, 0.64, 0.33, 0.3, 0.6, 0.15, 0.06};

const cspData csp_601_525 = {0.3127, 0.3290, 0.63, 0.34, 0.31, 0.595, 0.155, 0.07};

const cspData csp_601_625 = {0.3127, 0.3290, 0.64, 0.33, 0.29, 0.6, 0.15, 0.06};

typedef struct
{
    VSNodeRef *node = nullptr;
    const VSVideoInfo *vi = nullptr;
    cmsHTRANSFORM transform = nullptr;
} icccData;

cmsHPROFILE profile_1886(const cspData &csp, const cmsHPROFILE &lcmsProfileDisplay);

void VS_CC icccInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi);

const VSFrameRef *VS_CC icccGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

void VS_CC icccFree(void *instanceData, VSCore *core, const VSAPI *vsapi);

extern "C" char *get_sys_color_profile();

#endif
