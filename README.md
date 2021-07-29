# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

---

## Usage

```python
iccc.ICCConvert(clip, simulation_icc, display_icc, soft_proofing=True, simulation_intent='relative', display_intent='perceptual', gamut_warning=False, black_point_compensation=False)
```
Intended for soft proofing.

Input `clip` must have 8-bit or 16-bit RGB format. The output has the same format.

`simulation_icc` is the path to the ICC profile to simulate.

`display_icc` is the path to the ICC profile for your monitor. In Windows default to the profile used for the current window.

`soft_proofing` is the flag for soft proofing. Default on. Disable to apply simple conversion, otherwise it further takes a backward step for the proofing. [Here](https://sourceforge.net/p/lcms/mailman/message/36783703/) is a brief introduction about how Little CMS handles it.

`simulation_intent` and `display_intent` are corresponding to the ICC rendering intents for simulation and for display, respectively, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
 - "perceptual" for Perceptual (default for display)
 - "saturation" for Saturation
 - "relative"   for Relative Colorimetric (default for simulation)
 - "absolute"   for Absolute Colorimetric

If not in soft proofing mode, only `simulation_intent` will be taken.

`gamut_warning` is the flag for out-of-gamut warning. A certain color will fill the overflowing region. Default off.

`black_point_compensation` is the flag for what it tells. Default off.

```python
iccc.ICCPlayback(clip, display_icc, playback_csp='709', intent='perceptual')
```
One-way gamut mapping for video playback with BT.1886 configuration.

Currently supported `playback_csp` options are `'709'` (HD), `'601-525'` (SMPTE-C) and `'601-625'` (PAL).

However, you may also set it as `'srgb'` for images.

---

## Compilation

In Mingw-w64:
- Install `mingw-w64-x86_64-lcms2`
- `git clone https://github.com/YomikoR/VapourSynth-ICCConvert --recursive && cd VapourSynth-ICCConvert/src`
- `gcc icc_detection.c -O2 -c -o icc_detection.o`
- `g++ iccc.cc 1886.cc icc_detection.o libp2p/p2p_api.cpp libp2p/v210.cpp -O2 -static -shared -llcms2 -lgdi32 -I. -I/path/to/VapourSynth/include -o libiccc.dll`

---

## License

LGPL 2.1 (due to BT.1886 playback)
