// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "CompressedPlyReader.h"
#include "GaussianSplatDecoder.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	void AppendFloat32(TArray<uint8>& Bytes, float Value)
	{
		const int32 Offset = Bytes.AddUninitialized(sizeof(float));
		FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(float));
	}

	void AppendUInt32LE(TArray<uint8>& Bytes, uint32 Value)
	{
		Bytes.Add(static_cast<uint8>(Value));
		Bytes.Add(static_cast<uint8>(Value >> 8));
		Bytes.Add(static_cast<uint8>(Value >> 16));
		Bytes.Add(static_cast<uint8>(Value >> 24));
	}

	void AppendInt24LE(TArray<uint8>& Bytes, int32 Value)
	{
		const uint32 Encoded = static_cast<uint32>(Value) & 0x00FFFFFFu;
		Bytes.Add(static_cast<uint8>(Encoded));
		Bytes.Add(static_cast<uint8>(Encoded >> 8));
		Bytes.Add(static_cast<uint8>(Encoded >> 16));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingDecoderRegistryTest,
	"UESplatting.Ingestion.DecoderRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUESplattingDecoderRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FString> Extensions = FGaussianSplatDecoderRegistry::GetAllSupportedExtensions();
	TestTrue(TEXT("PLY is registered"), Extensions.Contains(TEXT("ply")));
	TestTrue(TEXT("SPZ is registered"), Extensions.Contains(TEXT("spz")));

	FSplatDecodeResult Result;
	Result.Splats.AddDefaulted();
	Result.SHBands = 3;
	FString Error = TEXT("stale error");
	const bool bDecoded = FGaussianSplatDecoderRegistry::DecodeFile(
		FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("does-not-exist.unsupported")),
		Result,
		Error);

	TestFalse(TEXT("Unsupported file is rejected"), bDecoded);
	TestTrue(TEXT("Failed decode clears splat output"), Result.Splats.IsEmpty());
	TestEqual(TEXT("Failed decode resets SH bands"), Result.SHBands, 0);
	TestTrue(TEXT("Failed decode returns a useful error"), Error.Contains(TEXT("No splat decoder")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingLegacySpzDecodeTest,
	"UESplatting.Ingestion.LegacySpzV3Decode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUESplattingLegacySpzDecodeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<uint8> Raw;
	AppendUInt32LE(Raw, 0x5053474e);
	AppendUInt32LE(Raw, 3);
	AppendUInt32LE(Raw, 1);
	Raw.Add(0); // SH degree
	Raw.Add(10); // fractional bits
	Raw.Add(0); // flags
	Raw.Add(0); // reserved
	AppendInt24LE(Raw, 1024);
	AppendInt24LE(Raw, -2048);
	AppendInt24LE(Raw, 3072);
	Raw.Add(255); // alpha
	Raw.Add(128);
	Raw.Add(128);
	Raw.Add(128);
	Raw.Add(96);
	Raw.Add(96);
	Raw.Add(96);
	AppendUInt32LE(Raw, 0xC0000000u); // identity quaternion; W is largest

	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Gzip, Raw.Num());
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(CompressedSize);
	TestTrue(TEXT("Legacy SPZ fixture gzip compression succeeds"),
		FCompression::CompressMemory(NAME_Gzip, Compressed.GetData(), CompressedSize, Raw.GetData(), Raw.Num()));
	Compressed.SetNum(CompressedSize);

	const FString FixturePath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("UESplattingLegacySpz"), TEXT(".spz"));
	TestTrue(TEXT("Legacy SPZ fixture was written"), FFileHelper::SaveArrayToFile(Compressed, *FixturePath));

	FSplatDecodeResult Result;
	FString Error;
	TestTrue(TEXT("Legacy SPZ decodes through registry"), FGaussianSplatDecoderRegistry::DecodeFile(FixturePath, Result, Error));
	TestTrue(TEXT("Legacy SPZ decode has no error"), Error.IsEmpty());
	TestEqual(TEXT("Legacy SPZ contains one splat"), Result.Splats.Num(), 1);
	TestEqual(TEXT("Legacy SPZ fixture is degree zero"), Result.SHBands, 0);

	if (Result.Splats.Num() == 1)
	{
		const FGaussianSplatData& Splat = Result.Splats[0];
		TestTrue(TEXT("SPZ RUB coordinates normalize to UE centimetres"),
			Splat.Position.Equals(FVector3f(-300.0f, 100.0f, -200.0f), 0.001f));
		const float ExpectedScale = FMath::Exp(-4.0f) * 100.0f;
		TestTrue(TEXT("SPZ byte scale dequantizes to linear centimetres"),
			Splat.Scale.Equals(FVector3f(ExpectedScale, ExpectedScale, ExpectedScale), 0.001f));
		TestTrue(TEXT("SPZ alpha remains linear"), FMath::IsNearlyEqual(Splat.Opacity, 1.0f));
		TestTrue(TEXT("SPZ identity rotation remains normalized"),
			FMath::IsNearlyEqual(Splat.Rotation.W, 1.0f)
			&& FMath::IsNearlyZero(Splat.Rotation.X)
			&& FMath::IsNearlyZero(Splat.Rotation.Y)
			&& FMath::IsNearlyZero(Splat.Rotation.Z));
	}

	IFileManager::Get().Delete(*FixturePath, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingStandardPlyDecodeTest,
	"UESplatting.Ingestion.StandardPlyDecode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUESplattingStandardPlyDecodeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString FixturePath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("UESplattingStandardPly"), TEXT(".ply"));
	const FString Header =
		TEXT("ply\n")
		TEXT("format binary_little_endian 1.0\n")
		TEXT("element vertex 1\n")
		TEXT("property float x\nproperty float y\nproperty float z\n")
		TEXT("property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n")
		TEXT("property float opacity\n")
		TEXT("property float scale_0\nproperty float scale_1\nproperty float scale_2\n")
		TEXT("property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n")
		TEXT("end_header\n");

	FTCHARToUTF8 Utf8(*Header);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	AppendFloat32(Bytes, 1.0f);
	AppendFloat32(Bytes, 2.0f);
	AppendFloat32(Bytes, 3.0f);
	AppendFloat32(Bytes, 0.1f);
	AppendFloat32(Bytes, -0.2f);
	AppendFloat32(Bytes, 0.3f);
	AppendFloat32(Bytes, 0.0f);
	AppendFloat32(Bytes, FMath::Loge(0.01f));
	AppendFloat32(Bytes, FMath::Loge(0.02f));
	AppendFloat32(Bytes, FMath::Loge(0.03f));
	AppendFloat32(Bytes, 1.0f);
	AppendFloat32(Bytes, 0.0f);
	AppendFloat32(Bytes, 0.0f);
	AppendFloat32(Bytes, 0.0f);
	TestTrue(TEXT("Standard PLY fixture was written"), FFileHelper::SaveArrayToFile(Bytes, *FixturePath));

	FSplatDecodeResult Result;
	FString Error;
	TestTrue(TEXT("Standard PLY decodes through registry"), FGaussianSplatDecoderRegistry::DecodeFile(FixturePath, Result, Error));
	TestTrue(TEXT("Standard PLY decode has no error"), Error.IsEmpty());
	TestEqual(TEXT("Standard PLY contains one splat"), Result.Splats.Num(), 1);
	TestEqual(TEXT("Standard PLY without f_rest is degree zero"), Result.SHBands, 0);

	if (Result.Splats.Num() == 1)
	{
		const FGaussianSplatData& Splat = Result.Splats[0];
		TestTrue(TEXT("Position is converted from RDF metres to UE centimetres"),
			Splat.Position.Equals(FVector3f(300.0f, 100.0f, -200.0f), 0.001f));
		TestTrue(TEXT("Scale is exponentiated, converted to centimetres, and reordered"),
			Splat.Scale.Equals(FVector3f(3.0f, 1.0f, 2.0f), 0.001f));
		TestTrue(TEXT("Opacity logit zero becomes one half"), FMath::IsNearlyEqual(Splat.Opacity, 0.5f));
		TestTrue(TEXT("Identity rotation remains normalized"),
			FMath::IsNearlyEqual(Splat.Rotation.W, 1.0f)
			&& FMath::IsNearlyZero(Splat.Rotation.X)
			&& FMath::IsNearlyZero(Splat.Rotation.Y)
			&& FMath::IsNearlyZero(Splat.Rotation.Z));
		TestTrue(TEXT("SH DC values survive normalization"),
			Splat.SH_DC.Equals(FVector3f(0.1f, -0.2f, 0.3f), 0.0001f));
	}

	IFileManager::Get().Delete(*FixturePath, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCompressedPlyDecodeTest,
	"UESplatting.Ingestion.CompressedPlyDecode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUESplattingCompressedPlyDecodeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString FixturePath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("UESplattingCompressedPly"), TEXT(".ply"));
	const FString Header =
		TEXT("ply\n")
		TEXT("format binary_little_endian 1.0\n")
		TEXT("element chunk 1\n")
		TEXT("property float min_x\nproperty float max_x\n")
		TEXT("property float min_y\nproperty float max_y\n")
		TEXT("property float min_z\nproperty float max_z\n")
		TEXT("property float min_scale_x\nproperty float max_scale_x\n")
		TEXT("property float min_scale_y\nproperty float max_scale_y\n")
		TEXT("property float min_scale_z\nproperty float max_scale_z\n")
		TEXT("element vertex 1\n")
		TEXT("property uint packed_position\n")
		TEXT("property uint packed_rotation\n")
		TEXT("property uint packed_scale\n")
		TEXT("property uint packed_color\n")
		TEXT("end_header\n");

	FTCHARToUTF8 Utf8(*Header);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	AppendFloat32(Bytes, 1.0f);
	AppendFloat32(Bytes, 1.0f);
	AppendFloat32(Bytes, 2.0f);
	AppendFloat32(Bytes, 2.0f);
	AppendFloat32(Bytes, 3.0f);
	AppendFloat32(Bytes, 3.0f);
	AppendFloat32(Bytes, FMath::Loge(0.01f));
	AppendFloat32(Bytes, FMath::Loge(0.01f));
	AppendFloat32(Bytes, FMath::Loge(0.02f));
	AppendFloat32(Bytes, FMath::Loge(0.02f));
	AppendFloat32(Bytes, FMath::Loge(0.03f));
	AppendFloat32(Bytes, FMath::Loge(0.03f));
	AppendUInt32LE(Bytes, 0u);
	AppendUInt32LE(Bytes, (512u << 20) | (512u << 10) | 512u);
	AppendUInt32LE(Bytes, 0u);
	AppendUInt32LE(Bytes, (64u << 24) | (128u << 16) | (192u << 8) | 255u);
	TestTrue(TEXT("Compressed PLY fixture was written"), FFileHelper::SaveArrayToFile(Bytes, *FixturePath));
	TestTrue(TEXT("Compressed PLY signature is detected"), FCompressedPlyReader::IsCompressedPly(FixturePath));

	FSplatDecodeResult Result;
	FString Error;
	TestTrue(TEXT("Compressed PLY decodes through registry"), FGaussianSplatDecoderRegistry::DecodeFile(FixturePath, Result, Error));
	TestTrue(TEXT("Compressed PLY decode has no error"), Error.IsEmpty());
	TestEqual(TEXT("Compressed PLY contains one splat"), Result.Splats.Num(), 1);
	TestEqual(TEXT("Compressed PLY without SH element is degree zero"), Result.SHBands, 0);

	if (Result.Splats.Num() == 1)
	{
		const FGaussianSplatData& Splat = Result.Splats[0];
		TestTrue(TEXT("Packed position converts from RDF metres to UE centimetres"),
			Splat.Position.Equals(FVector3f(300.0f, 100.0f, -200.0f), 0.001f));
		TestTrue(TEXT("Packed log scale converts to reordered linear centimetres"),
			Splat.Scale.Equals(FVector3f(3.0f, 1.0f, 2.0f), 0.001f));
		TestTrue(TEXT("Packed alpha remains linear"), FMath::IsNearlyEqual(Splat.Opacity, 1.0f));
		TestTrue(TEXT("Packed identity quaternion remains normalized"),
			FMath::IsNearlyEqual(Splat.Rotation.SizeSquared(), 1.0f, 0.0001f)
			&& Splat.Rotation.W > 0.999f);

		const float SH_C0 = GaussianSplattingConstants::SH_C0;
		const FVector3f ExpectedDC(
			(64.0f / 255.0f - 0.5f) / SH_C0,
			(128.0f / 255.0f - 0.5f) / SH_C0,
			(192.0f / 255.0f - 0.5f) / SH_C0);
		TestTrue(TEXT("Packed display color converts to raw SH DC"),
			Splat.SH_DC.Equals(ExpectedDC, 0.0001f));
	}

	IFileManager::Get().Delete(*FixturePath, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingMalformedCompressedPlyTest,
	"UESplatting.Ingestion.MalformedCompressedPly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUESplattingMalformedCompressedPlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString UnknownTypePath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("UESplattingUnknownType"), TEXT(".ply"));
	const FString UnknownTypeHeader =
		TEXT("ply\n")
		TEXT("format binary_little_endian 1.0\n")
		TEXT("element vertex 1\n")
		TEXT("property octet packed_position\n")
		TEXT("end_header\n");
	TestTrue(TEXT("Unknown-type fixture was written"), FFileHelper::SaveStringToFile(UnknownTypeHeader, *UnknownTypePath));

	TArray<FGaussianSplatData> Splats;
	FString Error;
	int32 SHBands = 3;
	TestFalse(
		TEXT("Unknown packed PLY property type is rejected"),
		FCompressedPlyReader::ReadCompressedPly(UnknownTypePath, Splats, Error, &SHBands));
	TestTrue(TEXT("Unknown type error is descriptive"), Error.Contains(TEXT("unsupported property type")));
	TestTrue(TEXT("Unknown type failure leaves no splats"), Splats.IsEmpty());
	TestEqual(TEXT("Unknown type failure resets SH bands"), SHBands, 0);

	const FString MissingChunkPath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("UESplattingMissingChunk"), TEXT(".ply"));
	const FString MissingChunkHeader =
		TEXT("ply\n")
		TEXT("format binary_little_endian 1.0\n")
		TEXT("element chunk 0\n")
		TEXT("property float min_x\nproperty float max_x\n")
		TEXT("property float min_y\nproperty float max_y\n")
		TEXT("property float min_z\nproperty float max_z\n")
		TEXT("property float min_scale_x\nproperty float max_scale_x\n")
		TEXT("property float min_scale_y\nproperty float max_scale_y\n")
		TEXT("property float min_scale_z\nproperty float max_scale_z\n")
		TEXT("element vertex 1\n")
		TEXT("property uint packed_position\n")
		TEXT("property uint packed_rotation\n")
		TEXT("property uint packed_scale\n")
		TEXT("property uint packed_color\n")
		TEXT("end_header\n");

	FTCHARToUTF8 Utf8(*MissingChunkHeader);
	TArray<uint8> MissingChunkBytes;
	MissingChunkBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	MissingChunkBytes.AddZeroed(16);
	TestTrue(TEXT("Missing-chunk fixture was written"), FFileHelper::SaveArrayToFile(MissingChunkBytes, *MissingChunkPath));

	Error.Empty();
	TestFalse(
		TEXT("Packed PLY without enough chunks is rejected"),
		FCompressedPlyReader::ReadCompressedPly(MissingChunkPath, Splats, Error, &SHBands));
	TestTrue(TEXT("Missing chunk error is descriptive"), Error.Contains(TEXT("needs at least")));

	IFileManager::Get().Delete(*UnknownTypePath, false, true, true);
	IFileManager::Get().Delete(*MissingChunkPath, false, true, true);
	return true;
}

#endif
