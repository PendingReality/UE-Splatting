# UESplatting

UESplatting is an Unreal Engine 5.8 source plugin for importing, loading, and
rendering 3D Gaussian splats. Its renderer is derived from
[TimChen1383/NanoGaussianSplatting](https://github.com/TimChen1383/NanoGaussianSplatting)
and distributed under the included MIT License.

## Supported Formats

- Standard binary 3DGS PLY
- PlayCanvas/SuperSplat compressed PLY
- Legacy gzip SPZ v1-v3, SH degree 0-3

SPZ v4/Zstandard and `.splat`, `.ksplat`, and `.sog` files are not currently
supported.

## Installation

Copy this directory to `<YourProject>/Plugins/UESplatting`, regenerate project files
if requested, and build the UE 5.8 Editor target.

To import a splat, drag a supported file into Content Browser and place the
resulting asset in a level.

## Runtime Loading

**Gaussian Splat Actor** exposes a runtime file path and asynchronous load
actions. The same API is available through `UGaussianSplatComponent` in
Blueprint and C++.

## Optional Scene Capture

Install the sibling `UESplattingCapture` plugin to add the Beta Room Coverage,
Directional Array, and Focused Detail dataset-export tools. Movie Render
Pipeline is only required by that optional plugin.

## Attribution

See `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md` in this directory.
