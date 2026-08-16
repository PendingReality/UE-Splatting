// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Actor.h"
#include "UESplattingDatasetExporter.h"
#include "UESplattingCaptureVolume.generated.h"

UENUM(BlueprintType)
enum class EUESplattingCaptureVolumePattern : uint8
{
	RoomCoverage UMETA(DisplayName = "Room Coverage"),
	// Keep the serialized enum name for pre-release actor compatibility.
	SimpleSweep UMETA(DisplayName = "Directional Array"),
	FocusedDetail UMETA(DisplayName = "Focused Detail")
};

UENUM(BlueprintType)
enum class EUESplattingCaptureVolumeDistribution : uint8
{
	Grid UMETA(DisplayName = "Grid"),
	Halton UMETA(DisplayName = "Halton")
};

UENUM(BlueprintType)
enum class EUESplattingCaptureVolumeYawMode : uint8
{
	FourDirections UMETA(DisplayName = "4 Directions"),
	EightDirections UMETA(DisplayName = "8 Directions")
};

UENUM(BlueprintType)
enum class EUESplattingCaptureVolumePitchMode : uint8
{
	LevelOnly UMETA(DisplayName = "Level Only"),
	LevelPlusUpDown UMETA(DisplayName = "Level + Up/Down")
};

UENUM(BlueprintType)
enum class EUESplattingCaptureVolumePreviewMode : uint8
{
	StationsOnly UMETA(DisplayName = "Stations Only"),
	SampleDirections UMETA(DisplayName = "Sample Directions"),
	AllDirections UMETA(DisplayName = "All Directions")
};

