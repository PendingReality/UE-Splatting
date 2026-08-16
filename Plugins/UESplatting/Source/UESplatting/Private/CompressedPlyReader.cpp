// SPDX-License-Identifier: MIT

#include "CompressedPlyReader.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	constexpr int32 CP_CHUNK_SIZE = 256;   // splats per chunk (PlayCanvas/splat-transform)

	struct FCpElement
	{
		FString Name;
		int64   Count = 0;
		int32   Stride = 0;
		int64   DataOffset = 0;            // absolute byte offset of this element's data
		TMap<FString, int32> PropOffsets;  // property name -> byte offset within a row
		TMap<FString, int32> PropSizes;    // property name -> scalar byte size
		TMap<FString, FString> PropTypes;  // property name -> PLY scalar type
	};

	int32 PlyTypeSize(const FString& T)
	{
		if (T == TEXT("float") || T == TEXT("float32") || T == TEXT("int") || T == TEXT("int32")
			|| T == TEXT("uint") || T == TEXT("uint32") || T == TEXT("uint_32")) return 4;
		if (T == TEXT("double") || T == TEXT("float64")) return 8;
		if (T == TEXT("uchar") || T == TEXT("uint8") || T == TEXT("char") || T == TEXT("int8")) return 1;
		if (T == TEXT("short") || T == TEXT("int16") || T == TEXT("ushort") || T == TEXT("uint16")) return 2;
		return 0;
	}

	FORCEINLINE uint32 ReadU32LE(const uint8* P)
	{
		return (uint32)P[0] | ((uint32)P[1] << 8) | ((uint32)P[2] << 16) | ((uint32)P[3] << 24);
	}

	FORCEINLINE float ReadF32(const uint8* P)
	{
		float F; FMemory::Memcpy(&F, P, sizeof(float)); return F;
	}

	// 11/10/11 unorm: x=bits31..21, y=bits20..11, z=bits10..0 -> normalized [0,1].
	FORCEINLINE void Unpack111011(uint32 V, float& X, float& Y, float& Z)
	{
		X = (float)(V >> 21) / 2047.0f;
		Y = (float)((V >> 11) & 0x3FFu) / 1023.0f;
		Z = (float)(V & 0x7FFu) / 2047.0f;
	}

	// Reads the ASCII PLY header, parses elements/properties/strides and assigns each
	// element its absolute data offset (elements are stored in header order).
	bool ParseCompressedHeader(const TArray<uint8>& Bytes, TArray<FCpElement>& OutElements, FString& OutError)
	{
		OutElements.Reset();
		OutError.Reset();
		const int32 MaxHeader = FMath::Min(Bytes.Num(), 65536);
		const char* EndMarker = "end_header";
		const int32 MarkerLen = 10;
		int32 HeaderEnd = -1;
		for (int32 i = 0; i <= MaxHeader - MarkerLen; ++i)
		{
			if (FMemory::Memcmp(&Bytes[i], EndMarker, MarkerLen) == 0)
			{
				for (int32 j = i + MarkerLen; j < MaxHeader; ++j)
				{
					if (Bytes[j] == '\n') { HeaderEnd = j + 1; break; }
				}
				break;
			}
		}
		if (HeaderEnd < 0) { OutError = TEXT("Compressed PLY: end_header not found"); return false; }

		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), HeaderEnd);
		FString HeaderStr(Conv.Length(), Conv.Get());

		TArray<FString> Lines;
		HeaderStr.ParseIntoArrayLines(Lines);

		bool bFoundPly = false;
		bool bFoundFormat = false;
		FCpElement Current;
		bool bHaveCurrent = false;

		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line == TEXT("ply")) { bFoundPly = true; continue; }
			if (Line.StartsWith(TEXT("format")))
			{
				if (Line.Contains(TEXT("binary_little_endian"))) { bFoundFormat = true; }
				else if (Line.Contains(TEXT("binary_big_endian"))) { OutError = TEXT("Compressed PLY: big-endian not supported"); return false; }
				else if (Line.Contains(TEXT("ascii"))) { OutError = TEXT("Compressed PLY: ascii not supported"); return false; }
				continue;
			}
			if (Line.StartsWith(TEXT("element")))
			{
				if (bHaveCurrent) { OutElements.Add(Current); }
				Current = FCpElement();
				bHaveCurrent = true;
				TArray<FString> P; Line.ParseIntoArray(P, TEXT(" "));
				if (P.Num() != 3 || P[1].IsEmpty() || !P[2].IsNumeric())
				{
					OutError = FString::Printf(TEXT("Compressed PLY: malformed element declaration '%s'"), *Line);
					return false;
				}
				Current.Name = P[1];
				Current.Count = FCString::Atoi64(*P[2]);
				if (Current.Count < 0)
				{
					OutError = FString::Printf(TEXT("Compressed PLY: negative element count for '%s'"), *Current.Name);
					return false;
				}
				continue;
			}
			if (bHaveCurrent && Line.StartsWith(TEXT("property")))
			{
				TArray<FString> P; Line.ParseIntoArray(P, TEXT(" "));
				if (P.Num() >= 2 && P[1] == TEXT("list"))
				{
					OutError = TEXT("Compressed PLY: list properties are not supported by the packed format");
					return false;
				}
				if (P.Num() != 3)
				{
					OutError = FString::Printf(TEXT("Compressed PLY: malformed property declaration '%s'"), *Line);
					return false;
				}
				const int32 Size = PlyTypeSize(P[1]);
				if (Size <= 0)
				{
					OutError = FString::Printf(TEXT("Compressed PLY: unsupported property type '%s'"), *P[1]);
					return false;
				}
				const FString& PropertyName = P[2];
				if (Current.PropOffsets.Contains(PropertyName) || Current.Stride > MAX_int32 - Size)
				{
					OutError = FString::Printf(TEXT("Compressed PLY: duplicate property or row too large: '%s'"), *PropertyName);
					return false;
				}
				Current.PropOffsets.Add(PropertyName, Current.Stride);
				Current.PropSizes.Add(PropertyName, Size);
				Current.PropTypes.Add(PropertyName, P[1]);
				Current.Stride += Size;
				continue;
			}
		}
		if (bHaveCurrent) { OutElements.Add(Current); }

		if (!bFoundPly) { OutError = TEXT("Compressed PLY: missing 'ply' magic"); return false; }
		if (!bFoundFormat) { OutError = TEXT("Compressed PLY: missing binary_little_endian format declaration"); return false; }
		if (OutElements.IsEmpty()) { OutError = TEXT("Compressed PLY: no elements declared"); return false; }

		// Assign absolute data offsets in header order.
		int64 Cursor = HeaderEnd;
		for (FCpElement& E : OutElements)
		{
			if (E.Count > 0 && E.Stride <= 0)
			{
				OutError = FString::Printf(TEXT("Compressed PLY: element '%s' has no scalar row layout"), *E.Name);
				return false;
			}
			if (E.Stride > 0 && E.Count > (MAX_int64 - Cursor) / E.Stride)
			{
				OutError = FString::Printf(TEXT("Compressed PLY: element '%s' byte range overflows"), *E.Name);
				return false;
			}
			E.DataOffset = Cursor;
			Cursor += E.Count * E.Stride;
		}
		if (Cursor > Bytes.Num())
		{
			OutError = FString::Printf(TEXT("Compressed PLY truncated: header implies %lld bytes, file is %d"), Cursor, Bytes.Num());
			return false;
		}
		return true;
	}

	bool HasScalarSize(const FCpElement& Element, const TCHAR* PropertyName, int32 ExpectedSize)
	{
		const int32* Size = Element.PropSizes.Find(PropertyName);
		return Size && *Size == ExpectedSize;
	}

	bool HasScalarType(const FCpElement& Element, const TCHAR* PropertyName, std::initializer_list<const TCHAR*> AcceptedTypes)
	{
		const FString* Type = Element.PropTypes.Find(PropertyName);
		if (!Type)
		{
			return false;
		}
		for (const TCHAR* AcceptedType : AcceptedTypes)
		{
			if (*Type == AcceptedType)
			{
				return true;
			}
		}
		return false;
	}

	const FCpElement* FindElement(const TArray<FCpElement>& Elems, const TCHAR* Name)
	{
		for (const FCpElement& E : Elems) { if (E.Name == Name) { return &E; } }
		return nullptr;
	}

	FORCEINLINE float ChunkFloat(const uint8* Data, const FCpElement& Chunk, int64 ChunkIdx, const FString& Prop, float Default)
	{
		const int32* Off = Chunk.PropOffsets.Find(Prop);
		if (!Off) { return Default; }
		return ReadF32(Data + Chunk.DataOffset + ChunkIdx * Chunk.Stride + *Off);
	}
}

