// SPDX-License-Identifier: MIT

#include "SpzFileReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	constexpr uint32 SPZ_MAGIC = 0x5053474e;   // "NGSP" little-endian
	constexpr int32  SPZ_HEADER_SIZE = 16;

	int32 SHCoeffsPerChannel(int32 ShDegree)
	{
		switch (ShDegree) { case 1: return 3; case 2: return 8; case 3: return 15; default: return 0; }
	}

	FORCEINLINE uint32 ReadSpzU32LE(const uint8* P)
	{
		return (uint32)P[0] | ((uint32)P[1] << 8) | ((uint32)P[2] << 16) | ((uint32)P[3] << 24);
	}

	// Signed 24-bit little-endian fixed-point -> float (v2/v3 positions).
	FORCEINLINE float DequantPos24(const uint8* P, int32 FracBits)
	{
		int32 V = (int32)P[0] | ((int32)P[1] << 8) | ((int32)P[2] << 16);
		if (V & 0x00800000) { V |= (int32)0xFF000000; }   // sign-extend 24 -> 32
		return (float)V / FMath::Pow(2.0f, (float)FracBits);
	}

	FORCEINLINE float DequantHalf(const uint8* P)
	{
		FFloat16 H; H.Encoded = (uint16)((uint16)P[0] | ((uint16)P[1] << 8));
		return H.GetFloat();
	}

	// SH-coefficient sign flip for the SPZ RUB->RDF basis change (180 deg about X).
	// Applied per coefficient to all RGB channels (NOT to SH_DC, the rotation-invariant
	// DC term). Verified vs Niantic splat-types.h convertCoordinates flipSh (x=+,y=-,z=-).
	// Index c matches UESplatting SH[c] / PLY f_rest coefficient order.
	// c:                                     0   1   2    3   4   5   6   7    8   9  10  11  12  13  14
	static const float SpzRubToRdfShSign[15] = { -1, -1,  1,  -1,  1,  1, -1,  1,  -1,  1, -1, -1,  1, -1,  1 };
}

bool FSpzFileReader::IsValidSpzFile(const FString& FilePath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenRead(*FilePath));
	if (!FileHandle) { return false; }
	uint8 Magic[2];
	if (!FileHandle->Read(Magic, 2)) { return false; }
	return Magic[0] == 0x1f && Magic[1] == 0x8b;   // gzip
}

