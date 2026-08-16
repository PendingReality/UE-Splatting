// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GaussianDataTypes.h"

/**
 * Result of decoding a splat file into the engine-canonical CPU representation.
 *
 * Every decoder MUST output Splats already normalized into the SAME convention the
 * PLY path uses (see FPLYFileReader::LinearizeSplatData and the RDF->UE transform):
 * UE centimetres, left-handed Z-up, linear opacity [0,1], linear scale, normalized
 * quaternion, SH coefficients in the PLY/RDF coefficient basis. This guarantees an
 * .spz import co-locates with its PLY twin.
 */
struct FSplatDecodeResult
{
	/** Per-splat data in UESplatting's canonical Unreal representation. */
	TArray<FGaussianSplatData> Splats;

	/**
	 * Detected SH band count (0-3). MUST be assigned to UGaussianSplatAsset::SHBands
	 * before InitializeFromSplatData(), because CompressSH() reads it.
	 */
	int32 SHBands = 0;
};

/**
 * Interface for a per-format Gaussian-splat decoder (PLY, SPZ, ...).
 *
 * Decoders live in the runtime module so the editor factory and runtime loading
 * path use the same normalization and format dispatch.
 */
class UESPLATTING_API IGaussianSplatDecoder
{
public:
	virtual ~IGaussianSplatDecoder() = default;

	/** Lowercase file extensions this decoder handles, without the dot (e.g. {"ply"}). */
	virtual TArray<FString> GetSupportedExtensions() const = 0;

	/** Cheap claim check: extension match plus an optional magic-byte sniff. */
	virtual bool SupportsFile(const FString& FilePath) const = 0;

	/** Decode the file into OutResult (UE-normalized). Returns false and sets OutError on failure. */
	virtual bool Decode(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError) const = 0;

	/** Human-readable decoder name, for logs. */
	virtual const TCHAR* GetDecoderName() const = 0;
};

/**
 * Registry / dispatcher for splat-file decoders.
 *
 * Single entry point for ALL ingestion: factory import, reimport, and the
 * Cluster LOD rebuild/clear paths must route through DecodeFile() so that
 * non-PLY formats survive every code path that re-reads the source file.
 */
class UESPLATTING_API FGaussianSplatDecoderRegistry
{
public:
	/** Decode any supported splat file, dispatching to the matching decoder. */
	static bool DecodeFile(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError);

	/** True if any registered decoder claims the file. */
	static bool CanDecodeFile(const FString& FilePath);

	/** All supported extensions across decoders (lowercase, no dot) - for UFactory Formats. */
	static TArray<FString> GetAllSupportedExtensions();

private:
	/** Lazily-built decoder list (main-thread / editor import use). */
	static const TArray<TUniquePtr<IGaussianSplatDecoder>>& GetDecoders();
};
