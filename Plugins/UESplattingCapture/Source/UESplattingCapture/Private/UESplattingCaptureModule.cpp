// SPDX-License-Identifier: MIT

#include "UESplattingCaptureModule.h"
#include "UESplattingLog.h"
#include "UESplattingCaptureVolume.h"
#include "UESplattingCaptureVolumeDetails.h"
#include "UESplattingDatasetExporter.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Misc/MessageDialog.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FUESplattingCaptureModule"

void FUESplattingCaptureModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUESplattingCaptureModule::RegisterMenus));

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(
		AUESplattingCaptureVolume::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FUESplattingCaptureVolumeDetails::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();

	UE_LOG(LogUESplatting, Log, TEXT("UESplatting Capture editor module started."));
}

void FUESplattingCaptureModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.UnregisterCustomClassLayout(AUESplattingCaptureVolume::StaticClass()->GetFName());
		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}

	UE_LOG(LogUESplatting, Log, TEXT("UESplatting Capture editor module shut down."));
}

void FUESplattingCaptureModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("UESplatting");
	Section.Label = LOCTEXT("UESplattingMenuSection", "UESplatting");
	Section.AddMenuEntry(
		"UESplattingAddColmapCaptureVolume",
		LOCTEXT("AddColmapCaptureVolume", "Add Scene Capture Volume"),
		LOCTEXT("AddColmapCaptureVolumeTooltip", "Place a UESplatting capture volume actor for room-scale known-pose dataset export."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUESplattingCaptureModule::AddCaptureVolumeToLevel)));
	Section.AddMenuEntry(
		"UESplattingAddDirectionalArray",
		LOCTEXT("AddDirectionalArray", "Add Directional Camera Array"),
		LOCTEXT("AddDirectionalArrayTooltip", "Place a planar grid of translated cameras at the active viewport, all looking in the actor's forward direction."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUESplattingCaptureModule::AddDirectionalArrayToLevel)));
	Section.AddMenuEntry(
		"UESplattingAddFocusedDetailRegion",
		LOCTEXT("AddFocusedDetailRegion", "Add Focused Detail Region"),
		LOCTEXT("AddFocusedDetailRegionTooltip", "Place a target box for higher-detail reconstruction. It automatically links to the selected Room Coverage actor, or the only Room Coverage actor in the world."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUESplattingCaptureModule::AddFocusedDetailRegionToLevel)));
	Section.AddMenuEntry(
		"UESplattingExportSelectedCamerasToColmapDataset",
		LOCTEXT("ExportSelectedCamerasToColmapDataset", "Export Selected Cameras to Scene Capture Dataset"),
		LOCTEXT("ExportSelectedCamerasToColmapDatasetTooltip", "Capture selected camera actors to a known-pose scene capture dataset."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUESplattingCaptureModule::ExportSelectedCamerasToColmapDataset)));
	Section.AddMenuEntry(
		"UESplattingExportSelectedCaptureVolumesToColmapDataset",
		LOCTEXT("ExportSelectedCaptureVolumesToColmapDataset", "Export Selected Capture Volumes to Scene Capture Dataset"),
		LOCTEXT("ExportSelectedCaptureVolumesToColmapDatasetTooltip", "Export selected UESplatting capture sets. Room Coverage and Focused Detail links expand automatically; multi-selection is useful for ad hoc or multi-room batches."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUESplattingCaptureModule::ExportSelectedCaptureVolumesToColmapDataset)));
}

void FUESplattingCaptureModule::AddCaptureVolumeToLevel()
{
	if (!GEditor)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEditorForCaptureVolume", "Could not access the editor world."));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->GetCurrentLevel())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorldForCaptureVolume", "Could not find a level to place the capture volume in."));
		return;
	}

	AActor* NewActor = GEditor->AddActor(World->GetCurrentLevel(), AUESplattingCaptureVolume::StaticClass(), FTransform::Identity);
	if (!NewActor)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CaptureVolumeAddFailed", "Failed to add a UESplatting capture volume."));
		return;
	}

	NewActor->SetActorLabel(TEXT("UESplatting_SceneCaptureVolume"));
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(NewActor, true, true, true);
}

