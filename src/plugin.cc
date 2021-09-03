#include "common.hpp"
#include "magick/magick.hpp"
#include <vector>
#include <iterator>

extern void VS_CC icccCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
extern void VS_CC iccpCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
extern void VS_CC immxCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

#if defined (_WIN32)
#include "windows.h"

static char dummy;

static void check_dll_func(std::vector<const char *> func_names, std::string &ret)
{
    // Get this DLL
    char path[MAX_PATH - 8];
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &dummy, &module))
    {
        ret.clear();
        return;
    }
    GetModuleFileNameA(module, path, sizeof(path));
    std::string path_s = path;
    if (path_s.size() < 4)
    {
        ret.clear();
        return;
    }
    std::string dotdll = ".dll";
    if (!std::equal(dotdll.rbegin(), dotdll.rend(), path_s.rbegin()))
    {
        ret.clear();
        return;
    }
    std::string magick_dll_path = path_s.substr(0, path_s.size() - 4).append("_magick.dll");
    // Load iccc_magick.dll
    HMODULE mmodule = LoadLibraryA(magick_dll_path.c_str());
    if (!mmodule)
    {
        ret.clear();
        return;
    }
    // resolve functions by name
    bool all_resolved = true;
    for (const char* func_name : func_names)
    {
        if (!GetProcAddress(mmodule, func_name))
        {
            all_resolved = false;
            break;
        }
    }
    if(all_resolved)
    {
        FreeLibrary(mmodule);
        ret.clear();
        ret.append(magick_dll_path);
        return;
    }
    else
    {
        ret.clear();
        return;
    }
}
#endif

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc(ICCC_PLUGIN_ID, "iccc", "ICC Conversion", VAPOURSYNTH_API_VERSION, 1, plugin);

    registerFunc("Convert",
        "clip:clip;"
        "simulation_icc:data;"
        "display_icc:data:opt;"
        "intent:data:opt;"
        "gamut_warning:int:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt",
        icccCreate, nullptr, plugin);

    registerFunc("Playback",
        "clip:clip;"
        "display_icc:data:opt;"
        "playback_csp:data:opt;"
        "gamma:float:opt;"
        "intent:data:opt;"
        "black_point_compensation:int:opt;"
        "clut_size:int:opt",
        iccpCreate, nullptr, plugin);

#if defined(_WIN32)
    static std::string dll_path_s;
    std::vector<const char*> func_names(std::begin(magick_function_list), std::end(magick_function_list));
    check_dll_func(func_names, dll_path_s);
    if (!dll_path_s.empty())
    {
        registerFunc("Extract",
        "filename:data;output:data:opt;overwrite:int:opt;fallback_srgb:int:opt;",
        immxCreate, static_cast<void *>(&dll_path_s), plugin);    
    }
#else
    registerFunc("Extract",
        "filename:data;output:data:opt;overwrite:int:opt;fallback_srgb:int:opt;",
        immxCreate, nullptr, plugin);
#endif
}
