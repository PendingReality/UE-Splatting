// SPDX-License-Identifier: MIT

#include "UESplattingCaptureVolumeDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "UESplattingCaptureVolume.h"
#include "UESplattingDatasetExporter.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FUESplattingCaptureVolumeDetails"

TSharedRef<IDetailCustomization> FUESplattingCaptureVolumeDetails::MakeInstance()
{
	return MakeShared<FUESplattingCaptureVolumeDetails>();
}

void FUESplattingCaptureVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	CaptureVolumes.Reset();
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, CaptureBounds));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, ProbePreview));

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (AUESplattingCaptureVolume* CaptureVolume = Cast<AUESplattingCaptureVolume>(Object.Get()))
		{
			CaptureVolumes.Add(CaptureVolume);
		}
	}

	IDetailCategoryBuilder& CaptureCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting Capture"),
		LOCTEXT("UESplattingCaptureCategory", "UESplatting Capture"),
		ECategoryPriority::Important);

	TSharedRef<IPropertyHandle> CapturePatternHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, CapturePattern));
	TSharedRef<IPropertyHandle> CaptureProfileHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, CaptureProfile));
	TSharedRef<IPropertyHandle> ProbeSpacingHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, RoomCoverageCustomProbeSpacingMeters));
	TSharedRef<IPropertyHandle> RoomCoverageVolumeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, RoomCoverageVolume));
	TSharedRef<IPropertyHandle> DetailProfileHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, DetailCaptureProfile));
	TSharedRef<IPropertyHandle> DetailNearDistanceHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, DetailNearStandoffMeters));
	TSharedRef<IPropertyHandle> DetailFarDistanceHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, DetailFarStandoffMeters));
	TSharedRef<IPropertyHandle> DirectionalArraySpacingHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, DirectionalArraySpacingMeters));
	TSharedRef<IPropertyHandle> HorizontalFieldOfViewHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, HorizontalFieldOfView));
	TSharedRef<IPropertyHandle> ZoneIdHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, ZoneId));
	TSharedRef<IPropertyHandle> BlockIdHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, BlockId));
	TSharedRef<IPropertyHandle> OverlapMarginHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, OverlapMarginMeters));
	TSharedRef<IPropertyHandle> ShowPreviewHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, bShowProbePreview));
	TSharedRef<IPropertyHandle> ReferencePostProcessCameraHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, ReferencePostProcessCamera));
	TSharedRef<IPropertyHandle> ExportSettingsHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, ExportSettings));
	TSharedPtr<IPropertyHandle> RendererHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, Renderer));
	TSharedPtr<IPropertyHandle> FreezeSceneHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, bFreezeSceneDuringCapture));
	TSharedPtr<IPropertyHandle> OutputDirectoryHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, OutputDirectory));
	TSharedPtr<IPropertyHandle> ImageFormatHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, ImageFormat));
	TSharedPtr<IPropertyHandle> JpegQualityHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, JpegQuality));
	TSharedPtr<IPropertyHandle> LightingMethodHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, LightingMethod));
	TSharedPtr<IPropertyHandle> PhotometricModeHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, PhotometricMode));
	TSharedPtr<IPropertyHandle> EyeAdaptationHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, bUseEyeAdaptation));
	TSharedPtr<IPropertyHandle> RayTracingHandle = ExportSettingsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FUESplattingDatasetExportSettings, bUseRayTracingIfEnabled));
	DetailBuilder.HideProperty(CapturePatternHandle);
	DetailBuilder.HideProperty(CaptureProfileHandle);
	DetailBuilder.HideProperty(ProbeSpacingHandle);
	DetailBuilder.HideProperty(RoomCoverageVolumeHandle);
	DetailBuilder.HideProperty(DetailProfileHandle);
	DetailBuilder.HideProperty(DetailNearDistanceHandle);
	DetailBuilder.HideProperty(DetailFarDistanceHandle);
	DetailBuilder.HideProperty(DirectionalArraySpacingHandle);
	DetailBuilder.HideProperty(HorizontalFieldOfViewHandle);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, Distribution));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, StationSpacing));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, HaltonStationCount));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, YawMode));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, PitchMode));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, PitchAngleDegrees));
	DetailBuilder.HideProperty(ZoneIdHandle);
	DetailBuilder.HideProperty(BlockIdHandle);
	DetailBuilder.HideProperty(OverlapMarginHandle);
	DetailBuilder.HideProperty(ShowPreviewHandle);
	DetailBuilder.HideProperty(ReferencePostProcessCameraHandle);
	if (RendererHandle.IsValid())
	{
		DetailBuilder.HideProperty(RendererHandle.ToSharedRef());
	}
	if (FreezeSceneHandle.IsValid())
	{
		DetailBuilder.HideProperty(FreezeSceneHandle.ToSharedRef());
	}
	if (OutputDirectoryHandle.IsValid())
	{
		DetailBuilder.HideProperty(OutputDirectoryHandle.ToSharedRef());
	}
	if (ImageFormatHandle.IsValid())
	{
		DetailBuilder.HideProperty(ImageFormatHandle.ToSharedRef());
	}
	if (JpegQualityHandle.IsValid())
	{
		DetailBuilder.HideProperty(JpegQualityHandle.ToSharedRef());
	}
	if (LightingMethodHandle.IsValid())
	{
		DetailBuilder.HideProperty(LightingMethodHandle.ToSharedRef());
	}
	if (PhotometricModeHandle.IsValid())
	{
		DetailBuilder.HideProperty(PhotometricModeHandle.ToSharedRef());
	}
	if (EyeAdaptationHandle.IsValid())
	{
		DetailBuilder.HideProperty(EyeAdaptationHandle.ToSharedRef());
	}
	if (RayTracingHandle.IsValid())
	{
		DetailBuilder.HideProperty(RayTracingHandle.ToSharedRef());
	}

	CaptureCategory.AddCustomRow(LOCTEXT("UESplattingCaptureActionsFilter", "Export Capture Probe Images Output"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetSummaryText(); })
			.Font(FAppStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CalibrateExposureButton", "Render MRQ Test Frame"))
				.ToolTipText(LOCTEXT("CalibrateExposureTooltip", "Render the active viewport pose through the same Movie Render Queue backend used by dataset export."))
				.OnClicked_Lambda([this]() { return OnCalibrateClicked(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenExposurePreviewButton", "Open Preview"))
				.ToolTipText(LOCTEXT("OpenExposurePreviewTooltip", "Open the most recent one-frame render-backend preview."))
				.IsEnabled_Lambda([this]()
				{
					TArray<AUESplattingCaptureVolume*> CaptureSet;
					AUESplattingCaptureVolume::ExpandCaptureVolumeSet(GetValidVolumes(), CaptureSet);
					const AUESplattingCaptureVolume* PrimaryVolume = AUESplattingCaptureVolume::FindPrimaryCaptureVolume(CaptureSet);
					return PrimaryVolume && !PrimaryVolume->LastPhotometricCalibration.PreviewImagePath.IsEmpty();
				})
				.OnClicked_Lambda([this]() { return OnOpenCalibrationPreviewClicked(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportDatasetButton", "Export Dataset"))
				.ToolTipText(LOCTEXT("ExportDatasetTooltip", "Export this actor's complete linked capture set through Movie Render Queue, including its Room Coverage and Focused Detail regions."))
				.OnClicked_Lambda([this]() { return OnExportClicked(); })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
				if (Volumes.Num() == 1 && Volumes[0]->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
				{
					return LOCTEXT("ScaleDetailActorHint", "Scale the box around the surfaces that need more detail. Cameras are generated outside the box and aim back into it.");
				}
				if (Volumes.Num() == 1 && Volumes[0]->CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep)
				{
					return LOCTEXT("ScaleDirectionalArrayHint", "Scale Y/Z to set the camera wall, then rotate the actor until the green preview arrows point toward the scene. Each origin exports exactly one view.");
				}
				return LOCTEXT("ScaleActorHint", "Scale the actor to resize the sampled capture volume.");
			})
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetOutputRootText(); })
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		]
	];

	const TAttribute<EVisibility> RoomCoverageVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume && Volume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage;
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	const TAttribute<EVisibility> FocusedDetailVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume && Volume->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail;
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	const TAttribute<EVisibility> DirectionalArrayVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume && Volume->CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep;
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});

	CaptureCategory.AddProperty(CapturePatternHandle)
		.DisplayName(LOCTEXT("CapturePatternDisplayName", "Capture Mode"))
		.ToolTip(LOCTEXT("CapturePatternTooltip", "Room Coverage samples an area in all directions. Directional Array creates a forward-facing camera wall. Focused Detail surrounds a bounded target with inward-facing cameras."));

	CaptureCategory.AddProperty(ShowPreviewHandle)
		.DisplayName(LOCTEXT("ShowProbePreviewDisplayName", "Show Probe Preview"))
		.ToolTip(LOCTEXT("ShowProbePreviewTooltip", "Show the live editor-only probe layout. Preview geometry is not present during capture export."));

	CaptureCategory.AddProperty(RoomCoverageVolumeHandle)
		.DisplayName(LOCTEXT("RoomCoverageVolumeDisplayName", "Room Coverage"))
		.ToolTip(LOCTEXT("RoomCoverageVolumeTooltip", "Persistent owner of this detail region's capture set. Exporting either actor includes the room and every detail region linked to it."))
		.Visibility(FocusedDetailVisibility);

	CaptureCategory.AddProperty(CaptureProfileHandle)
		.DisplayName(LOCTEXT("CaptureProfileDisplayName", "Probe Density"))
		.ToolTip(LOCTEXT("CaptureProfileTooltip", "Translated-camera density for room capture. Low, Medium, High, and Ultra use 1.5 m, 1.0 m, 0.75 m, and 0.5 m spacing while retaining three height bands and eight views per probe."))
		.Visibility(RoomCoverageVisibility);
	CaptureCategory.AddProperty(ProbeSpacingHandle)
		.DisplayName(LOCTEXT("ProbeSpacingDisplayName", "Custom Probe Spacing"))
		.ToolTip(LOCTEXT("ProbeSpacingTooltip", "Custom nominal translated-camera spacing in meters. The live summary reports requested and accepted density plus scene-aware coverage."))
		.Visibility(RoomCoverageVisibility);
	CaptureCategory.AddProperty(DirectionalArraySpacingHandle)
		.DisplayName(LOCTEXT("DirectionalArraySpacingDisplayName", "Camera Spacing"))
		.ToolTip(LOCTEXT("DirectionalArraySpacingTooltip", "Nominal world-space spacing between translated camera origins across the array. Increase it as the wall grows from human-sized to drone-sized baselines."))
		.Visibility(DirectionalArrayVisibility);
	CaptureCategory.AddProperty(DetailProfileHandle)
		.DisplayName(LOCTEXT("DetailProfileDisplayName", "Detail Quality"))
		.ToolTip(LOCTEXT("DetailProfileTooltip", "Controls the number of translated camera origins, elevation bands, distance rings, and target tiles used around this detail region."))
		.Visibility(FocusedDetailVisibility);
	CaptureCategory.AddProperty(DetailNearDistanceHandle)
		.DisplayName(LOCTEXT("DetailNearDistanceDisplayName", "Close Camera Distance"))
		.ToolTip(LOCTEXT("DetailNearDistanceTooltip", "Distance from the target box surface to the closest camera ring. Move closer for more target pixels while preserving camera clearance."))
		.Visibility(FocusedDetailVisibility);
	CaptureCategory.AddProperty(DetailFarDistanceHandle)
		.DisplayName(LOCTEXT("DetailFarDistanceDisplayName", "Context Camera Distance"))
		.ToolTip(LOCTEXT("DetailFarDistanceTooltip", "Distance from the target box surface to the wider ring that connects close detail views back to the room."))
		.Visibility(FocusedDetailVisibility);
	CaptureCategory.AddProperty(ZoneIdHandle)
		.DisplayName(LOCTEXT("ZoneIdDisplayName", "Zone Id"))
		.ToolTip(LOCTEXT("ZoneIdTooltip", "Optional room or zone id recorded in capture-manifest.json for downstream block/scene organization."));
	CaptureCategory.AddProperty(BlockIdHandle)
		.DisplayName(LOCTEXT("BlockIdDisplayName", "Block Id"))
		.ToolTip(LOCTEXT("BlockIdTooltip", "Optional block/cell id recorded in capture-manifest.json for partitioned large-scene training."));
	CaptureCategory.AddProperty(OverlapMarginHandle)
		.DisplayName(LOCTEXT("OverlapMarginDisplayName", "Overlap Margin"))
		.ToolTip(LOCTEXT("OverlapMarginTooltip", "Optional intended overlap margin, in meters, for neighboring capture blocks. Recorded in the manifest."));

	const TAttribute<EVisibility> PrimarySettingsVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume && (Volume->CapturePattern != EUESplattingCaptureVolumePattern::FocusedDetail
				|| !Volume->GetLinkedRoomCoverageVolume());
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	CaptureCategory.AddProperty(HorizontalFieldOfViewHandle)
		.DisplayName(LOCTEXT("HorizontalFieldOfViewDisplayName", "Horizontal FOV"))
		.ToolTip(LOCTEXT("HorizontalFieldOfViewTooltip", "Horizontal field of view recorded in the exported pinhole intrinsics. Linked Focused Detail regions inherit this value from Room Coverage."))
		.Visibility(PrimarySettingsVisibility);
	const TAttribute<EVisibility> LegacyRendererVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume
				&& (Volume->CapturePattern != EUESplattingCaptureVolumePattern::FocusedDetail || !Volume->GetLinkedRoomCoverageVolume())
				&& Volume->ExportSettings.Renderer == EUESplattingSceneCaptureRenderer::SceneCapture2DLegacy;
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	const TAttribute<EVisibility> MovieRenderQueueVisibility = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
		return Volumes.ContainsByPredicate([](const AUESplattingCaptureVolume* Volume)
		{
			return Volume
				&& (Volume->CapturePattern != EUESplattingCaptureVolumePattern::FocusedDetail || !Volume->GetLinkedRoomCoverageVolume())
				&& Volume->ExportSettings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue;
		}) ? EVisibility::Visible : EVisibility::Collapsed;
	});

	if (RendererHandle.IsValid())
	{
		CaptureCategory.AddProperty(RendererHandle.ToSharedRef())
			.DisplayName(LOCTEXT("RendererDisplayName", "Renderer"))
			.ToolTip(LOCTEXT("RendererTooltip", "Movie Render Queue is the normal authored-scene path. SceneCapture2D is retained only as an explicit legacy fallback."))
			.Visibility(PrimarySettingsVisibility);
	}
	if (FreezeSceneHandle.IsValid())
	{
		CaptureCategory.AddProperty(FreezeSceneHandle.ToSharedRef())
			.DisplayName(LOCTEXT("FreezeSceneDisplayName", "Freeze Scene During Capture"))
			.ToolTip(LOCTEXT("FreezeSceneTooltip", "Pause normal actor, physics, timer, Niagara, and game-time-driven material updates during the MRQ shot while the capture camera continues to update. Systems explicitly configured to tick while paused or use real time can still advance."))
			.Visibility(MovieRenderQueueVisibility);
	}

	if (OutputDirectoryHandle.IsValid())
	{
		CaptureCategory.AddProperty(OutputDirectoryHandle.ToSharedRef())
			.DisplayName(LOCTEXT("OutputDirectoryDisplayName", "Output Directory"))
			.ToolTip(LOCTEXT("OutputDirectoryTooltip", "Exact capture folder. Leave empty to use Capture Root plus Capture Id, UESPLATTING_SCENE_CAPTURE_ROOT, or Project Saved."))
			.Visibility(PrimarySettingsVisibility);
	}
	if (ImageFormatHandle.IsValid())
	{
		CaptureCategory.AddProperty(ImageFormatHandle.ToSharedRef())
			.DisplayName(LOCTEXT("ImageFormatDisplayName", "Image Format"))
			.ToolTip(LOCTEXT("ImageFormatTooltip", "Legacy SceneCapture2D output format. Movie Render Queue emits JPEG and UESplatting normalizes filenames to .jpg."))
			.Visibility(LegacyRendererVisibility);
	}
	if (JpegQualityHandle.IsValid())
	{
		CaptureCategory.AddProperty(JpegQualityHandle.ToSharedRef())
			.DisplayName(LOCTEXT("JpegQualityDisplayName", "JPEG Quality"))
			.ToolTip(LOCTEXT("JpegQualityTooltip", "Legacy SceneCapture2D JPEG quality. Movie Render Queue uses its graph's engine JPEG settings."))
			.Visibility(LegacyRendererVisibility);
	}
	if (LightingMethodHandle.IsValid())
	{
		CaptureCategory.AddProperty(LightingMethodHandle.ToSharedRef())
			.DisplayName(LOCTEXT("LightingMethodDisplayName", "Lighting Method"))
			.ToolTip(LOCTEXT("LightingMethodTooltip", "Legacy SceneCapture2D lighting override. Movie Render Queue follows the authored project and camera renderer."))
			.Visibility(LegacyRendererVisibility);
	}
	if (PhotometricModeHandle.IsValid())
	{
		CaptureCategory.AddProperty(PhotometricModeHandle.ToSharedRef())
			.DisplayName(LOCTEXT("PhotometricModeDisplayName", "Photometrics"))
			.ToolTip(LOCTEXT("PhotometricModeTooltip", "Legacy SceneCapture2D exposure behavior. Movie Render Queue preserves the authored project, volume, and camera post-process stack."))
			.Visibility(LegacyRendererVisibility);
	}
	CaptureCategory.AddProperty(ReferencePostProcessCameraHandle)
		.DisplayName(LOCTEXT("ReferencePostProcessCameraDisplayName", "Reference Post Process Source"))
		.ToolTip(LOCTEXT("ReferencePostProcessCameraTooltip", "Optional Camera, Cine Camera, or Post Process Volume used as an authored appearance reference. Active unbound volumes are already applied by Unreal and are not duplicated."))
		.Visibility(PrimarySettingsVisibility);
	if (RayTracingHandle.IsValid())
	{
		CaptureCategory.AddProperty(RayTracingHandle.ToSharedRef())
			.DisplayName(LOCTEXT("RayTracingDisplayName", "Use Ray Tracing"))
			.ToolTip(LOCTEXT("RayTracingTooltip", "Legacy SceneCapture2D ray-tracing override. Movie Render Queue follows project renderer settings."))
			.Visibility(LegacyRendererVisibility);
	}

	IDetailCategoryBuilder& LayoutCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Camera Layout"),
		LOCTEXT("UESplattingCameraLayoutCategory", "UESplatting Camera Layout"),
		ECategoryPriority::TypeSpecific);
	LayoutCategory.InitiallyCollapsed(false);

	IDetailCategoryBuilder& PreviewCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Preview"),
		LOCTEXT("UESplattingPreviewCategory", "UESplatting Preview"),
		ECategoryPriority::TypeSpecific);
	PreviewCategory.InitiallyCollapsed(true);

	IDetailCategoryBuilder& OutputCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Output"),
		LOCTEXT("UESplattingOutputCategory", "UESplatting Output"),
		ECategoryPriority::TypeSpecific);
	OutputCategory.InitiallyCollapsed(false);

	IDetailCategoryBuilder& CaptureQualityCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Capture Quality"),
		LOCTEXT("UESplattingCaptureQualityCategory", "UESplatting Capture Quality"),
		ECategoryPriority::TypeSpecific);
	CaptureQualityCategory.InitiallyCollapsed(true);

	IDetailCategoryBuilder& SparsePointCloudCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Experimental Seed Cloud"),
		LOCTEXT("UESplattingExperimentalSeedCloudCategory", "UESplatting Experimental Seed Cloud"),
		ECategoryPriority::TypeSpecific);
	SparsePointCloudCategory.InitiallyCollapsed(true);

	IDetailCategoryBuilder& FilteringCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Filtering"),
		LOCTEXT("UESplattingFilteringCategory", "UESplatting Filtering"),
		ECategoryPriority::TypeSpecific);
	FilteringCategory.InitiallyCollapsed(true);

	IDetailCategoryBuilder& CoverageCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Coverage"),
		LOCTEXT("UESplattingCoverageCategory", "UESplatting Coverage"),
		ECategoryPriority::TypeSpecific);
	CoverageCategory.InitiallyCollapsed(true);

	IDetailCategoryBuilder& CoordinatesCategory = DetailBuilder.EditCategory(
		TEXT("UESplatting|Coordinates"),
		LOCTEXT("UESplattingCoordinatesCategory", "UESplatting Coordinates"),
		ECategoryPriority::TypeSpecific);
	CoordinatesCategory.InitiallyCollapsed(true);
}

