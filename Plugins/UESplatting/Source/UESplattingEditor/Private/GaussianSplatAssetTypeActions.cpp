// SPDX-License-Identifier: MIT

#include "GaussianSplatAssetTypeActions.h"
#include "UESplattingLog.h"
#include "GaussianSplatAsset.h"
#include "EditorReimportHandler.h"
#include "ToolMenuSection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_GaussianSplatAsset"

FText FAssetTypeActions_GaussianSplatAsset::GetName() const
{
	return LOCTEXT("AssetName", "Gaussian Splat Asset");
}

FColor FAssetTypeActions_GaussianSplatAsset::GetTypeColor() const
{
	// Teal color to distinguish from other assets
	return FColor(64, 200, 180);
}

UClass* FAssetTypeActions_GaussianSplatAsset::GetSupportedClass() const
{
	return UGaussianSplatAsset::StaticClass();
}

uint32 FAssetTypeActions_GaussianSplatAsset::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

void FAssetTypeActions_GaussianSplatAsset::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UGaussianSplatAsset>> GaussianSplatAssets;
	for (UObject* Object : InObjects)
	{
		if (UGaussianSplatAsset* Asset = Cast<UGaussianSplatAsset>(Object))
		{
			GaussianSplatAssets.Add(Asset);
		}
	}

	// GAUSSIAN SPLAT ACTIONS section
	Section.AddMenuEntry(
		"GaussianSplatAsset_Reimport",
		LOCTEXT("ReimportLabel", "Reimport"),
		LOCTEXT("ReimportTooltip", "Reimport the Gaussian Splat asset from its source file"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_GaussianSplatAsset::ExecuteReimport, GaussianSplatAssets),
			FCanExecuteAction()
		)
	);

	Section.AddMenuEntry(
		"GaussianSplatAsset_ShowInfo",
		LOCTEXT("ShowInfoLabel", "Show Info"),
		LOCTEXT("ShowInfoTooltip", "Display information about the Gaussian Splat asset"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_GaussianSplatAsset::ExecuteShowInfo, GaussianSplatAssets),
			FCanExecuteAction()
		)
	);

	// UESplatting-owned hierarchy. Internal function/property names remain for asset compatibility.
	Section.AddSubMenu(
		"GaussianSplatAsset_Nanite",
		LOCTEXT("ClusterLodSubMenuLabel", "Cluster LOD"),
		LOCTEXT("ClusterLodSubMenuTooltip", "UESplatting cluster hierarchy options for LOD and culling"),
		FNewMenuDelegate::CreateLambda([this, GaussianSplatAssets](FMenuBuilder& SubMenuBuilder)
		{
			// Determine current state
			bool bAllEnabled = AreAllNaniteEnabled(GaussianSplatAssets);
			bool bAllDisabled = AreAllNaniteDisabled(GaussianSplatAssets);

			// Cluster LOD checkbox - checked if enabled
			SubMenuBuilder.AddMenuEntry(
				LOCTEXT("ClusterLodEnabledLabel", "Cluster LOD"),
				LOCTEXT("ClusterLodEnabledTooltip", "Toggle UESplatting cluster LOD on the selected Gaussian Splat assets"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, GaussianSplatAssets, bAllEnabled]()
					{
						if (bAllEnabled)
						{
							// All enabled -> disable all
							ExecuteDisableNanite(GaussianSplatAssets);
						}
						else
						{
							// Mixed or all disabled -> enable all
							ExecuteEnableNanite(GaussianSplatAssets);
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([bAllEnabled]() { return bAllEnabled; })
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);

			SubMenuBuilder.AddSeparator();

			// Enable Cluster LOD action
			SubMenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("EnableClusterLodLabel", "Build Cluster LOD ({0} Assets)"), FText::AsNumber(GaussianSplatAssets.Num())),
				LOCTEXT("EnableClusterLodTooltip", "Build the UESplatting cluster hierarchy for LOD and culling"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetTypeActions_GaussianSplatAsset::ExecuteEnableNanite, GaussianSplatAssets),
					FCanExecuteAction::CreateLambda([bAllEnabled]() { return !bAllEnabled; })
				)
			);

			// Disable Cluster LOD action
			SubMenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("DisableClusterLodLabel", "Clear Cluster LOD ({0} Assets)"), FText::AsNumber(GaussianSplatAssets.Num())),
				LOCTEXT("DisableClusterLodTooltip", "Remove the UESplatting cluster hierarchy to reduce asset size"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetTypeActions_GaussianSplatAsset::ExecuteDisableNanite, GaussianSplatAssets),
					FCanExecuteAction::CreateLambda([bAllDisabled]() { return !bAllDisabled; })
				)
			);
		}),
		false,
		FSlateIcon()
	);
}

