# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

## Usage

```python
iccc.ICCConvert(clip, simulation_icc, display_icc, simulation_intent='relative', display_intent='perceptual', gamut_warning=False)
```

Input `clip` must have 8-bit or 16-bit RGB format. The output has the same format.

`simulation_icc` is the path to the ICC profile to simulate (e.g. Rec. 709).

`display_icc` is the path to the ICC profile for your monitor.

`simulation_intent` and `display_intent` are corresponding to the ICC rendering intents for simulation and for display, respectively, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
 - "perceptual" for Perceptual (default for display)
 - "saturation" for Saturation
 - "relative"   for Relative Colorimetric (default for simulation)
 - "absolute"   for Absolute Colorimetric

`gamut_warning` is the flag for out-of-gamut warning. Default off.

## License

MIT
