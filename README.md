# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

---

## Usage

```python
iccc.Convert(clip, simulation_icc, display_icc, soft_proofing=True, simulation_intent='relative', display_intent='perceptual', gamut_warning=False, black_point_compensation=False, clut_size=49)
```
Intended for color profile conversion and soft proofing.

- The format of input `clip` must be either `RGB24` or `RGB48`. The output has the same format.

- `simulation_icc` is the path to the ICC profile to simulate (input profile for conversion).

- `display_icc` is the path to the ICC profile for your monitor (output profile for conversion).

  In Windows and in Linux X11, this parameter is *optional*. The default profile is detected for the current window (e.g. the editor window of VapourSynth Editor, or the console window used to launch vapoursynth-preview, but *not* their preview windows). Procedures of detection are a little different:
  - Windows: *foreground* window -> monitor -> ICC profile
    - The profile should be identical to the one shown in Start -> Settings -> System -> Display
  - Linux, X11: center of the window of *input focus* -> monitor -> ICC profile *by X11* or *by colord* (built differently)
    - If center of the window is out of any monitor, the detection fails
    - By connecting and disconnecting monitors, switching the primary display, etc., X11 may easily confuse the color profiles, but colord usually does right

  It's strongly recommended to manually specify the ICC profile for production purpose.

- `soft_proofing` is the flag for soft proofing. When enabled, it takes an additional backward step for the proofing. Otherwise simple conversion is applied. [Here](https://sourceforge.net/p/lcms/mailman/message/36783703/) is a brief introduction about how Little CMS handles it.

  Soft proofing requires both profiles are of display (`mntr`) device class. When such condition is not met, `soft_proofing` is automatically disabled, without reporting an error.

 - `simulation_intent` and `display_intent` are corresponding to the ICC rendering intents for simulation and for display, respectively, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
   - "perceptual" for Perceptual (default for display)
   - "saturation" for Saturation
   - "relative"   for Relative Colorimetric (default for simulation)
   - "absolute"   for Absolute Colorimetric

    If not in soft proofing mode, only `display_intent` will be taken.

 - `gamut_warning` is the flag for out-of-gamut warning. A certain color will fill the overflowing region. Default off.

 - `black_point_compensation` is the flag for what it tells. Default off.

 - `clut_size` specifies the internal LUT size used by Little CMS. The LUT size is applied for each plane (channel), so it does have an impact on the speed of plugin initialization.
 
   Possible values are the following:
    - 2~255 for the actual LUT size
    - 1 for Little CMS preset which is equivalent to 49 (default)
    - 0 for Little CMS preset which is equivalent to 33
    - -1 for Little CMS preset which is equivalent to 17

```python
iccc.Playback(clip, display_icc, playback_csp='709', gamma=None, intent='relative', black_point_compensation=True, clut_size=49)
```
Video playback with BT.1886 configuration, or overridden by a given float value of `gamma` (e.g. 2.4 for OLED displays). This should have the same behavior as the [mpv player](https://mpv.io/).

Currently supported `playback_csp` options are the following:
- `'709'` for HD
- `'2020'` for UHD
- `'601-525'`, `'170m'` or `'240m'` for SD (NTSC)
- `'601-625'`, `'470bg'` for SD (PAL)

For viewing images, instead, you may also set `playback_csp` as `'srgb'`.

```python
iccc.Extract(filename, output=<filename>.icc, overwrite=False, fallback_srgb=True)
```
Extract embedded color profile from single image. When there's no embedded profile, sRGB can be a fallback option.
When things are processed as expected, the return value is the path to the saved profile (in bytes format in Python, while you can use it as an input argument of other plugins).

---

## Manual Compilation

Please refer to [meson.build](https://github.com/YomikoR/VapourSynth-ICCConvert/blob/main/meson.build).

Some details to clarify:
- In Linux with colord, the colord module should be compiled into a shared library, whose relative path will be used. Therefore, place `libiccc_colord.so` in the same directory of `libiccc.so`, and do not change its name.

- In Windows with ImageMagick, the ImageMagick module should be compiled into a shared library, whose relative path will be used. I did a lazy modification that only changed the ending characters, so it should be always named `xxx_magick.dll` if the main `iccc` plugin is named `xxx.dll`. If this `_magick.dll` is missing, the plugin simply won't register the corresponding functions, so that they won't be visible by VapourSynth. For manual compilation, the `_magick.dll` consists of source files in the `magick` folder.
