// SPDX-License-Identifier: MIT

#include "UESplattingEditorModule.h"

#include "GaussianSplatAsset.h"
#include "GaussianSplatAssetTypeActions.h"
#include "GaussianSplatThumbnailRenderer.h"
#include "IAssetTools.h"
#include "UESplattingLog.h"
#include "AssetToolsModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"

void FUESplattingEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedPtr<IAssetTypeActions> GaussianSplatAssetActions = MakeShared<FAssetTypeActions_GaussianSplatAsset>();
	AssetTools.RegisterAssetTypeActions(GaussianSplatAssetActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(GaussianSplatAssetActions);

	UThumbnailManager::Get().RegisterCustomRenderer(
		UGaussianSplatAsset::StaticClass(),
		UGaussianSplatThumbnailRenderer::StaticClass());

	UE_LOG(LogUESplatting, Log, TEXT("UESplatting editor module started."));
}

void FUESplattingEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UGaussianSplatAsset::StaticClass());
	}

	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();

	UE_LOG(LogUESplatting, Log, TEXT("UESplatting editor module shut down."));
}

IMPLEMENT_MODULE(FUESplattingEditorModule, UESplattingEditor)
