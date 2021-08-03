# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

---

## Usage

```python
iccc.ICCConvert(clip, simulation_icc, display_icc, soft_proofing=True, simulation_intent='relative', display_intent='perceptual', gamut_warning=False, black_point_compensation=False, clut_size=49)
```
Intended for soft proofing.

- Input `clip` must have one of the formats `RGB24`, `RGB48` and `RGBS`. The output has the same format.

  Be aware that Little CMS is [almost not optimized](https://www.littlecms.com/plugin/) for float inputs, and the quality improvement from using floating-point formats is often not worthy.

- `simulation_icc` is the path to the ICC profile to simulate.

- `display_icc` is the path to the ICC profile for your monitor.

  In Windows and in Linux X11, this parameter is *optional*. The default profile is detected for the current window (e.g. the editor window of VapourSynth Editor, or the console window used to launch vapoursynth-preview, but *not* their preview windows). Procedures of detection are a little different:
  - Windows: *foreground* window -> monitor -> ICC profile
    - The profile should be identical to the one shown in Start -> Settings -> System -> Display
  - Linux, X11: center of the window of *input focus* -> monitor -> ICC profile *by X11* or *by colord* (built differently, see the compilation section)
    - If center of the window is out of any monitor, the detection fails
    - By connecting and disconnecting monitors, switching the primary display, etc., X11 may easily confuse the color profiles, but colord usually does right

  It's strongly recommended to manually specify the ICC profile for production purpose.

- `soft_proofing` is the flag for soft proofing. Default on. Disable to apply simple conversion, otherwise it further takes a backward step for the proofing.
 
  [Here](https://sourceforge.net/p/lcms/mailman/message/36783703/) is a brief introduction about how Little CMS handles it.

 - `simulation_intent` and `display_intent` are corresponding to the ICC rendering intents for simulation and for display, respectively, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
   - "perceptual" for Perceptual (default for display)
   - "saturation" for Saturation
   - "relative"   for Relative Colorimetric (default for simulation)
   - "absolute"   for Absolute Colorimetric

    If not in soft proofing mode, only `simulation_intent` will be taken.

 - `gamut_warning` is the flag for out-of-gamut warning. A certain color will fill the overflowing region. Default off.

 - `black_point_compensation` is the flag for what it tells. Default off.

 - `clut_size` specifies the internal LUT size used by Little CMS. The LUT size is applied for each plane (channel), so it does have an impact on the speed of plugin initialization.
 
   Possible values are the following:
    - 2~255 for the actual LUT size
    - 1 for Little CMS preset which is equivalent to 49 (default)
    - 0 for Little CMS preset which is equivalent to 33
    - -1 for Little CMS preset which is equivalent to 17

```python
iccc.ICCPlayback(clip, display_icc, playback_csp='709', gamma=None, intent='perceptual', black_point_compensation=False, clut_size=49)
```
One-way color profile mapping for video playback with BT.1886 configuration, or overridden by a given float value of `gamma` (e.g. 2.4 for OLED displays).

Currently supported `playback_csp` options are the following:
- `'709'` for HD
- `'2020'` for UHD
- `'601-525'`, `'170m'` or `'240m'` for SD (NTSC)
- `'601-625'`, `'470bg'` for SD (PAL)

However, you may also set it as `'srgb'` for images.

By default, black point compensation is enabled in mpv, but here the flag is off.

---

## Manual Compilation

In Mingw-w64:
- Install `mingw-w64-x86_64-lcms2`
- ```
  git clone https://github.com/YomikoR/VapourSynth-ICCConvert --recursive && cd VapourSynth-ICCConvert/src

  gcc detection/win32.c -O2 -c -o icc_detection.o

  g++ iccc.cc 1886.cc icc_detection.o libp2p/p2p_api.cpp libp2p/v210.cpp -O2 -static -shared -llcms2 -lgdi32 -I. -o libiccc.dll
  ```

In Linux with X11:
- Libraries of `X11`, `Xrandr`, `lcms2` are required.
- ```
  git clone https://github.com/YomikoR/VapourSynth-ICCConvert --recursive && cd VapourSynth-ICCConvert/src
  
  gcc detection/x11.c -DAUTO_PROFILE_X11 -O2 -fPIC -c -o icc_detection.o
  
  g++ iccc.cc 1886.cc icc_detection.o libp2p/p2p_api.cpp libp2p/v210.cpp -DAUTO_PROFILE_X11 -O2 -fPIC -shared -llcms2 -lX11 -lXrandr -I. -o libiccc.so
  ```

In Linux with X11 and **colord**:
 - Libraries of `X11`, `Xrandr`, `lcms2` and `colord` are required.
 - The colord module may be compiled into a standalone library, whose relative path will be used. Therefore, place `libiccc_colord.so` in the same directory of the main output, and do not change its name.
 - ```
   git clone https://github.com/YomikoR/VapourSynth-ICCConvert --recursive && cd VapourSynth-ICCConvert/src

   gcc detection/colord.c -DAUTO_PROFILE_COLORD -O2 -fPIC -shared `pkg-config --cflags colord` `pkg-config --libs colord` -o libiccc_colord.so

   gcc detection/x11.c -DAUTO_PROFILE_X11 -DAUTO_PROFILE_COLORD -O2 -fPIC -c -o icc_detection.o

   g++ iccc.cc 1886.cc icc_detection.o libp2p/p2p_api.cpp libp2p/v210.cpp -DAUTO_PROFILE_X11 -DAUTO_PROFILE_COLORD -O2 -fPIC -shared -llcms2 -lX11 -lXrandr -I. -o libiccc.so
   ```