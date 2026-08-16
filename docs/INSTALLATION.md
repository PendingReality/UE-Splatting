# Installation

## Requirements

- Unreal Engine 5.8
- A C++ compiler supported by that engine installation
- A C++ Unreal project

UESplatting is distributed as source and is compiled for each project.

## Add UESplatting To A Project

1. Copy `Plugins/UESplatting` into `<YourProject>/Plugins/UESplatting`.
2. Open the project or regenerate project files if Unreal requests it.
3. Build the project's Editor target.
4. In Unreal Editor, confirm **UESplatting** is enabled under
   **Edit > Plugins > Rendering**.

Unreal generates `Binaries` and `Intermediate` for the receiving project. They
do not need to be copied from another installation.

## Build The Demo Project

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" UESplattingDemoEditor Win64 Development -Project="<repo>\UESplattingDemo.uproject" -WaitMutex
```

Open `UESplattingDemo.uproject` after the build completes. The default map
contains the generated sample splat.

## Package The Plugin

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<repo>\Plugins\UESplatting\UESplatting.uplugin" -Package="<output>\UESplatting" -TargetPlatforms=Win64
```

Install the packaged output by placing its `UESplatting` directory under the target
project's `Plugins` directory.

## Optional Capture Tools

Copy `Plugins/UESplattingCapture` beside the core plugin and enable
**UESplatting Capture** when known-pose dataset export is needed. It depends on
UESplatting and Unreal's Movie Render Pipeline. The core importer, renderer, and
runtime loader do not require Movie Render Pipeline.

To package the optional plugin from this repository, pass the core descriptor
as a dependency:

```powershell
& "<UE_ROOT>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<repo>\Plugins\UESplattingCapture\UESplattingCapture.uplugin" -Package="<output>\UESplattingCapture" -TargetPlatforms=Win64 -Dependencies="<repo>\Plugins\UESplatting\UESplatting.uplugin"
```