bool FCompressedPlyReader::IsCompressedPly(const FString& FilePath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(PlatformFile.OpenRead(*FilePath));
	if (!Handle) { return false; }
	const int32 Probe = (int32)FMath::Min<int64>(Handle->Size(), 4096);
	TArray<uint8> Buf; Buf.SetNumUninitialized(Probe);
	if (!Handle->Read(Buf.GetData(), Probe)) { return false; }
	// Must be a PLY whose header declares the compressed packed layout.
	if (!(Probe >= 3 && Buf[0] == 'p' && Buf[1] == 'l' && Buf[2] == 'y')) { return false; }
	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Buf.GetData()), Probe);
	FString Head(Conv.Length(), Conv.Get());
	return Head.Contains(TEXT("packed_position"));
}

bool FCompressedPlyReader::ReadCompressedPly(const FString& FilePath, TArray<FGaussianSplatData>& OutSplats,
	FString& OutError, int32* OutSHBands)
{
	OutSplats.Empty();
	OutError.Empty();
	if (OutSHBands) { *OutSHBands = 0; }

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to open compressed PLY: %s"), *FilePath);
		return false;
	}

	TArray<FCpElement> Elements;
	if (!ParseCompressedHeader(Bytes, Elements, OutError)) { return false; }

	const FCpElement* Chunk  = FindElement(Elements, TEXT("chunk"));
	const FCpElement* Vertex = FindElement(Elements, TEXT("vertex"));
	const FCpElement* Sh      = FindElement(Elements, TEXT("sh"));
	if (!Chunk || !Vertex)
	{
		OutError = TEXT("Compressed PLY missing 'chunk' or 'vertex' element");
		return false;
	}
	if (Vertex->Count <= 0 || Vertex->Count > MAX_int32)
	{
		OutError = FString::Printf(TEXT("Compressed PLY has invalid vertex count: %lld"), Vertex->Count);
		return false;
	}
	const int64 RequiredChunkCount = (Vertex->Count + CP_CHUNK_SIZE - 1) / CP_CHUNK_SIZE;
	if (Chunk->Count < RequiredChunkCount)
	{
		OutError = FString::Printf(TEXT("Compressed PLY needs at least %lld chunks for %lld vertices, but declares %lld"),
			RequiredChunkCount, Vertex->Count, Chunk->Count);
		return false;
	}

	const int32* OffPos = Vertex->PropOffsets.Find(TEXT("packed_position"));
	const int32* OffRot = Vertex->PropOffsets.Find(TEXT("packed_rotation"));
	const int32* OffScl = Vertex->PropOffsets.Find(TEXT("packed_scale"));
	const int32* OffCol = Vertex->PropOffsets.Find(TEXT("packed_color"));
	if (!OffPos || !OffRot || !OffScl || !OffCol)
	{
		OutError = TEXT("Compressed PLY vertex element missing a packed_* property");
		return false;
	}
	if (!HasScalarSize(*Vertex, TEXT("packed_position"), 4)
		|| !HasScalarSize(*Vertex, TEXT("packed_rotation"), 4)
		|| !HasScalarSize(*Vertex, TEXT("packed_scale"), 4)
		|| !HasScalarSize(*Vertex, TEXT("packed_color"), 4)
		|| !HasScalarType(*Vertex, TEXT("packed_position"), { TEXT("uint"), TEXT("uint32"), TEXT("uint_32") })
		|| !HasScalarType(*Vertex, TEXT("packed_rotation"), { TEXT("uint"), TEXT("uint32"), TEXT("uint_32") })
		|| !HasScalarType(*Vertex, TEXT("packed_scale"), { TEXT("uint"), TEXT("uint32"), TEXT("uint_32") })
		|| !HasScalarType(*Vertex, TEXT("packed_color"), { TEXT("uint"), TEXT("uint32"), TEXT("uint_32") }))
	{
		OutError = TEXT("Compressed PLY packed_* vertex properties must be 32-bit scalars");
		return false;
	}

	static const TCHAR* RequiredChunkFloatProperties[] = {
		TEXT("min_x"), TEXT("max_x"), TEXT("min_y"), TEXT("max_y"), TEXT("min_z"), TEXT("max_z"),
		TEXT("min_scale_x"), TEXT("max_scale_x"), TEXT("min_scale_y"), TEXT("max_scale_y"),
		TEXT("min_scale_z"), TEXT("max_scale_z")
	};
	for (const TCHAR* PropertyName : RequiredChunkFloatProperties)
	{
		if (!HasScalarSize(*Chunk, PropertyName, 4)
			|| !HasScalarType(*Chunk, PropertyName, { TEXT("float"), TEXT("float32") }))
		{
			OutError = FString::Printf(TEXT("Compressed PLY chunk element is missing 32-bit property '%s'"), PropertyName);
			return false;
		}
	}

	const bool bHasColorBounds = Chunk->PropOffsets.Contains(TEXT("min_r"))
		|| Chunk->PropOffsets.Contains(TEXT("max_r"))
		|| Chunk->PropOffsets.Contains(TEXT("min_g"))
		|| Chunk->PropOffsets.Contains(TEXT("max_g"))
		|| Chunk->PropOffsets.Contains(TEXT("min_b"))
		|| Chunk->PropOffsets.Contains(TEXT("max_b"));
	if (bHasColorBounds)
	{
		static const TCHAR* ColorBounds[] = {
			TEXT("min_r"), TEXT("max_r"), TEXT("min_g"), TEXT("max_g"), TEXT("min_b"), TEXT("max_b")
		};
		for (const TCHAR* PropertyName : ColorBounds)
		{
			if (!HasScalarSize(*Chunk, PropertyName, 4)
				|| !HasScalarType(*Chunk, PropertyName, { TEXT("float"), TEXT("float32") }))
			{
				OutError = FString::Printf(TEXT("Compressed PLY has incomplete color bounds; missing '%s'"), PropertyName);
				return false;
			}
		}
	}

	// Optional SH element: f_rest_0.. (channel-major), 9/24/45 columns -> degree 1/2/3.
	int32 ShCols = 0;
	if (Sh)
	{
		if (Sh->Count != Vertex->Count)
		{
			OutError = FString::Printf(TEXT("Compressed PLY SH row count (%lld) does not match vertex count (%lld)"), Sh->Count, Vertex->Count);
			return false;
		}
		while (Sh->PropOffsets.Contains(FString::Printf(TEXT("f_rest_%d"), ShCols))) { ++ShCols; }
		if (ShCols != 9 && ShCols != 24 && ShCols != 45)
		{
			OutError = FString::Printf(TEXT("Compressed PLY SH element has unsupported coefficient column count %d"), ShCols);
			return false;
		}
		for (int32 Column = 0; Column < ShCols; ++Column)
		{
			const FString PropertyName = FString::Printf(TEXT("f_rest_%d"), Column);
			if (!HasScalarSize(*Sh, *PropertyName, 1)
				|| !HasScalarType(*Sh, *PropertyName, { TEXT("uchar"), TEXT("uint8") }))
			{
				OutError = FString::Printf(TEXT("Compressed PLY SH property '%s' must be an 8-bit scalar"), *PropertyName);
				return false;
			}
		}
	}
	const int32 ShBands = (ShCols >= 45) ? 3 : (ShCols >= 24) ? 2 : (ShCols >= 9) ? 1 : 0;
	const int32 ShCoeffsPerCh = (ShBands == 1) ? 3 : (ShBands == 2) ? 8 : (ShBands == 3) ? 15 : 0;
	if (OutSHBands) { *OutSHBands = ShBands; }

	const int64 NumSplats = Vertex->Count;
	OutSplats.SetNum((int32)NumSplats);

	const uint8* Data = Bytes.GetData();
	const float kSqrt2 = 1.41421356237309504880f;
	const float SH_C0  = GaussianSplattingConstants::SH_C0;
	constexpr float MetersToUE = 100.0f;

	for (int64 i = 0; i < NumSplats; ++i)
	{
		FGaussianSplatData& Splat = OutSplats[(int32)i];
		const int64 ChunkIdx = i / CP_CHUNK_SIZE;
		const uint8* Row = Data + Vertex->DataOffset + i * Vertex->Stride;

		// ---- Position: 11/10/11 unorm lerped within chunk min/max (PLY/RDF metres) ----
		float nx, ny, nz;
		Unpack111011(ReadU32LE(Row + *OffPos), nx, ny, nz);
		const float Px = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_x"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_x"), 0.f), nx);
		const float Py = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_y"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_y"), 0.f), ny);
		const float Pz = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_z"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_z"), 0.f), nz);

		// ---- Scale: 11/10/11 unorm lerped within chunk log-scale min/max ----
		float sxn, syn, szn;
		Unpack111011(ReadU32LE(Row + *OffScl), sxn, syn, szn);
		const float Ls0 = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_scale_x"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_scale_x"), 0.f), sxn);
		const float Ls1 = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_scale_y"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_scale_y"), 0.f), syn);
		const float Ls2 = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_scale_z"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_scale_z"), 0.f), szn);

		// ---- Color: RGBA8 ; RGB lerped to display colour -> raw SH_DC ; A = linear alpha ----
		const uint32 PackedCol = ReadU32LE(Row + *OffCol);
		const float Tr = (float)((PackedCol >> 24) & 0xFF) / 255.0f;
		const float Tg = (float)((PackedCol >> 16) & 0xFF) / 255.0f;
		const float Tb = (float)((PackedCol >> 8)  & 0xFF) / 255.0f;
		const float Ta = (float)(PackedCol & 0xFF) / 255.0f;
		float Cr = Tr, Cg = Tg, Cb = Tb;
		if (bHasColorBounds)
		{
			Cr = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_r"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_r"), 1.f), Tr);
			Cg = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_g"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_g"), 1.f), Tg);
			Cb = FMath::Lerp(ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("min_b"), 0.f), ChunkFloat(Data, *Chunk, ChunkIdx, TEXT("max_b"), 1.f), Tb);
		}

		// ---- Rotation: 2-bit largest index + 3 x 10-bit biased unorm (smallest three) ----
		// Slots map to rot_0..3 = (W, X, Y, Z) per PlayCanvas convention.
		const uint32 PackedRot = ReadU32LE(Row + *OffRot);
		const uint32 Which = PackedRot >> 30;
		const float A = ((float)((PackedRot >> 20) & 0x3FFu) / 1023.0f - 0.5f) * kSqrt2;
		const float B = ((float)((PackedRot >> 10) & 0x3FFu) / 1023.0f - 0.5f) * kSqrt2;
		const float C = ((float)(PackedRot & 0x3FFu) / 1023.0f - 0.5f) * kSqrt2;
		const float M = FMath::Sqrt(FMath::Max(0.0f, 1.0f - (A * A + B * B + C * C)));
		float R[4];
		switch (Which)
		{
			case 0: R[0] = M; R[1] = A; R[2] = B; R[3] = C; break;
			case 1: R[0] = A; R[1] = M; R[2] = B; R[3] = C; break;
			case 2: R[0] = A; R[1] = B; R[2] = M; R[3] = C; break;
			default: R[0] = A; R[1] = B; R[2] = C; R[3] = M; break;
		}
		const float QW = R[0], QX = R[1], QY = R[2], QZ = R[3];  // rot_0..3 = W,X,Y,Z

		// ---- Emit FGaussianSplatData in the SAME UE convention as standard PLY ----
		Splat.Position.X =  Pz * MetersToUE;
		Splat.Position.Y =  Px * MetersToUE;
		Splat.Position.Z = -Py * MetersToUE;

		Splat.Rotation.W =  QW;
		Splat.Rotation.X = -QZ;
		Splat.Rotation.Y = -QX;
		Splat.Rotation.Z =  QY;
		Splat.Rotation = GaussianSplattingUtils::NormalizeQuat(Splat.Rotation);

		// log scale_0/1/2 -> reorder (Z,X,Y), exp(), metres->cm (matches FPLYFileReader)
		Splat.Scale.X = FMath::Exp(Ls2) * MetersToUE;
		Splat.Scale.Y = FMath::Exp(Ls0) * MetersToUE;
		Splat.Scale.Z = FMath::Exp(Ls1) * MetersToUE;

		Splat.Opacity = Ta;   // already linear alpha

		// display colour -> raw SH DC
		Splat.SH_DC.X = (Cr - 0.5f) / SH_C0;
		Splat.SH_DC.Y = (Cg - 0.5f) / SH_C0;
		Splat.SH_DC.Z = (Cb - 0.5f) / SH_C0;

		// ---- Optional higher SH (uint8 f_rest, channel-major planar like standard PLY) ----
		for (int32 c = 0; c < GaussianSplattingConstants::NumSHCoefficients; ++c)
		{
			if (Sh && c < ShCoeffsPerCh)
			{
				const uint8* ShRow = Data + Sh->DataOffset + i * Sh->Stride;
				auto Decode = [&](int32 Col) -> float
				{
					const int32* Off = Sh->PropOffsets.Find(FString::Printf(TEXT("f_rest_%d"), Col));
					if (!Off) { return 0.0f; }
					const uint8 Src = ShRow[*Off];
					const float N = (Src == 0) ? 0.0f : (Src == 255) ? 1.0f : ((float)Src + 0.5f) / 256.0f;
					return (N - 0.5f) * 8.0f;
				};
				Splat.SH[c].X = Decode(c);
				Splat.SH[c].Y = Decode(c + ShCoeffsPerCh);
				Splat.SH[c].Z = Decode(c + ShCoeffsPerCh * 2);
			}
			else
			{
				Splat.SH[c] = FVector3f::ZeroVector;
			}
		}
	}

	return true;
}
