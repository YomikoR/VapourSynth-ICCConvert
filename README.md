# VapourSynth-ICCConvert

Little CMS based ICC profile simulation for VapourSynth.

## Usage

```python
iccc.ICCConvert(clip, simulate_icc, display_icc, intent='absolute')
```

Input `clip` must have 8-bit or 16-bit RGB format. The output has the same format.

`simulate_icc` is the path to the ICC profile to simulate (e.g. Rec. 709).

`display_icc` is the path to the ICC profile for your monitor.

`intent` refers to the ICC rendering intent, see [this link](https://helpx.adobe.com/photoshop-elements/kb/color-management-settings-best-print.html#main-pars_header_1). Possible options are
 - "perceptual" for Perceptual
 - "saturation" for Saturation
 - "relative"   for Relative Colorimetric
 - "absolute"   for Absolute Colorimetric (default)

## License

MIT
