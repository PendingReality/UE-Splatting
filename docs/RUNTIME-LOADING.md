# Runtime Loading

`AGaussianSplatActor` contains a `UGaussianSplatComponent` and can load any
format registered by UESplatting.

## Actor Setup

1. Place a **Gaussian Splat Actor** in the level.
2. Set **Runtime Splat File** to a file available on the target machine.
3. Enable **Load Runtime Splat On Begin Play**, or call
   **Load Runtime Splat** from Blueprint.
4. Bind the success and failure delegates if other game logic needs the result.

## Blueprint And C++ API

Call `LoadSplatFromFileAsync(FilePath)` on a `UGaussianSplatComponent` to start
a load. `CancelRuntimeSplatLoad()` invalidates the active request. Starting a
new request also invalidates the previous one.

File decoding runs on Unreal's thread pool. UObject creation, asset
initialization, and component assignment run on the game thread.

The component exposes:

- `OnRuntimeSplatLoadSucceeded`, with the transient asset and splat count.
- `OnRuntimeSplatLoadFailed`, with an error message.

## Files In Packaged Builds

An editor path is not automatically available in a packaged game. Splat files
must be staged as runtime dependencies, shipped beside the application, or
downloaded to a writable directory.

The included demo stages `samples/Data/UESplatting_Demo.ply` as a NonUFS runtime
dependency. Its build rules show one way to package an external splat file.

Large files require memory for decoded CPU data, the transient Unreal asset,
and renderer buffers. Decoding is asynchronous, but final asset creation can
still cause a game-thread hitch for very large captures.
