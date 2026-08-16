# Development

UESplatting currently targets Unreal Engine 5.8. The tested development platform is
Win64 with D3D12.

## Build

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" UESplattingDemoEditor Win64 Development -Project="<repo>\UESplattingDemo.uproject" -WaitMutex
```

## Automation Tests

Open **Tools > Test Automation** or Session Frontend and run the
`UESplatting.*` tests. The core suite covers decoder routing and numeric
conversion. With UESplatting Capture enabled, the same prefix also covers
capture-volume layouts, export metadata, and Movie Render Queue camera-pose
sequencing.

The Movie Render Queue test needs a rendering-capable editor process. Decoder
and metadata tests can run without opening the demo map manually.

## Package The Plugin

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<repo>\Plugins\UESplatting\UESplatting.uplugin" -Package="<output>\UESplatting" -TargetPlatforms=Win64
```

Package the optional Capture plugin with the core descriptor as a dependency:

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<repo>\Plugins\UESplattingCapture\UESplattingCapture.uplugin" -Package="<output>\UESplattingCapture" -TargetPlatforms=Win64 -Dependencies="<repo>\Plugins\UESplatting\UESplatting.uplugin"
```

## Source Modules

| Plugin | Module | Type |
| --- | --- | --- |
| UESplatting | `UESplatting` | Runtime |
| UESplatting | `UESplattingEditor` | Editor |
| UESplatting Capture | `UESplattingCapture` | Editor |

These modules were originally named `NanoGS` and `NanoGSEditor`. They were
renamed so the plugin no longer collides with an upstream NanoGaussianSplatting
install in the same project, which previously produced a duplicate module name
at build time.

Capture types also dropped the `NanoGS` prefix and the `Colmap` infix, so the
Room Coverage actor is now `AUESplattingCaptureVolume`. COLMAP is one of several
output formats, not what the tool is. Core types such as `UGaussianSplatAsset`
and `AGaussianSplatActor` kept their names; only their script package moved.

A fresh install needs no migration. The plugins ship redirects for assets and
levels created before the module rename:

- `UESplatting/Config/DefaultUESplatting.ini` redirects the runtime script
  package.
- `UESplattingCapture/Config/DefaultUESplattingCapture.ini` redirects the
  capture script package and renamed reflected types.

Projects using the packaged plugins receive the same redirects; no
project-level config block is required.

## Demo Fixture

Regenerate the synthetic sample with:

```powershell
python Scripts/generate_demo_splat.py
```

The generated PLY contains 37,782 splats. `Scripts/create_demo_content.py` can
be run through Unreal's Python plugin to recreate the imported asset and map;
Python is a development-time helper and is not a UESplatting dependency.
