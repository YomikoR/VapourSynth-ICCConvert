# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

---

## Usage

```python
iccc.Convert(clip, simulation_icc, display_icc=<from_system>, intent=<from_simulation_icc>, gamut_warning=False, black_point_compensation=False, clut_size=49, prefer_props=True)
```
Color profile conversion.

- The format of input `clip` must be either `RGB24` or `RGB48`. The output has the same format.

- `simulation_icc` is the path to the ICC profile to simulate (input profile for conversion).

  When `prefer_props` is enabled, it is an *optional* fallback value for embedded ICC profiles read from frame properties.

- `display_icc` is the path to the ICC profile for your monitor (output profile for conversion).

  In Windows and in Linux X11, this parameter is *optional*. The default profile is detected for the current window (e.g. the editor window of VapourSynth Editor, or the console window used to launch vapoursynth-preview, but *not* their preview windows). Procedures of detection are a little different:
  - Windows: *foreground* window -> monitor -> ICC profile
    - The profile should be identical to the one shown in Start -> Settings -> System -> Display
  - Linux, X11: center of the window of *input focus* -> monitor -> ICC profile *by X11* or *by colord* (built differently)
    - If center of the window is out of any monitor, the detection fails
    - By connecting and disconnecting monitors, switching the primary display, etc., X11 may easily confuse the color profiles, but colord usually does right

  It's strongly recommended to manually specify the ICC profile for production purpose.

 - `intent` refers to the ICC rendering intent for conversion, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
   - "perceptual" for Perceptual
   - "saturation" for Saturation
   - "relative"   for Relative Colorimetric
   - "absolute"   for Absolute Colorimetric

    The default value is taken from the input profile header.

    Not all rendering intents are supported by all display profiles. If the profile is not providing sufficient information for selected intent, Little CMS has the following fallback rules:

    - Perceptual: the default intent of the (output) profile.
    - Relative Colorimetric: perceptual.
    - Saturation: perceptual.
    - Absolute Colorimetric: relative colorimetric intent, with undoing of chromatic adaptation.

 - `gamut_warning` is the flag for out-of-gamut warning. A certain color will fill the overflowing region. Default off.

 - `black_point_compensation` is the flag for what it tells. Default off.

 - `clut_size` specifies the internal LUT size used by Little CMS. The LUT size is applied for each plane (channel), so it does have an impact on the speed of plugin initialization.
 
   Possible values are the following:
    - 2~255 for the actual LUT size
    - 1 for Little CMS preset which is equivalent to 49 (default)
    - 0 for Little CMS preset which is equivalent to 33
    - -1 for Little CMS preset which is equivalent to 17

 - `prefer_props` is the flag for reading embedded ICC profiles from the frame property `ICCProfile`. Default on. The rendering intent from the header of the embedded profile will also override the above `intent`.

    ICC profiles are internally hashed to reuse exising ICC transform instances, so duplication of embedded ICC profiles from the input clip won't cause a big performance loss.

```python
iccc.Playback(clip, display_icc, playback_csp='709', gamma=None, intent='relative', black_point_compensation=True, clut_size=49)
```
Video playback with BT.1886 configuration, or overridden by a given float value of `gamma` (e.g. 2.4 for OLED displays). This should have the same behavior as the [mpv player](https://mpv.io/).

Currently supported `playback_csp` options are the following:
- `'709'` for HD
- `'2020'` for UHD (SDR)
- `'170m'` for SD (NTSC)

For viewing images, instead, you may also set `playback_csp` as `'srgb'`.

```python
iccc.Extract(filename, output=<filename>.icc, overwrite=False, fallback_srgb=True)
```
Extract embedded color profile from single image, and read its default rendering intent. When there's no embedded profile, sRGB can be a fallback option.

When things are processed as expected, it returns a `dict` with key `path` for the location of extracted ICC file, and key `intent` for the name of its default rendering intent. The naming convention is for using as an argument of the above functions.

---

## Manual Compilation

Please refer to [meson.build](https://github.com/YomikoR/VapourSynth-ICCConvert/blob/main/meson.build).

Some details to clarify:
- In Linux with colord, the colord module should be compiled into a shared library, whose relative path will be used. Therefore, place `libiccc_colord.so` in the same directory of `libiccc.so`, and do not change its name.

- In Windows with ImageMagick, the ImageMagick module should be compiled into a shared library, whose relative path will be used. I did a lazy modification that only changed the ending characters, so it should be always named `xxx_magick.dll` if the main `iccc` plugin is named `xxx.dll`. If this `_magick.dll` is missing, the plugin simply won't register the corresponding functions, so that they won't be visible by VapourSynth. For manual compilation, the `_magick.dll` consists of source files in the `magick` folder.