bool FSpzFileReader::ReadSpzFile(const FString& FilePath, TArray<FGaussianSplatData>& OutSplats,
	FString& OutError, int32* OutSHBands, ESpzCoordinateBasis SourceBasis)
{
	OutSplats.Empty();
	OutError.Empty();
	if (OutSHBands) { *OutSHBands = 0; }

	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to open SPZ file: %s"), *FilePath);
		return false;
	}
	if (Compressed.Num() < 18 || Compressed[0] != 0x1f || Compressed[1] != 0x8b)
	{
		OutError = TEXT("Not a gzip-compressed SPZ stream (missing gzip magic)");
		return false;
	}

	// gzip trailer ISIZE = uncompressed size mod 2^32 (exact for < 4 GiB payloads).
	const uint32 ISize = ReadSpzU32LE(&Compressed[Compressed.Num() - 4]);
	if (ISize < (uint32)SPZ_HEADER_SIZE)
	{
		OutError = TEXT("SPZ gzip trailer reports an implausibly small uncompressed size");
		return false;
	}
	if (ISize > (uint32)MAX_int32)
	{
		OutError = TEXT("SPZ payload exceeds UESplatting's 2 GiB in-memory decoder limit");
		return false;
	}

	TArray<uint8> Raw;
	Raw.SetNumUninitialized((int32)ISize);
	if (!FCompression::UncompressMemory(NAME_Gzip, Raw.GetData(), Raw.Num(), Compressed.GetData(), Compressed.Num()))
	{
		OutError = TEXT("Failed to gzip-inflate SPZ (payload may exceed 4 GiB, or NAME_Gzip unsupported)");
		return false;
	}

	const uint8* Data = Raw.GetData();
	const uint32 Magic = ReadSpzU32LE(Data);
	if (Magic != SPZ_MAGIC)
	{
		OutError = FString::Printf(TEXT("Bad SPZ magic (expected NGSP, got 0x%08X)"), Magic);
		return false;
	}

	const uint32 Version   = ReadSpzU32LE(Data + 4);
	const uint32 NumSplats = ReadSpzU32LE(Data + 8);
	const int32  ShDegree  = (int32)Data[12];
	const int32  FracBits  = (int32)Data[13];
	// Data[14] = flags (bit0 antialiased, bit7 LOD); Data[15] = reserved.

	if (Version < 1 || Version > 3)
	{
		OutError = FString::Printf(TEXT("Unsupported SPZ version %u (this reader handles legacy gzip v1-3; v4/ZSTD is not supported)"), Version);
		return false;
	}
	if (ShDegree < 0 || ShDegree > 3)
	{
		OutError = FString::Printf(TEXT("Unsupported SPZ SH degree %d (UESplatting supports 0-3)"), ShDegree);
		return false;
	}
	if (NumSplats == 0)
	{
		OutError = TEXT("SPZ reports zero splats");
		return false;
	}
	if (NumSplats > (uint32)MAX_int32)
	{
		OutError = TEXT("SPZ splat count exceeds UESplatting's array limit");
		return false;
	}
	if (Version >= 2 && FracBits > 30)
	{
		OutError = FString::Printf(TEXT("Invalid SPZ fractional-bit count %d (expected 0-30)"), FracBits);
		return false;
	}

	const int32 CoeffsPerCh = SHCoeffsPerChannel(ShDegree);

	// SoA block layout/order: positions, alphas, colors, scales, rotations, SH.
	const int32 PosBytesPer = (Version == 1) ? 6 : 9;     // v1 = float16x3 ; v2/v3 = int24x3
	const int32 RotBytesPer = (Version == 3) ? 4 : 3;     // v3 = packed32 ; v1/v2 = int8x3
	const int64 PosOff   = SPZ_HEADER_SIZE;
	const int64 AlphaOff = PosOff   + (int64)NumSplats * PosBytesPer;
	const int64 ColorOff = AlphaOff + (int64)NumSplats * 1;
	const int64 ScaleOff = ColorOff + (int64)NumSplats * 3;
	const int64 RotOff   = ScaleOff + (int64)NumSplats * 3;
	const int64 ShOff    = RotOff   + (int64)NumSplats * RotBytesPer;
	const int64 Expected = ShOff    + (int64)NumSplats * CoeffsPerCh * 3;

	if ((int64)Raw.Num() < Expected)
	{
		OutError = FString::Printf(TEXT("SPZ payload truncated: need %lld bytes, have %d"), Expected, Raw.Num());
		return false;
	}

	if (OutSHBands) { *OutSHBands = ShDegree; }

	const float kInvSqrt2  = 0.70710678118654752440f;
	const float DcInvScale = 1.0f / 0.15f;   // raw SH_DC = (b/255 - 0.5) / 0.15
	constexpr float MetersToUE = 100.0f;

	OutSplats.SetNum((int32)NumSplats);

	for (uint32 i = 0; i < NumSplats; ++i)
	{
		FGaussianSplatData& Splat = OutSplats[(int32)i];

		// ---- Position (SPZ source space, metres) ----
		float Px, Py, Pz;
		const uint8* PosP = Data + PosOff + (int64)i * PosBytesPer;
		if (Version == 1)
		{
			Px = DequantHalf(PosP + 0); Py = DequantHalf(PosP + 2); Pz = DequantHalf(PosP + 4);
		}
		else
		{
			Px = DequantPos24(PosP + 0, FracBits);
			Py = DequantPos24(PosP + 3, FracBits);
			Pz = DequantPos24(PosP + 6, FracBits);
		}

		// ---- Scale (SPZ byte -> linear scale, metres) ----
		const uint8* ScP = Data + ScaleOff + (int64)i * 3;
		float Sx = FMath::Exp((float)ScP[0] / 16.0f - 10.0f);
		float Sy = FMath::Exp((float)ScP[1] / 16.0f - 10.0f);
		float Sz = FMath::Exp((float)ScP[2] / 16.0f - 10.0f);

		// ---- Rotation (x,y,z,w) in SPZ source space ----
		float Qx, Qy, Qz, Qw;
		const uint8* RotP = Data + RotOff + (int64)i * RotBytesPer;
		if (Version == 3)
		{
			// "smallest three": [2-bit largest index | 3 x (9-bit magnitude + 1-bit sign)].
			const uint32 Packed  = ReadSpzU32LE(RotP);
			const uint32 Largest = (Packed >> 30) & 0x3u;
			float Q[4] = { 0, 0, 0, 0 };
			float SumSq = 0.0f;
			uint32 Bits = Packed;   // low 30 bits hold the three stored comps, least-significant first
			// Spark/Niantic fill components from index 3 down to 0 (skipping the largest),
			// reading the 10-bit fields LSB-first (verified vs spz.ts 196-211). Match exactly.
			for (int32 idx = 3; idx >= 0; --idx)
			{
				if ((uint32)idx == Largest) { continue; }
				const uint32 Field = Bits & 0x3FFu;        // 10 bits per stored comp
				Bits >>= 10;
				const uint32 Mag  = Field & 0x1FFu;        // low 9 bits magnitude
				const uint32 Sign = (Field >> 9) & 0x1u;   // high bit sign
				float V = kInvSqrt2 * ((float)Mag / 511.0f);
				if (Sign) { V = -V; }
				Q[idx] = V; SumSq += V * V;
			}
			Q[Largest] = FMath::Sqrt(FMath::Max(0.0f, 1.0f - SumSq));
			Qx = Q[0]; Qy = Q[1]; Qz = Q[2]; Qw = Q[3];
		}
		else
		{
			Qx = (float)RotP[0] / 127.5f - 1.0f;
			Qy = (float)RotP[1] / 127.5f - 1.0f;
			Qz = (float)RotP[2] / 127.5f - 1.0f;
			Qw = FMath::Sqrt(FMath::Max(0.0f, 1.0f - (Qx * Qx + Qy * Qy + Qz * Qz)));
		}

		// ---- Optional RUB -> RDF rebase so the PLY transform applies (negate Y, Z) ----
		if (SourceBasis == ESpzCoordinateBasis::RUB)
		{
			Py = -Py; Pz = -Pz;
			Qy = -Qy; Qz = -Qz;   // 180-deg about X
		}

		// ---- Apply the SAME RDF->UE transform FPLYFileReader uses ----
		Splat.Position.X =  Pz * MetersToUE;
		Splat.Position.Y =  Px * MetersToUE;
		Splat.Position.Z = -Py * MetersToUE;

		Splat.Rotation.W =  Qw;
		Splat.Rotation.X = -Qz;
		Splat.Rotation.Y = -Qx;
		Splat.Rotation.Z =  Qy;
		Splat.Rotation = GaussianSplattingUtils::NormalizeQuat(Splat.Rotation);

		// Scale reorder (Z,X,Y) like PLY, metres -> cm. SPZ scale is already linear (no exp here).
		Splat.Scale.X = Sz * MetersToUE;
		Splat.Scale.Y = Sx * MetersToUE;
		Splat.Scale.Z = Sy * MetersToUE;

		// ---- Opacity (already linear; do NOT sigmoid) ----
		Splat.Opacity = (float)Data[AlphaOff + (int64)i] / 255.0f;

		// ---- DC colour -> raw SH_DC ----
		const uint8* ColP = Data + ColorOff + (int64)i * 3;
		Splat.SH_DC.X = ((float)ColP[0] / 255.0f - 0.5f) * DcInvScale;
		Splat.SH_DC.Y = ((float)ColP[1] / 255.0f - 0.5f) * DcInvScale;
		Splat.SH_DC.Z = ((float)ColP[2] / 255.0f - 0.5f) * DcInvScale;

		// ---- Higher SH (coefficient-major, RGB inner): sint8 (b-128)/128 ----
		// For the RUB basis we also apply the per-coefficient SH sign flip so view-dependent
		// colour matches the RDF/PLY basis (degree-0 files like 1920-s.spz have no higher SH).
		for (int32 c = 0; c < GaussianSplattingConstants::NumSHCoefficients; ++c)
		{
			if (c < CoeffsPerCh)
			{
				const uint8* ShP = Data + ShOff + ((int64)i * CoeffsPerCh + c) * 3;
				const float Flip = (SourceBasis == ESpzCoordinateBasis::RUB) ? SpzRubToRdfShSign[c] : 1.0f;
				Splat.SH[c].X = (((float)ShP[0] - 128.0f) / 128.0f) * Flip;
				Splat.SH[c].Y = (((float)ShP[1] - 128.0f) / 128.0f) * Flip;
				Splat.SH[c].Z = (((float)ShP[2] - 128.0f) / 128.0f) * Flip;
			}
			else
			{
				Splat.SH[c] = FVector3f::ZeroVector;
			}
		}
	}

	return true;
}
