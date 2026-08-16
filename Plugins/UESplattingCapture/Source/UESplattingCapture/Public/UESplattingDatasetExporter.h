// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UESplattingDatasetExporter.generated.h"

class AActor;
class UObject;

using FUESplattingDatasetExportCompleted = TFunction<void(const struct FUESplattingDatasetExportResult&)>;

UENUM(BlueprintType)
enum class EUESplattingSceneCaptureLightingMethod : uint8
{
	ProjectSettings UMETA(DisplayName = "Project Settings"),
	ForceLumen UMETA(DisplayName = "Force Lumen"),
	DisableDynamicGI UMETA(DisplayName = "Disable Dynamic GI")
};

UENUM(BlueprintType)
enum class EUESplattingSceneCaptureImageFormat : uint8
{
	JPEG UMETA(DisplayName = "JPEG"),
	PNG UMETA(DisplayName = "PNG")
};

UENUM(BlueprintType)
enum class EUESplattingSceneCaptureRenderer : uint8
{
	MovieRenderQueue UMETA(DisplayName = "Movie Render Queue (Recommended)"),
	SceneCapture2DLegacy UMETA(DisplayName = "SceneCapture2D (Legacy)")
};

UENUM(BlueprintType)
enum class EUESplattingSceneCapturePhotometricMode : uint8
{
	CalibratedLocked UMETA(DisplayName = "Calibrated Locked (Training)"),
	LockedManual UMETA(DisplayName = "Explicit Locked Exposure"),
	SceneAuthored UMETA(DisplayName = "Scene Authored (Recommended)")
};