UENUM(BlueprintType)
enum class EUESplattingCaptureProfile : uint8
{
	Low UMETA(DisplayName = "Low (1.5 m)"),
	Medium UMETA(DisplayName = "Medium (1.0 m)"),
	High UMETA(DisplayName = "High (0.75 m)"),
	Ultra UMETA(DisplayName = "Ultra (0.5 m)"),
	Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EUESplattingDetailCaptureProfile : uint8
{
	Low UMETA(DisplayName = "Low"),
	Medium UMETA(DisplayName = "Medium"),
	High UMETA(DisplayName = "High"),
	Ultra UMETA(DisplayName = "Ultra"),
	Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EUESplattingRoomCoverageViewSet : uint8
{
	Minimum6 UMETA(DisplayName = "Minimum 6 Views"),
	Standard8 UMETA(DisplayName = "Standard 8 Views"),
	Quality14 UMETA(DisplayName = "Quality 14 Views")
};

USTRUCT(BlueprintType)
struct FUESplattingCaptureCoverageStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	bool bSceneAwareAssessmentAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 CandidateStationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 ClearanceRejectedStationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 SurfacePatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 RepeatedSurfacePatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 FloorPatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 RepeatedFloorPatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 CloseDetailPatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	int32 RepeatedCloseDetailPatchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	float RepeatedSurfaceCoveragePercent = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	float RepeatedFloorCoveragePercent = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	float RepeatedCloseDetailCoveragePercent = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	float MinimumObservationBaselineMeters = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Coverage")
	FString Warning;
};

UCLASS(NotBlueprintable, ClassGroup = (UESplatting), HideCategories = (Activation, AssetUserData, Collision, Cooking, HLOD, Lighting, Mobile, Navigation, Physics, RayTracing, Rendering, Replication, Shape, Tags, Variable), meta = (DisplayName = "UESplatting Capture Bounds"))
class UESPLATTINGCAPTURE_API UUESplattingCaptureBoundsComponent : public UBoxComponent
{
	GENERATED_BODY()
};

class FPrimitiveSceneProxy;

UCLASS(NotBlueprintable, ClassGroup = (UESplatting), HideCategories = (Activation, AssetUserData, Collision, Cooking, HLOD, Lighting, Mobile, Navigation, Physics, RayTracing, Rendering, Replication, Tags, Variable), meta = (DisplayName = "UESplatting Probe Preview"))
class UESPLATTINGCAPTURE_API UUESplattingCapturePreviewComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UUESplattingCapturePreviewComponent();

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool GetIgnoreBoundsForEditorFocus() const override { return true; }
};

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "UESplatting Scene Capture Volume"))
class UESPLATTINGCAPTURE_API AUESplattingCaptureVolume : public AActor
{
	GENERATED_BODY()

public:
	AUESplattingCaptureVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Capture Volume")
	TObjectPtr<UBoxComponent> CaptureBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "UESplatting|Preview")
	TObjectPtr<UUESplattingCapturePreviewComponent> ProbePreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Output", meta = (ShowOnlyInnerProperties))
	FUESplattingDatasetExportSettings ExportSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout")
	EUESplattingCaptureVolumePattern CapturePattern = EUESplattingCaptureVolumePattern::RoomCoverage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Room Coverage", meta = (EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage"))
	EUESplattingCaptureProfile CaptureProfile = EUESplattingCaptureProfile::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Room Coverage", meta = (EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && CaptureProfile == EUESplattingCaptureProfile::Custom", EditConditionHides))
	EUESplattingRoomCoverageViewSet RoomCoverageViewSet = EUESplattingRoomCoverageViewSet::Standard8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Room Coverage", meta = (ClampMin = "0.25", ClampMax = "10.0", UIMin = "0.25", UIMax = "3.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && CaptureProfile == EUESplattingCaptureProfile::Custom", EditConditionHides, DisplayName = "Probe Spacing (m)"))
	float RoomCoverageCustomProbeSpacingMeters = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Room Coverage", meta = (ClampMin = "1", ClampMax = "5", UIMin = "1", UIMax = "5", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && CaptureProfile == EUESplattingCaptureProfile::Custom", EditConditionHides))
	int32 RoomCoverageHeightBands = 3;

	/** Room capture that owns output and render settings for this additive detail region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Room Coverage"))
	TObjectPtr<AUESplattingCaptureVolume> RoomCoverageVolume = nullptr;

	/** Quality of the translated camera shell generated outside a focused target box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides))
	EUESplattingDetailCaptureProfile DetailCaptureProfile = EUESplattingDetailCaptureProfile::Medium;

	/** Distance from the target box surface to the closest detail-camera ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.25", UIMax = "3.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Close Camera Distance (m)"))
	float DetailNearStandoffMeters = 0.5f;

	/** Distance from the target box surface to the wider context-camera ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "0.1", ClampMax = "20.0", UIMin = "0.5", UIMax = "5.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Context Camera Distance (m)"))
	float DetailFarStandoffMeters = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "4", ClampMax = "96", UIMin = "8", UIMax = "48", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail && DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom", EditConditionHides, DisplayName = "Azimuth Samples"))
	int32 DetailCustomAzimuthSamples = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "1", ClampMax = "8", UIMin = "2", UIMax = "5", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail && DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom", EditConditionHides, DisplayName = "Elevation Bands"))
	int32 DetailCustomElevationBands = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "1", ClampMax = "3", UIMin = "1", UIMax = "3", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail && DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom", EditConditionHides, DisplayName = "Distance Rings"))
	int32 DetailCustomDistanceRings = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "1", ClampMax = "5", UIMin = "1", UIMax = "3", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail && DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom", EditConditionHides, DisplayName = "Views Per Camera Origin"))
	int32 DetailCustomViewsPerStation = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "-60.0", ClampMax = "60.0", UIMin = "-30.0", UIMax = "15.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Minimum Elevation"))
	float DetailMinimumElevationDegrees = -15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "0.0", ClampMax = "85.0", UIMin = "30.0", UIMax = "75.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Maximum Elevation"))
	float DetailMaximumElevationDegrees = 60.0f;

	/** Local target samples used to find visible geometry inside the detail box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "2", ClampMax = "5", UIMin = "2", UIMax = "4", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Target Sample Grid"))
	int32 DetailTargetSampleGrid = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "0.05", ClampMax = "2.0", UIMin = "0.1", UIMax = "0.75", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Target Patch Size (m)"))
	float DetailTargetPatchSizeMeters = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "2", ClampMax = "8", UIMin = "2", UIMax = "5", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Required Target Observations"))
	int32 DetailRequiredObservations = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Camera Layout|Focused Detail", meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.1", UIMax = "1.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail", EditConditionHides, DisplayName = "Minimum Target Baseline (m)"))
	float DetailMinimumBaselineMeters = 0.25f;

	/** Nominal world-space spacing between cameras in the directional Y/Z array. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout|Directional Array", meta = (ClampMin = "0.1", ClampMax = "1000.0", UIMin = "0.25", UIMax = "25.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep", EditConditionHides, DisplayName = "Camera Spacing (m)"))
	float DirectionalArraySpacingMeters = 1.0f;


	/** Legacy pre-release Simple Sweep setting retained only for serialized compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	EUESplattingCaptureVolumeDistribution Distribution = EUESplattingCaptureVolumeDistribution::Halton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	float StationSpacing = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	int32 HaltonStationCount = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	EUESplattingCaptureVolumeYawMode YawMode = EUESplattingCaptureVolumeYawMode::FourDirections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	EUESplattingCaptureVolumePitchMode PitchMode = EUESplattingCaptureVolumePitchMode::LevelOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Legacy Simple Sweep")
	float PitchAngleDegrees = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Camera Layout", meta = (ClampMin = "1.0", ClampMax = "179.0", UIMin = "60.0", UIMax = "110.0"))
	float HorizontalFieldOfView = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Volume")
	FString ZoneId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Volume")
	FString BlockId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0", DisplayName = "Overlap Margin (m)"))
	float OverlapMarginMeters = 0.0f;

	/** Optional camera whose post-process settings are copied onto every generated probe view. Leave empty to rely on project defaults and post-process volumes at each probe location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (AllowedClasses = "/Script/Engine.PostProcessVolume,/Script/Engine.CameraActor,/Script/CinematicCamera.CineCameraActor"))
	TObjectPtr<AActor> ReferencePostProcessCamera = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Filtering")
	bool bFilterByClearance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Filtering", meta = (ClampMin = "0.0", UIMin = "10.0", UIMax = "200.0", EditCondition = "bFilterByClearance"))
	float CameraClearanceRadius = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Filtering", meta = (EditCondition = "bFilterByClearance"))
	TEnumAsByte<ECollisionChannel> ClearanceChannel = ECC_Visibility;

	/** Select room probes by repeated visible-surface coverage and spatial baseline after clearance filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage"))
	bool bUseSceneAwarePlacement = true;

	/** Size of the local surface patches used for coverage scoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (ClampMin = "0.25", ClampMax = "5.0", UIMin = "0.5", UIMax = "2.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && bUseSceneAwarePlacement", DisplayName = "Surface Patch Size (m)"))
	float CoveragePatchSizeMeters = 1.0f;

	/** Minimum translated-camera baseline for two observations to count as distinct coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (ClampMin = "0.1", ClampMax = "5.0", UIMin = "0.25", UIMax = "2.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && bUseSceneAwarePlacement", DisplayName = "Minimum Coverage Baseline (m)"))
	float CoverageMinimumBaselineMeters = 0.75f;

	/** Distinct baseline-separated origins desired for ordinary surface patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (ClampMin = "2", ClampMax = "8", UIMin = "2", UIMax = "5", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && bUseSceneAwarePlacement"))
	int32 CoverageRequiredObservations = 3;

	/** Distinct baseline-separated origins desired for floor patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (ClampMin = "2", ClampMax = "8", UIMin = "3", UIMax = "6", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && bUseSceneAwarePlacement"))
	int32 FloorCoverageRequiredObservations = 3;

	/** Surface hits within this distance are rewarded as close-detail observations, never rejected for proximity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coverage", meta = (ClampMin = "0.25", ClampMax = "5.0", UIMin = "0.5", UIMax = "3.0", EditCondition = "CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage && bUseSceneAwarePlacement", DisplayName = "Close Detail Distance (m)"))
	float CloseDetailDistanceMeters = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Preview")
	bool bShowProbePreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Preview")
	EUESplattingCaptureVolumePreviewMode PreviewMode = EUESplattingCaptureVolumePreviewMode::SampleDirections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Preview", meta = (ClampMin = "0", UIMin = "0", UIMax = "512", EditCondition = "PreviewMode != EUESplattingCaptureVolumePreviewMode::StationsOnly"))
	int32 PreviewDirectionLimit = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Preview", meta = (ClampMin = "1.0", UIMin = "4.0", UIMax = "40.0"))
	float PreviewProbeRadius = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Preview", meta = (ClampMin = "1.0", UIMin = "25.0", UIMax = "500.0", EditCondition = "PreviewMode != EUESplattingCaptureVolumePreviewMode::StationsOnly"))
	float PreviewDirectionLength = 120.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "UESplatting|Preview")
	int32 LastPreviewStationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "UESplatting|Preview")
	int32 LastPreviewViewCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "UESplatting|Coverage")
	FUESplattingCaptureCoverageStats LastCoverageStats;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "UESplatting|Capture Quality")
	FUESplattingPhotometricCalibrationResult LastPhotometricCalibration;

	UFUNCTION(BlueprintCallable, Category = "UESplatting|Actions", meta = (DisplayName = "Export Scene Capture Dataset"))
	void ExportColmapDataset();

	UFUNCTION(BlueprintCallable, Category = "UESplatting|Actions", meta = (DisplayName = "Calibrate Capture Exposure"))
	void CalibrateCaptureExposure();

	UFUNCTION(BlueprintCallable, Category = "UESplatting|Actions")
	void RefreshPreviewStats();
	void ConfigureAsFocusedDetailRegion();
	void ConfigureAsDirectionalArray();

	AUESplattingCaptureVolume* GetLinkedRoomCoverageVolume() const;
	void GetLinkedFocusedDetailRegions(TArray<AUESplattingCaptureVolume*>& OutDetailRegions) const;
	static void ExpandCaptureVolumeSet(
		const TArray<AUESplattingCaptureVolume*>& RequestedVolumes,
		TArray<AUESplattingCaptureVolume*>& OutCaptureVolumes);
	static void ExportCaptureVolumeSet(const TArray<AUESplattingCaptureVolume*>& RequestedVolumes);
	int32 GenerateCaptureViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats = nullptr) const;
	const TArray<FUESplattingCaptureView>& GetCachedPreviewCaptureViews() const { return CachedPreviewCaptureViews; }
	void ApplyCaptureScopeToExportSettings(FUESplattingDatasetExportSettings& InOutSettings) const;
	static void ApplyCombinedCaptureScopeToExportSettings(
		const TArray<AUESplattingCaptureVolume*>& CaptureVolumes,
		FUESplattingDatasetExportSettings& InOutSettings);
	static AUESplattingCaptureVolume* FindPrimaryCaptureVolume(const TArray<AUESplattingCaptureVolume*>& CaptureVolumes);
	FVector GetCaptureDimensionsMeters() const;
	FBox GetCaptureWorldBoundsMeters() const;
	FString GetResolvedCaptureProfileName() const;
	double GetEstimatedFloorAreaSquareMeters() const;
	float GetResolvedRoomCoverageProbeSpacingMeters() const;
	EUESplattingRoomCoverageViewSet GetResolvedRoomCoverageViewSet() const;
	int32 GetResolvedRoomCoverageHeightBands() const;
	float GetResolvedHorizontalFieldOfView() const;
	int32 GetResolvedDetailAzimuthSamples() const;
	int32 GetResolvedDetailElevationBands() const;
	int32 GetResolvedDetailDistanceRings() const;
	int32 GetResolvedDetailViewsPerStation() const;
	int32 GetRequestedDetailCandidateCount() const;
	int32 GetRequestedDirectionalArrayStationCount() const;
	int32 GetRequestedCaptureStationCount() const;
	FString GetCaptureGroupId() const;
	FString GetCaptureGroupKind() const;
	int32 GetResolvedRoomCoverageViewsPerStation() const;
	int32 GetRequestedRoomCoverageProbeCount() const;
	float GetAchievedRoomCoverageProbeSpacingMeters(int32 AcceptedStationCount) const;
	int64 GetEstimatedImageStorageBytes(int32 ImageCount) const;
	bool HasValidReferencePostProcessSource() const;
	bool PrepareViewportMatchedPhotometrics(FUESplattingDatasetExportSettings& InOutSettings, bool bConfirmBeforeExport);
	void ApplyReferencePostProcessToViews(TArray<FUESplattingCaptureView>& InOutViews) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	TArray<FUESplattingCaptureView> CachedPreviewCaptureViews;
	FTSTicker::FDelegateHandle PreviewRefreshTickerHandle;
	void ExportSingleCaptureVolume();
	void RequestPreviewRefresh();
	void UpdatePreviewStats();
	bool CalibrateCaptureExposureForViews(const TArray<FUESplattingCaptureView>& CaptureViews, bool bConfirmBeforeExport);
	void ApplyReferencePostProcess(TArray<FUESplattingCaptureView>& InOutViews) const;
	int32 GenerateRoomCoverageViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats) const;
	int32 GenerateFocusedDetailViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats) const;
	int32 GenerateDirectionalArrayViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats) const;
	FVector GetEffectiveLocalExtent() const;
	bool IsStationClear(const FVector& WorldLocation) const;

	static double Halton(int32 Index, int32 Base);
};