void FAssetTypeActions_GaussianSplatAsset::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	FAssetTypeActions_Base::OpenAssetEditor(InObjects, EditWithinLevelEditor);
}

void FAssetTypeActions_GaussianSplatAsset::ExecuteReimport(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects)
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			FReimportManager::Instance()->Reimport(Asset, /*bAskForNewFileIfMissing=*/true);
		}
	}
}

void FAssetTypeActions_GaussianSplatAsset::ExecuteShowInfo(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects)
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			FString ClusterLodStatus = Asset->IsNaniteEnabled() ?
				FString::Printf(TEXT("Enabled (%d clusters, %d LOD levels)"), Asset->GetClusterCount(), Asset->GetNumLODLevels()) :
				TEXT("Disabled");

			FString InfoMessage = FString::Printf(
				TEXT("Gaussian Splat Asset Info:\n\n")
				TEXT("Name: %s\n")
				TEXT("Splat Count: %d\n")
				TEXT("Original Splat Count: %d\n")
				TEXT("Memory Usage: %.2f MB\n")
				TEXT("Bounds: %s\n")
				TEXT("Source File: %s\n")
				TEXT("Cluster LOD: %s"),
				*Asset->GetName(),
				Asset->GetSplatCount(),
				Asset->GetOriginalSplatCount(),
				Asset->GetMemoryUsage() / (1024.0 * 1024.0),
				*Asset->GetBounds().ToString(),
				*Asset->SourceFilePath,
				*ClusterLodStatus
			);

			UE_LOG(LogUESplatting, Log, TEXT("%s"), *InfoMessage);

			// Show message box
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(InfoMessage));
		}
	}
}

void FAssetTypeActions_GaussianSplatAsset::ExecuteEnableNanite(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects)
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			if (!Asset->IsNaniteEnabled())
			{
				// Check if source file exists
				if (Asset->SourceFilePath.IsEmpty() || !FPaths::FileExists(Asset->SourceFilePath))
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
						LOCTEXT("SourceFileNotFound", "Cannot build Cluster LOD for {0}:\nSource splat file not found: {1}\n\nPlease reimport the asset first."),
						FText::FromString(Asset->GetName()),
						FText::FromString(Asset->SourceFilePath)
					));
					continue;
				}

				UE_LOG(LogUESplatting, Log, TEXT("Building Cluster LOD for asset: %s"), *Asset->GetName());

				if (Asset->BuildNaniteClusterHierarchy())
				{
					UE_LOG(LogUESplatting, Log, TEXT("Built Cluster LOD for asset: %s (%d clusters)"),
						*Asset->GetName(), Asset->GetClusterCount());
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
						LOCTEXT("ClusterLodBuildFailed", "Failed to build Cluster LOD for {0}.\nSee Output Log for details."),
						FText::FromString(Asset->GetName())
					));
				}
			}
		}
	}
}

void FAssetTypeActions_GaussianSplatAsset::ExecuteDisableNanite(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects)
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			if (Asset->IsNaniteEnabled())
			{
				UE_LOG(LogUESplatting, Log, TEXT("Clearing Cluster LOD for asset: %s"), *Asset->GetName());
				Asset->ClearNaniteClusterHierarchy();
				UE_LOG(LogUESplatting, Log, TEXT("Cleared Cluster LOD for asset: %s"), *Asset->GetName());
			}
		}
	}
}

bool FAssetTypeActions_GaussianSplatAsset::AreAllNaniteEnabled(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects) const
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			if (!Asset->IsNaniteEnabled())
			{
				return false;
			}
		}
	}
	return Objects.Num() > 0;
}

bool FAssetTypeActions_GaussianSplatAsset::AreAllNaniteDisabled(TArray<TWeakObjectPtr<UGaussianSplatAsset>> Objects) const
{
	for (const TWeakObjectPtr<UGaussianSplatAsset>& AssetPtr : Objects)
	{
		if (UGaussianSplatAsset* Asset = AssetPtr.Get())
		{
			if (Asset->IsNaniteEnabled())
			{
				return false;
			}
		}
	}
	return Objects.Num() > 0;
}

#undef LOCTEXT_NAMESPACE