USTRUCT(BlueprintType)
struct FUESplattingCaptureView
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Dataset")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Dataset", meta = (ClampMin = "1.0", ClampMax = "179.0", UIMin = "30.0", UIMax = "120.0"))
	float HorizontalFieldOfView = 90.0f;

	/** Stable capture-station identity used by dataset metadata and coverage analysis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Dataset")
	int32 StationIndex = INDEX_NONE;

	/** Stable source group for mixed room/detail captures. Local station indices are unique only inside this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Dataset")
	FString CaptureGroupId;

	/** Human-readable group role such as room, focused_detail, or directional_array. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Dataset")
	FString CaptureGroupKind;

	FPostProcessSettings PostProcessSettings;
	// Keep generated probe views eligible for capture-level renderer overrides such as Lumen GI/reflections.
	float PostProcessBlendWeight = 1.0f;
	FString DebugName;
};

USTRUCT(BlueprintType)
struct FUESplattingDatasetExportSettings
{
	GENERATED_BODY()

	/** Image renderer used for the dataset. MRQ uses Unreal's normal deferred camera pipeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality")
	EUESplattingSceneCaptureRenderer Renderer = EUESplattingSceneCaptureRenderer::MovieRenderQueue;

	/** Pause normal gameplay simulation for the MRQ shot while keeping the capture camera updating. Tick-when-paused and real-time-driven systems may still advance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (EditCondition = "Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue"))
	bool bFreezeSceneDuringCapture = false;

	/** Capture id used when OutputDirectory is empty. If empty, a timestamped unreal_* id is generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Output")
	FString CaptureId;

	/** Root directory used with CaptureId when OutputDirectory is empty. Falls back to UESPLATTING_SCENE_CAPTURE_ROOT, the legacy NANOGS_SCENE_CAPTURE_ROOT, then Project Saved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Output")
	FDirectoryPath CaptureRootDirectory;

	/** Exact output capture folder. If set, this overrides CaptureRootDirectory and CaptureId folder generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Output")
	FDirectoryPath OutputDirectory;

	/** Freeform description of how the capture views were generated. Recorded in capture-manifest.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output", meta = (MultiLine = "true"))
	FString CapturePatternNotes;

	/** Capture-volume density preset, or the layout label for non-room exports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output")
	FString CaptureProfile;

	/** Optional logical zone name for block/room-oriented dataset exports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output")
	FString CaptureZoneId;

	/** Optional block/cell id for partitioned large-scene exports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output")
	FString CaptureBlockId;

	/** World-space overlap margin in meters for block/cell-oriented exports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	double CaptureOverlapMarginMeters = 0.0;

	/** Whether CaptureWorldBoundsMinMeters/MaxMeters contain valid Unreal world-space bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output")
	bool bHasCaptureWorldBounds = false;

	/** Unreal world-space capture bounds minimum in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output", meta = (EditCondition = "bHasCaptureWorldBounds"))
	FVector CaptureWorldBoundsMinMeters = FVector::ZeroVector;

	/** Unreal world-space capture bounds maximum in meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Output", meta = (EditCondition = "bHasCaptureWorldBounds"))
	FVector CaptureWorldBoundsMaxMeters = FVector::ZeroVector;

	/** Captured image width in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "16", UIMin = "256", UIMax = "4096"))
	int32 ImageWidth = 1920;

	/** Captured image height in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "16", UIMin = "256", UIMax = "4096"))
	int32 ImageHeight = 1080;

	/** RGB image file format. JPEG is the practical default for training datasets; use PNG for lossless references or alpha/mask-style outputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality")
	EUESplattingSceneCaptureImageFormat ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;

	/** JPEG quality. UE 5.8's built-in JPEG path does not expose chroma-subsampling control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "50", ClampMax = "100", UIMin = "85", UIMax = "100", EditCondition = "ImageFormat == EUESplattingSceneCaptureImageFormat::JPEG"))
	int32 JpegQuality = 94;

	/** Dynamic GI/reflection method to force onto SceneCapture2D. Scene captures default to no Lumen unless this is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality")
	EUESplattingSceneCaptureLightingMethod LightingMethod = EUESplattingSceneCaptureLightingMethod::ProjectSettings;

	/** Preserve the authored project, post-process volume, and reference-camera look by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality")
	EUESplattingSceneCapturePhotometricMode PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;

	/** Fixed logarithmic exposure compensation. Calibrated Locked writes its solved value here; Explicit Locked uses it directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "-15.0", ClampMax = "15.0", UIMin = "-8.0", UIMax = "8.0", EditCondition = "PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored"))
	float ManualExposureCompensation = 0.0f;

	/** Target median of representative frame mean-luminance values on the exported 0-255 display scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (ClampMin = "32.0", ClampMax = "192.0", UIMin = "64.0", UIMax = "128.0", EditCondition = "PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked"))
	float CalibrationTargetMedianLuminance = 82.0f;

	/** Accepted amount below target; underexposure receives the tighter tolerance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (ClampMin = "2.0", ClampMax = "32.0", UIMin = "6.0", UIMax = "20.0", EditCondition = "PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked"))
	float CalibrationDarkTolerance = 8.0f;

	/** Accepted amount above target; avoids chasing exposure-insensitive sky and emissive signal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (ClampMin = "2.0", ClampMax = "32.0", UIMin = "6.0", UIMax = "20.0", EditCondition = "PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked"))
	float CalibrationBrightTolerance = 12.0f;

	/** Number of spatially and directionally distributed views used by the exposure preflight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (ClampMin = "8", ClampMax = "64", UIMin = "12", UIMax = "32", EditCondition = "PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked"))
	int32 CalibrationSampleViewCount = 24;

	/** Fixed white-balance temperature in Kelvin used by both locked training modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "1500.0", ClampMax = "15000.0", UIMin = "2500.0", UIMax = "10000.0", EditCondition = "PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored"))
	float WhiteBalanceTemperature = 6500.0f;

	/** Fixed magenta/green tint used by both locked training modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Capture Quality", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0", EditCondition = "PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored"))
	float WhiteBalanceTint = 0.0f;

	/** Frames rendered at each legacy SceneCapture2D viewpoint before saving so temporal history can settle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (DisplayName = "Scene Capture Warmup Frames Per View", ClampMin = "0", UIMin = "0", UIMax = "8", EditCondition = "Renderer == EUESplattingSceneCaptureRenderer::SceneCapture2DLegacy"))
	int32 CaptureWarmupFrames = 2;

	/** Let legacy scene captures use ray tracing features when the project/renderer supports them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality")
	bool bUseRayTracingIfEnabled = false;

	/** Honor authored eye adaptation in Scene Authored mode. Both locked modes disable temporal adaptation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (EditCondition = "PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored"))
	bool bUseEyeAdaptation = true;

	/** Keep one legacy SceneCapture view state alive across the export. Required for Lumen, but uses more renderer memory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality")
	bool bPersistCaptureRenderingState = false;

	/** Run garbage collection periodically during long editor exports. 0 disables this maintenance pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Capture Quality", meta = (ClampMin = "0", UIMin = "0", UIMax = "256"))
	int32 GarbageCollectEveryImages = 64;

	/** Generate an optional trainer seed from Unreal physics-collision hits. This is not rendered-surface ground truth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UESplatting|Experimental Seed Cloud", meta = (DisplayName = "Generate Collision Seed Cloud (Experimental)"))
	bool bGenerateTracePointCloud = false;

	/** Pixel step between collision-ray samples. Larger values are faster and produce fewer seed points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Experimental Seed Cloud", meta = (ClampMin = "1", UIMin = "8", UIMax = "256", EditCondition = "bGenerateTracePointCloud", DisplayName = "Sample Step (Pixels)"))
	int32 TracePixelStep = 56;

	/** Maximum physics-collision trace distance in Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Experimental Seed Cloud", meta = (ClampMin = "1.0", UIMin = "1000.0", UIMax = "1000000.0", EditCondition = "bGenerateTracePointCloud", DisplayName = "Maximum Trace Distance (cm)"))
	float TraceMaxDistance = 1000000.0f;

	/** Collision channel used for seed-point traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Experimental Seed Cloud", meta = (EditCondition = "bGenerateTracePointCloud"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Trace against complex physics collision where available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Experimental Seed Cloud", meta = (EditCondition = "bGenerateTracePointCloud"))
	bool bTraceComplex = true;

	/** Scale from Unreal centimeters to exported dataset units. 0.01 exports meters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "UESplatting|Coordinates", meta = (ClampMin = "0.000001", UIMin = "0.001", UIMax = "1.0", DisplayName = "World to Dataset Scale"))
	double WorldToColmapScale = 0.01;

	// Filled by the capture-volume preflight and recorded in the manifest.
	bool bViewportExposureMatched = false;
	float ViewportExposureScale = 0.0f;
	FString ViewportExposureSource;
	bool bPhotometricCalibrationPerformed = false;
	float CalibrationInitialExposureCompensation = 0.0f;
	float CalibrationLuminanceP10 = 0.0f;
	float CalibrationLuminanceMedian = 0.0f;
	float CalibrationLuminanceP90 = 0.0f;
	int32 CalibrationViewsEvaluated = 0;
	int32 RequestedProbeCount = 0;
	int32 AcceptedProbeCount = 0;
	int32 CandidateProbeCount = 0;
	int32 ClearanceRejectedProbeCount = 0;
	int32 SurfacePatchCount = 0;
	int32 RepeatedSurfacePatchCount = 0;
	int32 FloorPatchCount = 0;
	int32 RepeatedFloorPatchCount = 0;
	int32 CloseDetailPatchCount = 0;
	int32 RepeatedCloseDetailPatchCount = 0;
	float RepeatedSurfaceCoveragePercent = 0.0f;
	float RepeatedFloorCoveragePercent = 0.0f;
	float RepeatedCloseDetailCoveragePercent = 0.0f;
	float MinimumCoverageBaselineMeters = 0.0f;
};

USTRUCT(BlueprintType)
struct FUESplattingPhotometricCalibrationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	float EffectiveExposureCompensation = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	float LuminanceP10 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	float LuminanceMedian = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	float LuminanceP90 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	int32 SampleViewCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	FString PreviewImagePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Photometrics")
	FString Warning;
};

USTRUCT(BlueprintType)
struct FUESplattingDatasetExportResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Dataset")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Dataset")
	FString OutputDirectory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Dataset")
	int32 ImageCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Dataset")
	int32 SparsePointCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UESplatting|Dataset")
	FString Message;
};

UCLASS(meta = (DisplayName = "UESplatting Dataset Exporter"))
class UESPLATTINGCAPTURE_API UUESplattingDatasetExporter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static constexpr int32 LargeCaptureWarningImageCount = 1500;

	/** Convert group-local station ids into one contiguous dataset-wide station id space. */
	static int32 NormalizeCaptureStationIndices(TArray<FUESplattingCaptureView>& InOutCaptureViews);