void FUESplattingCaptureModule::AddDirectionalArrayToLevel()
{
	if (!GEditor)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEditorForDirectionalArray", "Could not access the editor world."));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->GetCurrentLevel())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorldForDirectionalArray", "Could not find a level to place the directional camera array in."));
		return;
	}

	FTransform SpawnTransform = FTransform::Identity;
	FUESplattingCaptureView ViewportView;
	FString ViewportError;
	if (UUESplattingDatasetExporter::ResolveActiveViewportView(World, ViewportView, ViewportError))
	{
		SpawnTransform = ViewportView.Transform;
		SpawnTransform.SetScale3D(FVector::OneVector);
	}

	AUESplattingCaptureVolume* DirectionalArray = Cast<AUESplattingCaptureVolume>(
		GEditor->AddActor(World->GetCurrentLevel(), AUESplattingCaptureVolume::StaticClass(), SpawnTransform));
	if (!DirectionalArray)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DirectionalArrayAddFailed", "Failed to add a UESplatting directional camera array."));
		return;
	}

	DirectionalArray->ConfigureAsDirectionalArray();
	DirectionalArray->SetActorLabel(TEXT("UESplatting_DirectionalCameraArray"));
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(DirectionalArray, true, true, true);
}

void FUESplattingCaptureModule::AddFocusedDetailRegionToLevel()
{
	if (!GEditor)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEditorForDetailRegion", "Could not access the editor world."));
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->GetCurrentLevel())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorldForDetailRegion", "Could not find a level to place the focused detail region in."));
		return;
	}

	AUESplattingCaptureVolume* LinkedRoomCoverage = nullptr;
	int32 SelectedRoomCount = 0;
	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			AUESplattingCaptureVolume* SelectedVolume = Cast<AUESplattingCaptureVolume>(*It);
			if (SelectedVolume && SelectedVolume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
			{
				LinkedRoomCoverage = SelectedVolume;
				++SelectedRoomCount;
			}
		}
	}
	if (SelectedRoomCount != 1)
	{
		LinkedRoomCoverage = nullptr;
		int32 WorldRoomCount = 0;
		for (TActorIterator<AUESplattingCaptureVolume> It(World); It; ++It)
		{
			AUESplattingCaptureVolume* Candidate = *It;
			if (Candidate && Candidate->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
			{
				LinkedRoomCoverage = Candidate;
				++WorldRoomCount;
			}
		}
		if (WorldRoomCount != 1)
		{
			LinkedRoomCoverage = nullptr;
		}
	}

	AUESplattingCaptureVolume* DetailRegion = Cast<AUESplattingCaptureVolume>(
		GEditor->AddActor(World->GetCurrentLevel(), AUESplattingCaptureVolume::StaticClass(), FTransform::Identity));
	if (!DetailRegion)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DetailRegionAddFailed", "Failed to add a UESplatting focused detail region."));
		return;
	}

	DetailRegion->ConfigureAsFocusedDetailRegion();
	DetailRegion->RoomCoverageVolume = LinkedRoomCoverage;
	DetailRegion->SetActorLabel(TEXT("UESplatting_FocusedDetailRegion"));
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(DetailRegion, true, true, true);
}

void FUESplattingCaptureModule::ExportSelectedCamerasToColmapDataset()
{
	FUESplattingDatasetExportSettings Settings;
	FUESplattingDatasetExportResult StartResult;
	const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromSelectedCameras(
		Settings,
		[](const FUESplattingDatasetExportResult& CompletedResult)
		{
			const FString OutputText = CompletedResult.OutputDirectory.IsEmpty()
				? TEXT("")
				: FString::Printf(TEXT("\n\nOutput:\n%s"), *CompletedResult.OutputDirectory);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompletedResult.Message + OutputText));
		},
		StartResult);
	if (!bStarted)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(StartResult.Message));
	}
}

void FUESplattingCaptureModule::ExportSelectedCaptureVolumesToColmapDataset()
{
	TArray<AUESplattingCaptureVolume*> CaptureVolumes;

	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (AUESplattingCaptureVolume* CaptureVolume = Cast<AUESplattingCaptureVolume>(*It))
			{
				CaptureVolumes.Add(CaptureVolume);
			}
		}
	}

	if (CaptureVolumes.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoCaptureVolumesSelected", "Select one or more UESplatting capture volumes first."));
		return;
	}

	AUESplattingCaptureVolume::ExportCaptureVolumeSet(CaptureVolumes);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUESplattingCaptureModule, UESplattingCapture)
