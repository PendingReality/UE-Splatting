// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GaussianDataTypes.h"

/**
 * Source coordinate basis of an SPZ file's stored data.
 *
 * Niantic/Spark SPZ is nominally RUB (right/up/back, OpenGL/three.js). Standard
 * 3DGS PLY (which UESplatting already handles) is RDF (right/down/forward, COLMAP).
 * The reader rebases into the SAME UE convention FPLYFileReader produces so an
 * .spz co-locates with a PLY twin. The exact basis of a given .spz is not always
 * advertised, so this is selectable. RUB is the format-spec default and the path
 * used by the shared decoder registry; MatchPly remains available for known RDF
 * exports.
 */
enum class ESpzCoordinateBasis : uint8
{
	/** Treat SPZ values as if already in the PLY/COLMAP (RDF) source convention. */
	MatchPly,
	/** Treat SPZ values as RUB (OpenGL) and convert to RDF before the PLY->UE transform. */
	RUB,
};

/**
 * Reader for SPZ Gaussian-splat files (Niantic / Spark legacy v1-v3, gzip-stream).
 *
 * Decodes into FGaussianSplatData in the SAME UE-space convention FPLYFileReader
 * produces (UE cm, left-handed Z-up, linear opacity, linear scale, normalized quat,
 * raw SH_DC, SH coefficients per-coefficient) so an .spz lines up with a PLY twin.
 *
 * NOTE: current Niantic SPZ v4 (plaintext header + ZSTD streams) is NOT handled here;
 * only the gzip v1-v3 legacy stream. Degree-4 SH is rejected (UESplatting supports <= 3).
 */
class UESPLATTING_API FSpzFileReader
{
public:
	static bool ReadSpzFile(
		const FString& FilePath,
		TArray<FGaussianSplatData>& OutSplats,
		FString& OutError,
		int32* OutSHBands = nullptr,
		ESpzCoordinateBasis SourceBasis = ESpzCoordinateBasis::RUB);

	/** Cheap sniff: gzip magic (1f 8b). Full validation happens in ReadSpzFile. */
	static bool IsValidSpzFile(const FString& FilePath);
};
