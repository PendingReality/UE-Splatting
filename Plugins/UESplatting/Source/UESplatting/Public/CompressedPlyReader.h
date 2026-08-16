// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GaussianDataTypes.h"

/**
 * Reader for the "compressed PLY" Gaussian-splat format produced by PlayCanvas /
 * SuperSplat / splat-transform: an `element chunk` block (256 splats/chunk, per-chunk
 * min/max bounds) plus an `element vertex` block of packed uint32s
 * (packed_position / packed_rotation / packed_scale / packed_color), and an optional
 * `element sh` block of uint8 f_rest coefficients.
 *
 * Shares the `.ply` extension with standard 3DGS PLY; callers dispatch by header sniff
 * (IsCompressedPly). Coordinate basis is PLY/RDF (same as standard PLY), so the decoded
 * FGaussianSplatData uses the identical RDF->UE transform - an imported compressed PLY
 * co-locates with a standard PLY twin.
 */
class UESPLATTING_API FCompressedPlyReader
{
public:
	static bool ReadCompressedPly(
		const FString& FilePath,
		TArray<FGaussianSplatData>& OutSplats,
		FString& OutError,
		int32* OutSHBands = nullptr);

	/** True if the file is a PLY whose header declares the compressed (chunk/packed) layout. */
	static bool IsCompressedPly(const FString& FilePath);
};
