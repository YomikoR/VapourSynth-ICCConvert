project('ICCConvert', ['c', 'cpp'],
    license: 'GPL-3.0-or-later',
    default_options: ['buildtype=release', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++17', 'optimization=2'],
    meson_version: '>=0.51.0',
    version: '6.2'
)

c = meson.get_compiler('c')

sources = [
    'src/iccc.cc',
    'src/1886.cc',
    'src/plugin.cc',
]

deps = []

libs = []

auto_profile_args = []

plugin_args = []

link_args = []

# libp2p
libs += static_library('libp2p_main',
    [
        'src/libp2p/p2p_api.cpp',
        'src/libp2p/v210.cpp',
        'src/libp2p/simd/cpuinfo_x86.cpp',
        'src/libp2p/simd/p2p_simd.cpp',
    ],
    cpp_args: ['-DP2P_SIMD', '-std=c++14']
)

libs += static_library('libp2p_sse41', 'src/libp2p/simd/p2p_sse41.cpp',
    cpp_args: ['-DP2P_SIMD', '-std=c++14', '-msse4.1']
)

# detection
if host_machine.system() == 'linux'
    deps += dependency('lcms2')
    dep_xrandr = dependency('xrandr', required: false, version: '>= 1.2')
    dep_colord = dependency('colord', required: false)
    if dep_xrandr.found()
        auto_profile_args += '-DAUTO_PROFILE_X11'
        deps += dep_xrandr
        if dep_colord.found()
            auto_profile_args += '-DAUTO_PROFILE_COLORD'
            shared_module('iccc_colord', 'src/detection/colord.c',
                dependencies: dep_colord,
                c_args: auto_profile_args,
                name_prefix: 'lib',
                install: true
            )
        endif
        sources += ['src/detection/x11.c']
    endif
    dep_fastfloat = c.find_library('lcms2_fast_float', required: false)
    if dep_fastfloat.found()
        plugin_args += '-DUSE_LCMS2_FAST_FLOAT'
        link_args += '-llcms2_fast_float'
    endif
elif host_machine.system() == 'darwin'
    deps += dependency('lcms2')
    dep_cocoa = dependency('appleframeworks', modules: 'Cocoa', required: false)
    if dep_cocoa.found()
        add_languages('objc', native: true)
        auto_profile_args += '-DAUTO_PROFILE_COCOA'
        deps += dep_cocoa
        shared_module('iccc_cocoa', 'src/detection/cocoa.m',
            dependencies: dependency('lcms2'),
            objc_args: auto_profile_args,
            name_prefix: 'lib',
            install: true
        )
        sources += ['src/detection/macos.c']
    endif
    dep_fastfloat = c.find_library('lcms2_fast_float', required: false)
    if dep_fastfloat.found()
        plugin_args += '-DUSE_LCMS2_FAST_FLOAT'
        link_args += '-llcms2_fast_float'
    endif
elif host_machine.system() == 'windows'
    deps += dependency('lcms2', static: true)
    sources += ['src/detection/win32.c']
    plugin_args += '-DUSE_LCMS2_FAST_FLOAT'
    link_args += '-static'
endif

shared_module('iccc', sources,
    include_directories: 'src',
    dependencies: deps,
    c_args: auto_profile_args,
    cpp_args: [auto_profile_args, plugin_args],
    link_with: libs,
    link_args: link_args,
    name_prefix: 'lib',
    gnu_symbol_visibility : 'hidden'
)
