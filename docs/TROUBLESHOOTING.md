# Troubleshooting

## Plugin Or Module Does Not Load

- Confirm the project uses Unreal Engine 5.8.
- Confirm a supported C++ compiler is installed.
- Remove the project's generated `Plugins/UESplatting/Binaries` and
  `Plugins/UESplatting/Intermediate` directories, then rebuild.
- Rebuild plugin binaries when changing engine installations.

## Global Shader Compilation Reports Too Many UAVs

Current UESplatting shaders support the SM5 eight-UAV limit. An error reporting nine
UAVs usually means the project contains an older plugin copy or stale generated
shader data. Update the plugin, clear its generated build data, and rebuild.

## A Splat Does Not Appear

- Check the Output Log for import or renderer errors.
- Confirm the asset reports a nonzero splat count.
- Frame the selected actor in the viewport.
- Check actor visibility and scale.
- If Cluster LOD is enabled, temporarily disable it to isolate the base asset.

## A Splat Is Misoriented

The exporter may use a different coordinate basis from COLMAP/OpenCV. Compare
the asset with a known camera or a matching file from the same capture before
applying a permanent corrective transform.

## Runtime Loading Fails

- Read the `OnRuntimeSplatLoadFailed` message.
- Confirm the file exists on the target machine.
- Confirm the file format and version are supported.
- Ensure packaged builds stage or download the source file.

## Capture Is Washed Out Or Too Dark

Assign the scene's reference camera or post-process volume and run
**Render MRQ Test Frame**. Check exposure, local exposure, white balance, color
grading, emissives, bloom, and volumetrics in that frame.

## Capture Takes Too Long Or Uses Too Much Disk

Probe count grows approximately with the inverse square of probe spacing.
Halving spacing creates about four times as many room positions for the same
height bands. Use the estimate in the Details panel before export and use
Focused Detail for localized coverage.

## Renderer Uses Too Much GPU Memory

`gs.MaxRenderBudget` limits the global working splat count. A positive value can
reduce memory use by dropping farther splats when the budget is exceeded.

## Export Refuses The Output Directory

Dataset export requires an empty destination so existing images cannot be mixed
with the new camera metadata. Choose a new directory or move the previous
capture elsewhere.