TArray<AUESplattingCaptureVolume*> FUESplattingCaptureVolumeDetails::GetValidVolumes() const
{
	TArray<AUESplattingCaptureVolume*> Volumes;
	for (const TWeakObjectPtr<AUESplattingCaptureVolume>& CaptureVolume : CaptureVolumes)
	{
		if (AUESplattingCaptureVolume* Volume = CaptureVolume.Get())
		{
			Volumes.Add(Volume);
		}
	}
	return Volumes;
}

FReply FUESplattingCaptureVolumeDetails::OnExportClicked()
{
	const TArray<AUESplattingCaptureVolume*> Volumes = GetValidVolumes();
	AUESplattingCaptureVolume::ExportCaptureVolumeSet(Volumes);
	return FReply::Handled();
}

FReply FUESplattingCaptureVolumeDetails::OnCalibrateClicked()
{
	TArray<AUESplattingCaptureVolume*> CaptureSet;
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet(GetValidVolumes(), CaptureSet);
	if (AUESplattingCaptureVolume* PrimaryVolume = AUESplattingCaptureVolume::FindPrimaryCaptureVolume(CaptureSet))
	{
		PrimaryVolume->CalibrateCaptureExposure();
	}
	return FReply::Handled();
}

FReply FUESplattingCaptureVolumeDetails::OnOpenCalibrationPreviewClicked()
{
	TArray<AUESplattingCaptureVolume*> CaptureSet;
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet(GetValidVolumes(), CaptureSet);
	if (const AUESplattingCaptureVolume* PrimaryVolume = AUESplattingCaptureVolume::FindPrimaryCaptureVolume(CaptureSet);
		PrimaryVolume && !PrimaryVolume->LastPhotometricCalibration.PreviewImagePath.IsEmpty())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*PrimaryVolume->LastPhotometricCalibration.PreviewImagePath);
	}
	return FReply::Handled();
}