#if WITH_DEV_AUTOMATION_TESTS
	/** Build transforms.json text without rendering so serialization contracts can be tested directly. */
	static FString BuildTransformsJsonForAutomationTests(
		const TArray<FUESplattingCaptureView>& CaptureViews,
		const FUESplattingDatasetExportSettings& Settings,
		bool bHasPointCloud = false);
#endif

	/** Render a small representative preflight and solve one global exposure compensation. */
	static bool CalibrateGlobalExposure(
		UObject* WorldContextObject,
		const TArray<FUESplattingCaptureView>& CaptureViews,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingPhotometricCalibrationResult& Result);

	/** Resolve the active perspective level viewport's current exposure and camera pose. */
	static bool ResolveActiveViewportExposure(
		UObject* WorldContextObject,
		FUESplattingDatasetExportSettings& InOutSettings,
		FUESplattingCaptureView& OutViewportView,
		FString& OutError);

	/** Resolve only the active perspective level viewport's camera pose and FOV. */
	static bool ResolveActiveViewportView(
		UObject* WorldContextObject,
		FUESplattingCaptureView& OutViewportView,
		FString& OutError);

	/** Render one viewport-matched frame through the exact SceneCapture dataset path. */
	static bool CaptureViewportMatchedPreview(
		UObject* WorldContextObject,
		const FUESplattingCaptureView& ViewportView,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingPhotometricCalibrationResult& Result);

	/** Ask for confirmation before an unusually large editor capture. */
	static bool ConfirmLargeCaptureIfNeeded(int32 ImageCount);

	/**
	 * Start a multi-frame known-pose dataset export from explicit camera views.
	 * Result reports whether the job started; completion is reported through the editor UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "UESplatting|Dataset Export", meta = (WorldContext = "WorldContextObject"))
	static bool ExportColmapDatasetFromCaptureViews(
		UObject* WorldContextObject,
		const TArray<FUESplattingCaptureView>& CaptureViews,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportResult& Result);

	/** C++ entry point used by editor tools that need a reliable completion callback. */
	static bool StartColmapDatasetExportFromCaptureViews(
		UObject* WorldContextObject,
		const TArray<FUESplattingCaptureView>& CaptureViews,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportCompleted Completion,
		FUESplattingDatasetExportResult& StartResult);

	/** Export a known-pose scene capture dataset from the supplied camera actors. */
	UFUNCTION(BlueprintCallable, Category = "UESplatting|Dataset Export")
	static bool ExportColmapDatasetFromCameraActors(
		const TArray<AActor*>& CameraActors,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportResult& Result);

	static bool StartColmapDatasetExportFromCameraActors(
		const TArray<AActor*>& CameraActors,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportCompleted Completion,
		FUESplattingDatasetExportResult& StartResult);

	/** Export a known-pose scene capture dataset from selected camera actors in the editor. */
	UFUNCTION(BlueprintCallable, Category = "UESplatting|Dataset Export")
	static bool ExportColmapDatasetFromSelectedCameras(
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportResult& Result);

	static bool StartColmapDatasetExportFromSelectedCameras(
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportCompleted Completion,
		FUESplattingDatasetExportResult& StartResult);
};
