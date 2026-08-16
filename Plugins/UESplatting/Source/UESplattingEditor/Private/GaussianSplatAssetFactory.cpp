// SPDX-License-Identifier: MIT

#include "GaussianSplatAssetFactory.h"
#include "UESplattingLog.h"
#include "GaussianSplatAsset.h"
#include "GaussianSplatDecoder.h"
#include "PLYFileReader.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"

UGaussianSplatAssetFactory::UGaussianSplatAssetFactory()
{
	bCreateNew = false;
	bEditorImport = true;
	bText = false;

	SupportedClass = UGaussianSplatAsset::StaticClass();

	// Supported import formats are driven by the decoder registry, so adding a new
	// format means registering one decoder (see FGaussianSplatDecoderRegistry).
	for (const FString& Ext : FGaussianSplatDecoderRegistry::GetAllSupportedExtensions())
	{
		Formats.Add(FString::Printf(TEXT("%s;Gaussian Splat (.%s)"), *Ext, *Ext));
	}
}

bool UGaussianSplatAssetFactory::FactoryCanImport(const FString& Filename)
{
	return FGaussianSplatDecoderRegistry::CanDecodeFile(Filename);
}

UObject* UGaussianSplatAssetFactory::FactoryCreateFile(
	UClass* InClass,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	const FString& Filename,
	const TCHAR* Parms,
	FFeedbackContext* Warn,
	bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	UGaussianSplatAsset* NewAsset = ImportPLYFile(Filename, InParent, InName, Flags, nullptr);

	if (!NewAsset)
	{
		if (Warn)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to import Gaussian Splat from: %s"), *Filename);
		}
	}

	return NewAsset;
}

FText UGaussianSplatAssetFactory::GetDisplayName() const
{
	return FText::FromString(TEXT("Gaussian Splat Asset"));
}

bool UGaussianSplatAssetFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UGaussianSplatAsset* Asset = Cast<UGaussianSplatAsset>(Obj);
	if (Asset && !Asset->SourceFilePath.IsEmpty())
	{
		OutFilenames.Add(Asset->SourceFilePath);
		return true;
	}
	return false;
}

void UGaussianSplatAssetFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UGaussianSplatAsset* Asset = Cast<UGaussianSplatAsset>(Obj);
	if (Asset && NewReimportPaths.Num() > 0)
	{
		Asset->SourceFilePath = NewReimportPaths[0];
	}
}

EReimportResult::Type UGaussianSplatAssetFactory::Reimport(UObject* Obj)
{
	UGaussianSplatAsset* Asset = Cast<UGaussianSplatAsset>(Obj);
	if (!Asset)
	{
		return EReimportResult::Failed;
	}

	if (Asset->SourceFilePath.IsEmpty())
	{
		UE_LOG(LogUESplatting, Error, TEXT("Cannot reimport: source file path is empty"));
		return EReimportResult::Failed;
	}

	if (!FPaths::FileExists(Asset->SourceFilePath))
	{
		UE_LOG(LogUESplatting, Error, TEXT("Cannot reimport: source file not found: %s"), *Asset->SourceFilePath);
		return EReimportResult::Failed;
	}

	// Preserve the custom Cluster LOD setting before reimport.
	const bool bWasNaniteEnabled = Asset->IsNaniteEnabled();

	UGaussianSplatAsset* ReimportedAsset = ImportPLYFile(
		Asset->SourceFilePath,
		Asset->GetOuter(),
		Asset->GetFName(),
		Asset->GetFlags(),
		Asset
	);

	if (ReimportedAsset)
	{
		// If Cluster LOD was enabled before reimport, rebuild the hierarchy.
		if (bWasNaniteEnabled)
		{
			UE_LOG(LogUESplatting, Log, TEXT("Reimport: Rebuilding Cluster LOD hierarchy"));
			if (!ReimportedAsset->BuildNaniteClusterHierarchy())
			{
				UE_LOG(LogUESplatting, Warning, TEXT("Reimport: Failed to rebuild Cluster LOD hierarchy"));
			}
		}
		return EReimportResult::Succeeded;
	}

	return EReimportResult::Failed;
}

UGaussianSplatAsset* UGaussianSplatAssetFactory::ImportPLYFile(
	const FString& FilePath,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	UGaussianSplatAsset* ExistingAsset)
{
	FScopedSlowTask SlowTask(100.0f, FText::FromString(TEXT("Importing Gaussian Splat...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(30.0f, FText::FromString(TEXT("Decoding splat file...")));

	FSplatDecodeResult Decoded;
	FString ErrorMessage;

	if (!FGaussianSplatDecoderRegistry::DecodeFile(FilePath, Decoded, ErrorMessage))
	{
		UE_LOG(LogUESplatting, Error, TEXT("Failed to decode splat file: %s"), *ErrorMessage);
		return nullptr;
	}

	UE_LOG(LogUESplatting, Log, TEXT("Decoded %d splats from %s (SH bands: %d)"), Decoded.Splats.Num(), *FilePath, Decoded.SHBands);

	// Create or reuse asset
	SlowTask.EnterProgressFrame(10.0f, FText::FromString(TEXT("Creating asset...")));

	UGaussianSplatAsset* Asset = ExistingAsset;
	if (!Asset)
	{
		Asset = NewObject<UGaussianSplatAsset>(InParent, UGaussianSplatAsset::StaticClass(), InName, Flags);
	}

	if (!Asset)
	{
		UE_LOG(LogUESplatting, Error, TEXT("Failed to create Gaussian Splat asset"));
		return nullptr;
	}

	// Store source file path
	Asset->SourceFilePath = FilePath;

	// Set the detected SH band count BEFORE initializing (CompressSH uses this)
	Asset->SHBands = Decoded.SHBands;

	// Initialize the base representation; Cluster LOD is an explicit asset action.
	SlowTask.EnterProgressFrame(55.0f, FText::FromString(TEXT("Compressing splat data...")));

	Asset->InitializeFromSplatData(Decoded.Splats, EGaussianQualityLevel::VeryHigh);

	// Reimport always returns to the base representation before optionally rebuilding cluster LOD.
	Asset->ClusterHierarchy.Reset();
	Asset->OriginalSplatCount = 0;
	Asset->SetNaniteEnabled(false);
	Asset->InvalidateRenderDataAndNotify();

	// Mark package dirty
	Asset->MarkPackageDirty();

	UE_LOG(LogUESplatting, Log, TEXT("Successfully imported Gaussian Splat asset: %d splats, %lld bytes (Cluster LOD disabled by default)"),
		Asset->GetSplatCount(), Asset->GetMemoryUsage());

	return Asset;
}
