# UESplatting Capture

UESplatting Capture is the optional Beta editor companion for
[UESplatting](../UESplatting). It exports known-pose perspective images,
Nerfstudio transforms, and COLMAP metadata for downstream Gaussian-splat
training. An experimental collision-derived seed cloud is available but is not
part of the required capture contract.

Capture layouts include omnidirectional Room Coverage, a forward-facing
Directional Array for controlled translated-baseline tests, and inward-facing
Focused Detail for bounded subjects.

Enable **UESplatting Capture** in **Edit > Plugins** when dataset export is
needed. The plugin requires UESplatting and Unreal's Movie Render Pipeline;
neither dependency is required to import, render, or load splats at runtime.

See the repository [Scene Capture guide](../../docs/SCENE-CAPTURE-BETA.md) for the
normal workflow and output contract.