FText FUESplattingCaptureVolumeDetails::GetSummaryText() const
{
	const TArray<AUESplattingCaptureVolume*> SelectedVolumes = GetValidVolumes();
	TArray<AUESplattingCaptureVolume*> Volumes;
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet(SelectedVolumes, Volumes);
	if (SelectedVolumes.IsEmpty() || Volumes.IsEmpty())
	{
		return LOCTEXT("NoVolumesSummary", "No capture volume selected");
	}

	int32 StationCount = 0;
	int32 ViewCount = 0;
	double FloorAreaSquareMeters = 0.0;
	for (AUESplattingCaptureVolume* Volume : Volumes)
	{
		StationCount += Volume->LastPreviewStationCount;
		ViewCount += Volume->LastPreviewViewCount;
		if (Volume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
		{
			FloorAreaSquareMeters += Volume->GetEstimatedFloorAreaSquareMeters();
		}
	}

	const float ImagesPerProbe = StationCount > 0
		? static_cast<float>(ViewCount) / static_cast<float>(StationCount)
		: 0.0f;
	const double ImagesPer100SquareMeters = FloorAreaSquareMeters > 0.0
		? static_cast<double>(ViewCount) / FloorAreaSquareMeters * 100.0
		: 0.0;

	if (SelectedVolumes.Num() == 1 && SelectedVolumes[0]->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
	{
		const AUESplattingCaptureVolume* Volume = SelectedVolumes[0];
		const int32 RoomStationCount = Volume->LastPreviewStationCount;
		const int32 RoomViewCount = Volume->LastPreviewViewCount;
		int32 LinkedDetailCount = 0;
		for (const AUESplattingCaptureVolume* Candidate : Volumes)
		{
			LinkedDetailCount += Candidate && Candidate != Volume
				&& Candidate->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail ? 1 : 0;
		}
		const int32 LinkedDetailStationCount = FMath::Max(0, StationCount - RoomStationCount);
		const int32 LinkedDetailViewCount = FMath::Max(0, ViewCount - RoomViewCount);
		const FVector DimensionsMeters = Volume->GetCaptureDimensionsMeters();
		const FString ProfileName = Volume->GetResolvedCaptureProfileName();
		const FString ViewSetName = StaticEnum<EUESplattingRoomCoverageViewSet>()->GetNameStringByValue(static_cast<int64>(Volume->GetResolvedRoomCoverageViewSet()));
		const int64 EstimatedStorageBytes = Volume->GetEstimatedImageStorageBytes(ViewCount);
		const double EstimatedStorageGiB = static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0 * 1024.0);
		const FString EstimatedStorage = EstimatedStorageGiB >= 1.0
			? FString::Printf(TEXT("%.1f GiB"), EstimatedStorageGiB)
			: FString::Printf(TEXT("%.0f MiB"), static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0));
		const FUESplattingCaptureCoverageStats& Coverage = Volume->LastCoverageStats;
		const int32 RequestedStationCount = Volume->GetRequestedRoomCoverageProbeCount();
		FString Summary = FString::Printf(
			TEXT("Capture volume: %.1fm x %.1fm x %.1fm\n%s density  |  %.2fm spacing  |  %d bands  |  %s\nRoom: %d accepted / %d requested probes  |  %d images  |  %d views/probe\nLinked detail: %d region%s  |  %d origins  |  %d images\nExport set: %d origins  |  %d images  |  ~%s\nAchieved room spacing: %.2fm  |  %.0f images/100m^2"),
			DimensionsMeters.X,
			DimensionsMeters.Y,
			DimensionsMeters.Z,
			*ProfileName,
			Volume->GetResolvedRoomCoverageProbeSpacingMeters(),
			Volume->GetResolvedRoomCoverageHeightBands(),
			*ViewSetName,
			RoomStationCount,
			RequestedStationCount,
			RoomViewCount,
			Volume->GetResolvedRoomCoverageViewsPerStation(),
			LinkedDetailCount,
			LinkedDetailCount == 1 ? TEXT("") : TEXT("s"),
			LinkedDetailStationCount,
			LinkedDetailViewCount,
			StationCount,
			ViewCount,
			*EstimatedStorage,
			Volume->GetAchievedRoomCoverageProbeSpacingMeters(RoomStationCount),
			ImagesPer100SquareMeters);
		if (Coverage.bSceneAwareAssessmentAvailable)
		{
			Summary += FString::Printf(
				TEXT("\nShared coverage: %.0f%%  |  Floor: %.0f%%  |  Close detail: %.0f%%  |  %d candidates"),
				Coverage.RepeatedSurfaceCoveragePercent,
				Coverage.RepeatedFloorCoveragePercent,
				Coverage.RepeatedCloseDetailCoveragePercent,
				Coverage.CandidateStationCount);
		}
		else
		{
			Summary += FString::Printf(TEXT("\nSpatial placement only  |  %d candidate probes"), Coverage.CandidateStationCount);
		}
		if (!Coverage.Warning.IsEmpty())
		{
			Summary += TEXT("\nReview: ") + Coverage.Warning;
		}
		if (Volume->HasValidReferencePostProcessSource())
		{
			Summary += TEXT("\nPhotometrics: viewport-matched global exposure with authored reference post process");
		}
		else if (Volume->ExportSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored)
		{
			Summary += TEXT("\nPhotometrics: active viewport match; preview is required before export");
		}
		else if (Volume->ExportSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked)
		{
			const FUESplattingPhotometricCalibrationResult& Calibration = Volume->LastPhotometricCalibration;
			if (Calibration.bSuccess)
			{
				Summary += FString::Printf(
					TEXT("\nExposure: %+.2f EV  |  Luma p10 / median / p90: %.0f / %.0f / %.0f"),
					Calibration.EffectiveExposureCompensation,
					Calibration.LuminanceP10,
					Calibration.LuminanceMedian,
					Calibration.LuminanceP90);
				if (!Calibration.Warning.IsEmpty())
				{
					Summary += TEXT("\nPhotometric review: ") + Calibration.Warning;
				}
			}
			else
			{
				Summary += FString::Printf(
					TEXT("\nExposure: pending representative-view calibration (target median %.0f)"),
					Volume->ExportSettings.CalibrationTargetMedianLuminance);
			}
		}
		if (ViewCount > UUESplattingDatasetExporter::LargeCaptureWarningImageCount)
		{
			Summary += TEXT("\nLarge capture: consider increasing spacing or splitting this into overlapping blocks.");
		}
		return FText::FromString(Summary);
	}

	if (SelectedVolumes.Num() == 1 && SelectedVolumes[0]->CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep)
	{
		const AUESplattingCaptureVolume* Volume = SelectedVolumes[0];
		const FVector DimensionsMeters = Volume->GetCaptureDimensionsMeters();
		const int32 RequestedStationCount = Volume->GetRequestedDirectionalArrayStationCount();
		const int32 AcceptedStationCount = Volume->LastPreviewStationCount;
		const int32 ImageCount = Volume->LastPreviewViewCount;
		const int64 EstimatedStorageBytes = Volume->GetEstimatedImageStorageBytes(ImageCount);
		const double EstimatedStorageGiB = static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0 * 1024.0);
		const FString EstimatedStorage = EstimatedStorageGiB >= 1.0
			? FString::Printf(TEXT("%.1f GiB"), EstimatedStorageGiB)
			: FString::Printf(TEXT("%.0f MiB"), static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0));
		const FRotator Aim = Volume->CaptureBounds->GetComponentRotation();
		FString Summary = FString::Printf(
			TEXT("Directional array: %.1fm wide x %.1fm high\n%.2fm camera spacing  |  %.0f deg FOV  |  one parallel view per origin\n%d accepted / %d requested cameras  |  %d images  |  ~%s\nAim: actor +X  |  yaw %.1f deg  |  pitch %.1f deg"),
			DimensionsMeters.Y,
			DimensionsMeters.Z,
			Volume->DirectionalArraySpacingMeters,
			Volume->GetResolvedHorizontalFieldOfView(),
			AcceptedStationCount,
			RequestedStationCount,
			ImageCount,
			*EstimatedStorage,
			Aim.Yaw,
			Aim.Pitch);
		if (!Volume->LastCoverageStats.Warning.IsEmpty())
		{
			Summary += TEXT("\nReview: ") + Volume->LastCoverageStats.Warning;
		}
		if (ImageCount > UUESplattingDatasetExporter::LargeCaptureWarningImageCount)
		{
			Summary += TEXT("\nLarge capture: increase camera spacing or reduce the array before export.");
		}
		return FText::FromString(Summary);
	}

	if (SelectedVolumes.Num() == 1 && SelectedVolumes[0]->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
	{
		const AUESplattingCaptureVolume* Volume = SelectedVolumes[0];
		const int32 DetailStationCount = Volume->LastPreviewStationCount;
		const int32 DetailViewCount = Volume->LastPreviewViewCount;
		const float DetailImagesPerProbe = DetailStationCount > 0
			? static_cast<float>(DetailViewCount) / static_cast<float>(DetailStationCount)
			: 0.0f;
		const FVector DimensionsMeters = Volume->GetCaptureDimensionsMeters();
		const FString ProfileName = StaticEnum<EUESplattingDetailCaptureProfile>()->GetDisplayNameTextByValue(
			static_cast<int64>(Volume->DetailCaptureProfile)).ToString();
		const int64 EstimatedStorageBytes = Volume->GetEstimatedImageStorageBytes(DetailViewCount);
		const double EstimatedStorageGiB = static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0 * 1024.0);
		const FString EstimatedStorage = EstimatedStorageGiB >= 1.0
			? FString::Printf(TEXT("%.1f GiB"), EstimatedStorageGiB)
			: FString::Printf(TEXT("%.0f MiB"), static_cast<double>(EstimatedStorageBytes) / (1024.0 * 1024.0));
		const FUESplattingCaptureCoverageStats& Coverage = Volume->LastCoverageStats;
		FString Summary = FString::Printf(
			TEXT("Detail target: %.2fm x %.2fm x %.2fm\n%s quality  |  %.1f-%.1fm camera distance  |  %.0f deg FOV%s\n%d accepted / %d candidate origins  |  %d images  |  %.1f views/origin  |  ~%s\n%d azimuth samples  |  %d elevation bands  |  %d distance rings"),
			DimensionsMeters.X,
			DimensionsMeters.Y,
			DimensionsMeters.Z,
			*ProfileName,
			Volume->DetailNearStandoffMeters,
			Volume->DetailFarStandoffMeters,
			Volume->GetResolvedHorizontalFieldOfView(),
			Volume->GetLinkedRoomCoverageVolume() ? TEXT(" (inherited from Room Coverage)") : TEXT(""),
			DetailStationCount,
			Volume->GetRequestedDetailCandidateCount(),
			DetailViewCount,
			DetailImagesPerProbe,
			*EstimatedStorage,
			Volume->GetResolvedDetailAzimuthSamples(),
			Volume->GetResolvedDetailElevationBands(),
			Volume->GetResolvedDetailDistanceRings());
		if (Coverage.bSceneAwareAssessmentAvailable)
		{
			Summary += FString::Printf(
				TEXT("\nTarget coverage: %.0f%% repeated  |  %d patches  |  %.2fm minimum baseline"),
				Coverage.RepeatedSurfaceCoveragePercent,
				Coverage.SurfacePatchCount,
				Coverage.MinimumObservationBaselineMeters);
		}
		else
		{
			Summary += TEXT("\nSpatial shell fallback: inspect target-facing arrows; no target collision evidence was found");
		}
		if (!Coverage.Warning.IsEmpty())
		{
			Summary += TEXT("\nReview: ") + Coverage.Warning;
		}
		if (const AUESplattingCaptureVolume* LinkedRoom = Volume->GetLinkedRoomCoverageVolume())
		{
			Summary += FString::Printf(
				TEXT("\nRoom Coverage: %s  |  automatic export set: %d actors, %d origins, %d images"),
				*LinkedRoom->GetActorLabel(),
				Volumes.Num(),
				StationCount,
				ViewCount);
		}
		else
		{
			Summary += TEXT("\nRoom Coverage: not assigned. Export currently contains this detail region only.");
		}
		return FText::FromString(Summary);
	}

	const AUESplattingCaptureVolume* PrimaryVolume = AUESplattingCaptureVolume::FindPrimaryCaptureVolume(Volumes);
	return FText::FromString(FString::Printf(TEXT("%d volume%s  |  %d probes  |  %d images  |  %.1f images/probe  |  %.0f images/100m^2\nPrimary settings: %s"),
		Volumes.Num(),
		Volumes.Num() == 1 ? TEXT("") : TEXT("s"),
		StationCount,
		ViewCount,
		ImagesPerProbe,
		ImagesPer100SquareMeters,
		PrimaryVolume ? *PrimaryVolume->GetActorLabel() : TEXT("none")));
}

