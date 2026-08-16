// SPDX-License-Identifier: MIT

#include "GaussianSplatDecoder.h"
#include "PLYFileReader.h"
#include "SpzFileReader.h"
#include "CompressedPlyReader.h"
#include "Misc/Paths.h"

namespace
{
	/**
	 * PLY decoder: thin wrapper over the existing streamed FPLYFileReader, which
	 * already performs the canonical RDF->UE normalization and SH-band detection.
	 */
	class FPlySplatDecoder : public IGaussianSplatDecoder
	{
	public:
		virtual TArray<FString> GetSupportedExtensions() const override
		{
			return { TEXT("ply") };
		}

		virtual bool SupportsFile(const FString& FilePath) const override
		{
			return FPaths::GetExtension(FilePath).Equals(TEXT("ply"), ESearchCase::IgnoreCase)
				&& FPLYFileReader::IsValidPLYFile(FilePath)
				&& !FCompressedPlyReader::IsCompressedPly(FilePath);   // compressed PLY routes to its own decoder
		}

		virtual bool Decode(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError) const override
		{
			int32 DetectedSHBands = 0;
			const bool bOk = FPLYFileReader::ReadPLYFile(FilePath, OutResult.Splats, OutError, &DetectedSHBands);
			OutResult.SHBands = DetectedSHBands;
			return bOk;
		}

		virtual const TCHAR* GetDecoderName() const override
		{
			return TEXT("PLY");
		}
	};

	/**
	 * SPZ decoder: Niantic / Spark legacy gzip v1-v3 (see FSpzFileReader).
	 * v4/ZSTD and SH degree 4 are rejected with an error rather than mis-decoded.
	 */
	class FSpzSplatDecoder : public IGaussianSplatDecoder
	{
	public:
		virtual TArray<FString> GetSupportedExtensions() const override
		{
			return { TEXT("spz") };
		}

		virtual bool SupportsFile(const FString& FilePath) const override
		{
			return FPaths::GetExtension(FilePath).Equals(TEXT("spz"), ESearchCase::IgnoreCase)
				&& FSpzFileReader::IsValidSpzFile(FilePath);
		}

		virtual bool Decode(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError) const override
		{
			int32 DetectedSHBands = 0;
			const bool bOk = FSpzFileReader::ReadSpzFile(FilePath, OutResult.Splats, OutError, &DetectedSHBands);
			OutResult.SHBands = DetectedSHBands;
			return bOk;
		}

		virtual const TCHAR* GetDecoderName() const override
		{
			return TEXT("SPZ");
		}
	};

	/**
	 * Compressed PLY decoder: PlayCanvas / SuperSplat / splat-transform chunk+packed layout
	 * (see FCompressedPlyReader). Shares the .ply extension with standard PLY; claims a file
	 * only when the header declares the packed layout.
	 */
	class FCompressedPlySplatDecoder : public IGaussianSplatDecoder
	{
	public:
		virtual TArray<FString> GetSupportedExtensions() const override
		{
			return { TEXT("ply") };
		}

		virtual bool SupportsFile(const FString& FilePath) const override
		{
			return FPaths::GetExtension(FilePath).Equals(TEXT("ply"), ESearchCase::IgnoreCase)
				&& FCompressedPlyReader::IsCompressedPly(FilePath);
		}

		virtual bool Decode(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError) const override
		{
			int32 DetectedSHBands = 0;
			const bool bOk = FCompressedPlyReader::ReadCompressedPly(FilePath, OutResult.Splats, OutError, &DetectedSHBands);
			OutResult.SHBands = DetectedSHBands;
			return bOk;
		}

		virtual const TCHAR* GetDecoderName() const override
		{
			return TEXT("CompressedPLY");
		}
	};
}

const TArray<TUniquePtr<IGaussianSplatDecoder>>& FGaussianSplatDecoderRegistry::GetDecoders()
{
	static const TArray<TUniquePtr<IGaussianSplatDecoder>> Decoders = []
	{
		TArray<TUniquePtr<IGaussianSplatDecoder>> Result;
		Result.Add(MakeUnique<FPlySplatDecoder>());
		Result.Add(MakeUnique<FCompressedPlySplatDecoder>());
		Result.Add(MakeUnique<FSpzSplatDecoder>());
		return Result;
	}();
	return Decoders;
}

bool FGaussianSplatDecoderRegistry::DecodeFile(const FString& FilePath, FSplatDecodeResult& OutResult, FString& OutError)
{
	OutResult = FSplatDecodeResult();
	OutError.Empty();

	for (const TUniquePtr<IGaussianSplatDecoder>& Decoder : GetDecoders())
	{
		if (Decoder->SupportsFile(FilePath))
		{
			const bool bOk = Decoder->Decode(FilePath, OutResult, OutError);
			if (!bOk && OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s decoder failed to decode '%s'"), Decoder->GetDecoderName(), *FilePath);
			}
			return bOk;
		}
	}

	OutError = FString::Printf(TEXT("No splat decoder supports file: %s"), *FilePath);
	return false;
}

bool FGaussianSplatDecoderRegistry::CanDecodeFile(const FString& FilePath)
{
	for (const TUniquePtr<IGaussianSplatDecoder>& Decoder : GetDecoders())
	{
		if (Decoder->SupportsFile(FilePath))
		{
			return true;
		}
	}
	return false;
}

TArray<FString> FGaussianSplatDecoderRegistry::GetAllSupportedExtensions()
{
	TArray<FString> Extensions;
	for (const TUniquePtr<IGaussianSplatDecoder>& Decoder : GetDecoders())
	{
		for (const FString& Ext : Decoder->GetSupportedExtensions())
		{
			Extensions.AddUnique(Ext);
		}
	}
	return Extensions;
}
