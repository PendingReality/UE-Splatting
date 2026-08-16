# Supported Formats

UESplatting uses one decoder registry for editor import, reimport, Cluster LOD source
reads, and runtime loading.

| Format | Support | Notes |
| --- | --- | --- |
| Standard 3DGS binary PLY | Yes | Standard Gaussian properties, SH degree 0-3 |
| PlayCanvas/SuperSplat compressed PLY | Yes | Packed chunk and vertex layout, optional packed SH |
| Legacy gzip SPZ v1-v3 | Yes | SH degree 0-3 |
| SPZ v4/Zstandard | No | Requires a different container and format path |
| `.splat`, `.ksplat`, `.sog` | No | Not implemented |
| Dynamic or 4D Gaussian formats | No | No animation format is defined yet |

## Coordinates And Units

Standard 3DGS PLY input is interpreted in the common COLMAP/OpenCV basis:
X right, Y down, and Z forward, with positions and scales in meters. UESplatting
converts it to Unreal's X forward, Y right, Z up basis in centimeters.

Compressed PLY and SPZ input are normalized to the same Unreal convention so
equivalent captures can be placed together.

Coordinate conventions are not always recorded by exporters. Check one known
camera or orientation before importing a large collection from a new source.

## Invalid Or Unsupported Files

UESplatting validates file structure as well as extension. Invalid headers,
truncated payloads, unsupported SPZ versions, and unknown layouts return an
import or runtime-load error.