FText FUESplattingCaptureVolumeDetails::GetOutputRootText() const
{
	const TArray<AUESplattingCaptureVolume*> SelectedVolumes = GetValidVolumes();
	TArray<AUESplattingCaptureVolume*> Volumes;
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet(SelectedVolumes, Volumes);
	if (Volumes.IsEmpty())
	{
		return FText::GetEmpty();
	}

	const AUESplattingCaptureVolume* PrimaryVolume = AUESplattingCaptureVolume::FindPrimaryCaptureVolume(Volumes);
	if (!PrimaryVolume)
	{
		return FText::GetEmpty();
	}
	const FUESplattingDatasetExportSettings& Settings = PrimaryVolume->ExportSettings;
	if (!Settings.OutputDirectory.Path.TrimStartAndEnd().IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("Output: %s"), *Settings.OutputDirectory.Path));
	}
	if (!Settings.CaptureRootDirectory.Path.TrimStartAndEnd().IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("Output root: %s"), *Settings.CaptureRootDirectory.Path));
	}

	FString EnvRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("UESPLATTING_SCENE_CAPTURE_ROOT")).TrimStartAndEnd();
	if (EnvRoot.IsEmpty())
	{
		EnvRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("NANOGS_SCENE_CAPTURE_ROOT")).TrimStartAndEnd();
	}
	if (!EnvRoot.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("Output root: %s"), *EnvRoot));
	}

	return FText::FromString(FString::Printf(TEXT("Output root: %s"), *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UESplatting"), TEXT("SceneCaptures"))));
}

#undef LOCTEXT_NAMESPACE
