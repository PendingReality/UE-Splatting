// SPDX-License-Identifier: MIT

#include "UESplattingDatasetExporter.h"
#include "UESplattingLog.h"
#include "UESplattingCaptureVolume.h"
#include "UESplattingCaptureTimeStep.h"

#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/Selection.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Interfaces/IPluginManager.h"
#include "LevelSequence.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "MovieJobVariableAssignmentContainer.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphPin.h"
#include "Graph/MovieGraphQuickRender.h"
#include "Graph/MovieGraphQuickRenderSettings.h"
#include "Graph/Nodes/MovieGraphCameraNode.h"
#include "Graph/Nodes/MovieGraphSamplingMethodNode.h"
#include "Graph/Nodes/MovieGraphWarmUpSettingNode.h"
#include "MovieGraphImageSequenceOutputNode.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "SequencerUtilities.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SceneManagement.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace UESplattingDataset
{
	struct FColmapQuaternion
	{
		double W = 1.0;
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
	};

	struct FColmapPose
	{
		FColmapQuaternion Rotation;
		FVector3d Translation = FVector3d::ZeroVector;
	};

	struct FPointObservation
	{
		double X = 0.0;
		double Y = 0.0;
		int64 PointId = -1;
	};

	struct FExportedImage
	{
		int32 ImageId = 0;
		int32 CameraId = 0;
		int32 StationIndex = INDEX_NONE;
		FString CaptureGroupId;
		FString CaptureGroupKind;
		FString Name;
		FTransform CameraToWorld = FTransform::Identity;
		FColmapPose Pose;
		double FocalLength = 0.0;
		float HorizontalFieldOfView = 90.0f;
		TArray<FPointObservation> Observations;
	};

	struct FExportedPoint
	{
		int64 PointId = 0;
		FVector3d Position = FVector3d::ZeroVector;
		FColor Color = FColor::White;
		int32 ImageId = 0;
		int32 Point2DIndex = 0;
	};

	static FString FloatString(double Value)
	{
		return FString::Printf(TEXT("%.17g"), Value);
	}

	static FString VectorString(const FVector3d& Value)
	{
		return FString::Printf(TEXT("%s %s %s"), *FloatString(Value.X), *FloatString(Value.Y), *FloatString(Value.Z));
	}

	static FString JsonPathString(const FString& Path)
	{
		return Path.Replace(TEXT("\\"), TEXT("/"));
	}

	static FString SanitizeCaptureId(const FString& CaptureId)
	{
		FString Sanitized = FPaths::MakeValidFileName(CaptureId.TrimStartAndEnd());
		if (Sanitized.IsEmpty())
		{
			Sanitized = FDateTime::Now().ToString(TEXT("unreal_%Y%m%d_%H%M%S"));
		}
		return Sanitized;
	}

	static FString ResolveCaptureId(const FUESplattingDatasetExportSettings& Settings, const FString& OutputDirectory)
	{
		if (!Settings.CaptureId.TrimStartAndEnd().IsEmpty())
		{
			return SanitizeCaptureId(Settings.CaptureId);
		}

		if (!OutputDirectory.TrimStartAndEnd().IsEmpty())
		{
			const FString FolderName = FPaths::GetCleanFilename(OutputDirectory);
			if (!FolderName.IsEmpty())
			{
				return SanitizeCaptureId(FolderName);
			}
		}

		return SanitizeCaptureId(TEXT(""));
	}

	static FVector3d UnrealPositionToDataset(const FVector& UnrealPosition, double UnitScale)
	{
		return FVector3d(UnrealPosition.X, -UnrealPosition.Y, UnrealPosition.Z) * UnitScale;
	}

	static FVector3d UnrealVectorToDataset(const FVector& UnrealVector)
	{
		return FVector3d(UnrealVector.X, -UnrealVector.Y, UnrealVector.Z);
	}

	static TArray<TSharedPtr<FJsonValue>> NumberArray(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (double Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return JsonValues;
	}

	static TArray<TSharedPtr<FJsonValue>> MatrixArray(const TArray<TArray<TSharedPtr<FJsonValue>>>& Rows)
	{
		TArray<TSharedPtr<FJsonValue>> JsonRows;
		for (const TArray<TSharedPtr<FJsonValue>>& Row : Rows)
		{
			JsonRows.Add(MakeShared<FJsonValueArray>(Row));
		}
		return JsonRows;
	}

	static bool SaveJsonObject(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath, FString& OutError)
	{
		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(JsonObject, Writer))
		{
			OutError = FString::Printf(TEXT("Failed to serialize JSON '%s'."), *FilePath);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(JsonText, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to write '%s'."), *FilePath);
			return false;
		}

		return true;
	}

	static bool EnsureDirectory(const FString& Directory)
	{
		return IFileManager::Get().MakeDirectory(*Directory, true);
	}

	static bool IsDirectoryEmpty(const FString& Directory)
	{
		bool bHasEntries = false;
		IFileManager::Get().IterateDirectory(*Directory, [&bHasEntries](const TCHAR*, bool)
		{
			bHasEntries = true;
			return false;
		});
		return !bHasEntries;
	}

	static FString ResolveOutputDirectory(const FUESplattingDatasetExportSettings& Settings)
	{
		if (!Settings.OutputDirectory.Path.TrimStartAndEnd().IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(Settings.OutputDirectory.Path);
		}

		const FString CaptureId = SanitizeCaptureId(Settings.CaptureId);

		FString CaptureRoot = Settings.CaptureRootDirectory.Path.TrimStartAndEnd();
		if (CaptureRoot.IsEmpty())
		{
			CaptureRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("UESPLATTING_SCENE_CAPTURE_ROOT")).TrimStartAndEnd();
		}
		if (CaptureRoot.IsEmpty())
		{
			CaptureRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("NANOGS_SCENE_CAPTURE_ROOT")).TrimStartAndEnd();
		}
		if (CaptureRoot.IsEmpty())
		{
			CaptureRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UESplatting"), TEXT("SceneCaptures"));
		}

		return FPaths::Combine(FPaths::ConvertRelativePathToFull(CaptureRoot), CaptureId);
	}

	static UCameraComponent* ResolveCameraComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UCameraComponent>() : nullptr;
	}

	static FColmapQuaternion QuaternionFromWorldToColmapRows(
		const FVector3d& Row0,
		const FVector3d& Row1,
		const FVector3d& Row2)
	{
		const double M00 = Row0.X;
		const double M01 = Row0.Y;
		const double M02 = Row0.Z;
		const double M10 = Row1.X;
		const double M11 = Row1.Y;
		const double M12 = Row1.Z;
		const double M20 = Row2.X;
		const double M21 = Row2.Y;
		const double M22 = Row2.Z;

		FColmapQuaternion Q;
		const double Trace = M00 + M11 + M22;
		if (Trace > 0.0)
		{
			const double S = FMath::Sqrt(Trace + 1.0) * 2.0;
			Q.W = 0.25 * S;
			Q.X = (M21 - M12) / S;
			Q.Y = (M02 - M20) / S;
			Q.Z = (M10 - M01) / S;
		}
		else if (M00 > M11 && M00 > M22)
		{
			const double S = FMath::Sqrt(1.0 + M00 - M11 - M22) * 2.0;
			Q.W = (M21 - M12) / S;
			Q.X = 0.25 * S;
			Q.Y = (M01 + M10) / S;
			Q.Z = (M02 + M20) / S;
		}
		else if (M11 > M22)
		{
			const double S = FMath::Sqrt(1.0 + M11 - M00 - M22) * 2.0;
			Q.W = (M02 - M20) / S;
			Q.X = (M01 + M10) / S;
			Q.Y = 0.25 * S;
			Q.Z = (M12 + M21) / S;
		}
		else
		{
			const double S = FMath::Sqrt(1.0 + M22 - M00 - M11) * 2.0;
			Q.W = (M10 - M01) / S;
			Q.X = (M02 + M20) / S;
			Q.Y = (M12 + M21) / S;
			Q.Z = 0.25 * S;
		}

		const double Length = FMath::Sqrt(Q.W * Q.W + Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z);
		if (Length > SMALL_NUMBER)
		{
			Q.W /= Length;
			Q.X /= Length;
			Q.Y /= Length;
			Q.Z /= Length;
		}
		return Q;
	}

	static FColmapPose BuildColmapPose(const FTransform& CameraToWorld, double WorldToColmapScale)
	{
		const FVector3d CameraCenter = UnrealPositionToDataset(CameraToWorld.GetLocation(), WorldToColmapScale);

		const FVector3d Forward = UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::X));
		const FVector3d Right = UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::Y));
		const FVector3d Up = UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::Z));

		const FVector3d Row0 = Right;
		const FVector3d Row1 = -Up;
		const FVector3d Row2 = Forward;

		FColmapPose Pose;
		Pose.Rotation = QuaternionFromWorldToColmapRows(Row0, Row1, Row2);
		Pose.Translation = FVector3d(
			-FVector3d::DotProduct(Row0, CameraCenter),
			-FVector3d::DotProduct(Row1, CameraCenter),
			-FVector3d::DotProduct(Row2, CameraCenter));
		return Pose;
	}

	static double ResolveHorizontalFovDegrees(float HorizontalFieldOfView)
	{
		return FMath::Clamp(static_cast<double>(HorizontalFieldOfView), 0.001, 179.0);
	}

	static double ResolveFocalLengthPixels(float HorizontalFieldOfView, int32 Width)
	{
		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(ResolveHorizontalFovDegrees(HorizontalFieldOfView)) * 0.5);
		return static_cast<double>(Width) / (2.0 * TanHalfHorizontal);
	}

	struct FCameraIntrinsicsSummary
	{
		bool bHasImages = false;
		bool bUniform = false;
		int32 UniqueIntrinsicsCount = 0;
		double MinFocalLength = 0.0;
		double MaxFocalLength = 0.0;
		double MinHorizontalFovDegrees = 0.0;
		double MaxHorizontalFovDegrees = 0.0;
	};

	static FCameraIntrinsicsSummary SummarizeCameraIntrinsics(const TArray<FExportedImage>& Images)
	{
		FCameraIntrinsicsSummary Summary;
		if (Images.IsEmpty())
		{
			return Summary;
		}

		Summary.bHasImages = true;
		Summary.bUniform = true;
		Summary.MinFocalLength = Images[0].FocalLength;
		Summary.MaxFocalLength = Images[0].FocalLength;
		Summary.MinHorizontalFovDegrees = ResolveHorizontalFovDegrees(Images[0].HorizontalFieldOfView);
		Summary.MaxHorizontalFovDegrees = Summary.MinHorizontalFovDegrees;

		for (int32 ImageIndex = 0; ImageIndex < Images.Num(); ++ImageIndex)
		{
			const FExportedImage& Image = Images[ImageIndex];
			const double FovDegrees = ResolveHorizontalFovDegrees(Image.HorizontalFieldOfView);
			Summary.MinFocalLength = FMath::Min(Summary.MinFocalLength, Image.FocalLength);
			Summary.MaxFocalLength = FMath::Max(Summary.MaxFocalLength, Image.FocalLength);
			Summary.MinHorizontalFovDegrees = FMath::Min(Summary.MinHorizontalFovDegrees, FovDegrees);
			Summary.MaxHorizontalFovDegrees = FMath::Max(Summary.MaxHorizontalFovDegrees, FovDegrees);

			if (!FMath::IsNearlyEqual(Image.FocalLength, Images[0].FocalLength, 1.e-6)
				|| !FMath::IsNearlyEqual(FovDegrees, ResolveHorizontalFovDegrees(Images[0].HorizontalFieldOfView), 1.e-6))
			{
				Summary.bUniform = false;
			}

			bool bSeenEarlier = false;
			for (int32 EarlierIndex = 0; EarlierIndex < ImageIndex; ++EarlierIndex)
			{
				const FExportedImage& EarlierImage = Images[EarlierIndex];
				if (FMath::IsNearlyEqual(Image.FocalLength, EarlierImage.FocalLength, 1.e-6)
					&& FMath::IsNearlyEqual(FovDegrees, ResolveHorizontalFovDegrees(EarlierImage.HorizontalFieldOfView), 1.e-6))
				{
					bSeenEarlier = true;
					break;
				}
			}
			if (!bSeenEarlier)
			{
				++Summary.UniqueIntrinsicsCount;
			}
		}

		return Summary;
	}

	static FVector BuildWorldRayDirection(const FTransform& CameraToWorld, float HorizontalFieldOfView, int32 PixelX, int32 PixelY, int32 Width, int32 Height)
	{
		const double Aspect = static_cast<double>(Width) / static_cast<double>(Height);
		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(ResolveHorizontalFovDegrees(HorizontalFieldOfView)) * 0.5);
		const double TanHalfVertical = TanHalfHorizontal / Aspect;
		const double NormalizedX = ((static_cast<double>(PixelX) + 0.5) / static_cast<double>(Width)) * 2.0 - 1.0;
		const double NormalizedY = 1.0 - ((static_cast<double>(PixelY) + 0.5) / static_cast<double>(Height)) * 2.0;

		const FVector LocalDirection(1.0, NormalizedX * TanHalfHorizontal, NormalizedY * TanHalfVertical);
		return CameraToWorld.TransformVectorNoScale(LocalDirection).GetSafeNormal();
	}

	static UTextureRenderTarget2D* CreateRenderTarget(UObject* Outer, int32 Width, int32 Height)
	{
		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(Outer, NAME_None, RF_Transient);
		if (!RenderTarget)
		{
			return nullptr;
		}

		RenderTarget->TargetGamma = GEngine ? GEngine->GetDisplayGamma() : 2.2f;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		RenderTarget->UpdateResourceImmediate(true);
		return RenderTarget;
	}

	static FString GetImageFileExtension(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.ImageFormat == EUESplattingSceneCaptureImageFormat::PNG ? TEXT("png") : TEXT("jpg");
	}

	static FString GetImageFormatName(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.ImageFormat == EUESplattingSceneCaptureImageFormat::PNG ? TEXT("png") : TEXT("jpeg");
	}

	static int32 GetImageCompressionQuality(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.ImageFormat == EUESplattingSceneCaptureImageFormat::JPEG
			? FMath::Clamp(Settings.JpegQuality, 50, 100)
			: 0;
	}

	static bool EncodeRenderTargetImage(
		UTextureRenderTarget2D* RenderTarget,
		const FUESplattingDatasetExportSettings& Settings,
		const FString& OutputImagePath,
		TArray64<uint8>& OutImageData,
		FString& OutError)
	{
		FImage Image;
		if (!FImageUtils::GetRenderTargetImage(RenderTarget, Image))
		{
			OutError = FString::Printf(TEXT("Failed to read render target for '%s'."), *OutputImagePath);
			return false;
		}

		const FString Extension = GetImageFileExtension(Settings);
		if (!FImageUtils::CompressImage(OutImageData, *Extension, Image, GetImageCompressionQuality(Settings)))
		{
			OutError = FString::Printf(TEXT("Failed to encode %s image '%s'."), *GetImageFormatName(Settings), *OutputImagePath);
			return false;
		}

		return true;
	}

	static EDynamicGlobalIlluminationMethod::Type GetProjectDynamicGIMethod()
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
		const int32 Method = CVar ? CVar->GetInt() : static_cast<int32>(EDynamicGlobalIlluminationMethod::None);
		return static_cast<EDynamicGlobalIlluminationMethod::Type>(FMath::Clamp(Method, 0, static_cast<int32>(EDynamicGlobalIlluminationMethod::Plugin)));
	}

	static EReflectionMethod::Type GetProjectReflectionMethod()
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"));
		const int32 Method = CVar ? CVar->GetInt() : static_cast<int32>(EReflectionMethod::None);
		return static_cast<EReflectionMethod::Type>(FMath::Clamp(Method, 0, static_cast<int32>(EReflectionMethod::ScreenSpace)));
	}

	static bool ResolveCaptureLightingMethods(
		const FUESplattingDatasetExportSettings& Settings,
		EDynamicGlobalIlluminationMethod::Type& OutDynamicGIMethod,
		EReflectionMethod::Type& OutReflectionMethod)
	{
		switch (Settings.LightingMethod)
		{
		case EUESplattingSceneCaptureLightingMethod::ForceLumen:
			OutDynamicGIMethod = EDynamicGlobalIlluminationMethod::Lumen;
			OutReflectionMethod = EReflectionMethod::Lumen;
			return true;
		case EUESplattingSceneCaptureLightingMethod::DisableDynamicGI:
			OutDynamicGIMethod = EDynamicGlobalIlluminationMethod::None;
			OutReflectionMethod = EReflectionMethod::None;
			return false;
		case EUESplattingSceneCaptureLightingMethod::ProjectSettings:
		default:
			OutDynamicGIMethod = GetProjectDynamicGIMethod();
			OutReflectionMethod = GetProjectReflectionMethod();
			return OutDynamicGIMethod == EDynamicGlobalIlluminationMethod::Lumen || OutReflectionMethod == EReflectionMethod::Lumen;
		}
	}

	static bool UsesPersistentCaptureState(const FUESplattingDatasetExportSettings& Settings)
	{
		EDynamicGlobalIlluminationMethod::Type DynamicGIMethod = EDynamicGlobalIlluminationMethod::None;
		EReflectionMethod::Type ReflectionMethod = EReflectionMethod::None;
		const bool bUsesLumen = ResolveCaptureLightingMethods(Settings, DynamicGIMethod, ReflectionMethod);
		return Settings.bPersistCaptureRenderingState || bUsesLumen || Settings.bUseEyeAdaptation;
	}

	static bool UsesLockedExposure(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored
			|| Settings.bViewportExposureMatched;
	}

	static bool UsesExplicitWhiteBalance(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored;
	}

	static bool UsesTemporalEyeAdaptation(const FUESplattingDatasetExportSettings& Settings)
	{
		return !UsesLockedExposure(Settings) && Settings.bUseEyeAdaptation;
	}

	static bool UsesCalibratedLockedPhotometrics(const FUESplattingDatasetExportSettings& Settings)
	{
		return Settings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked;
	}

	static void ConfigureSceneCaptureLighting(USceneCaptureComponent2D* CaptureComponent, const FUESplattingDatasetExportSettings& Settings)
	{
		if (!CaptureComponent)
		{
			return;
		}

		EDynamicGlobalIlluminationMethod::Type DynamicGIMethod = EDynamicGlobalIlluminationMethod::None;
		EReflectionMethod::Type ReflectionMethod = EReflectionMethod::None;
		ResolveCaptureLightingMethods(Settings, DynamicGIMethod, ReflectionMethod);

		CaptureComponent->bAlwaysPersistRenderingState = UsesPersistentCaptureState(Settings);
		CaptureComponent->bUseRayTracingIfEnabled = Settings.bUseRayTracingIfEnabled;
		if (Settings.LightingMethod != EUESplattingSceneCaptureLightingMethod::ProjectSettings)
		{
			// Explicit lighting modes intentionally override the authored stack.
			// Project Settings leaves reference-camera and volume overrides intact.
			CaptureComponent->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
			CaptureComponent->PostProcessSettings.DynamicGlobalIlluminationMethod = DynamicGIMethod;
			CaptureComponent->PostProcessSettings.bOverride_ReflectionMethod = true;
			CaptureComponent->PostProcessSettings.ReflectionMethod = ReflectionMethod;
		}

		const bool bLockedExposure = UsesLockedExposure(Settings);
		if (bLockedExposure)
		{
			FPostProcessSettings& PostProcess = CaptureComponent->PostProcessSettings;
			PostProcess.bOverride_AutoExposureMethod = true;
			PostProcess.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			PostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			PostProcess.AutoExposureApplyPhysicalCameraExposure = false;
			PostProcess.bOverride_AutoExposureBias = true;
			PostProcess.AutoExposureBias = FMath::Clamp(Settings.ManualExposureCompensation, -15.0f, 15.0f);
		}
		if (UsesExplicitWhiteBalance(Settings))
		{
			FPostProcessSettings& PostProcess = CaptureComponent->PostProcessSettings;
			PostProcess.bOverride_TemperatureType = true;
			PostProcess.TemperatureType = ETemperatureMethod::TEMP_WhiteBalance;
			PostProcess.bOverride_WhiteTemp = true;
			PostProcess.WhiteTemp = FMath::Clamp(Settings.WhiteBalanceTemperature, 1500.0f, 15000.0f);
			PostProcess.bOverride_WhiteTint = true;
			PostProcess.WhiteTint = FMath::Clamp(Settings.WhiteBalanceTint, -1.0f, 1.0f);
		}

		FEngineShowFlags& ShowFlags = CaptureComponent->ShowFlags;
		ShowFlags.SetLighting(true);
		ShowFlags.SetDirectLighting(true);
		ShowFlags.SetGlobalIllumination(true);
		ShowFlags.SetSkyLighting(true);
		ShowFlags.SetAmbientCubemap(true);
		ShowFlags.SetAmbientOcclusion(true);
		ShowFlags.SetScreenSpaceAO(true);
		ShowFlags.SetDistanceFieldAO(true);
		ShowFlags.SetLumenGlobalIllumination(true);
		ShowFlags.SetLumenReflections(true);
		ShowFlags.SetLumenScreenTraces(true);
		ShowFlags.SetLumenDetailTraces(true);
		ShowFlags.SetLumenGlobalTraces(true);
		ShowFlags.SetLumenFarFieldTraces(true);
		ShowFlags.SetLumenSecondaryBounces(true);
		ShowFlags.SetReflectionEnvironment(true);
		ShowFlags.SetScreenSpaceReflections(true);
		ShowFlags.SetVolumetricLightmap(true);
		ShowFlags.SetIndirectLightingCache(true);
		ShowFlags.SetVolumetricFog(true);
		ShowFlags.SetPostProcessing(true);
		ShowFlags.SetTonemapper(true);
		ShowFlags.SetBloom(true);
		ShowFlags.SetLocalExposure(Settings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored);
		ShowFlags.SetColorGrading(true);
		ShowFlags.SetToneCurve(true);
		ShowFlags.SetVignette(true);
		ShowFlags.SetCameraImperfections(true);
		ShowFlags.SetPostProcessMaterial(true);
		// UE's manual exposure compensation is evaluated by the eye-adaptation pass,
		// but AEM_Manual has no temporal adaptation or luminance metering.
		ShowFlags.SetEyeAdaptation(bLockedExposure || Settings.bUseEyeAdaptation);
		ShowFlags.SetMotionBlur(false);
	}

	static bool ConfigureCaptureView(
		USceneCaptureComponent2D* CaptureComponent,
		const FUESplattingCaptureView& CaptureView,
		const FUESplattingDatasetExportSettings& Settings,
		UTextureRenderTarget2D* RenderTarget,
		FString& OutError)
	{
		if (!CaptureComponent || !RenderTarget)
		{
			OutError = TEXT("Invalid scene capture component or render target.");
			return false;
		}

		if (AActor* CaptureActor = CaptureComponent->GetOwner())
		{
			CaptureActor->SetActorTransform(CaptureView.Transform);
		}
		CaptureComponent->SetWorldTransform(CaptureView.Transform);
		CaptureComponent->TextureTarget = RenderTarget;
		CaptureComponent->CaptureSource = SCS_FinalColorLDR;
		CaptureComponent->bCaptureEveryFrame = false;
		CaptureComponent->bCaptureOnMovement = false;
		CaptureComponent->FOVAngle = FMath::Clamp(CaptureView.HorizontalFieldOfView, 1.0f, 179.0f);
		CaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
		CaptureComponent->PostProcessSettings = CaptureView.PostProcessSettings;
		CaptureComponent->PostProcessBlendWeight = CaptureView.PostProcessBlendWeight;
		ConfigureSceneCaptureLighting(CaptureComponent, Settings);
		const FPostProcessSettings& PostProcess = CaptureComponent->PostProcessSettings;
		const bool bAuthoredLumen = (PostProcess.bOverride_DynamicGlobalIlluminationMethod
			&& PostProcess.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen)
			|| (PostProcess.bOverride_ReflectionMethod && PostProcess.ReflectionMethod == EReflectionMethod::Lumen);
		CaptureComponent->bAlwaysPersistRenderingState |= bAuthoredLumen;
		return true;
	}

	static bool CaptureSceneFrame(USceneCaptureComponent2D* CaptureComponent, bool bCameraCut, FString& OutError)
	{
		if (!CaptureComponent)
		{
			OutError = TEXT("Scene capture component became invalid during export.");
			return false;
		}

		CaptureComponent->bCameraCutThisFrame = bCameraCut;
		CaptureComponent->CaptureScene();
		return true;
	}

	static bool SaveCapturedImage(
		UTextureRenderTarget2D* RenderTarget,
		const FUESplattingDatasetExportSettings& Settings,
		const FString& OutputImagePath,
		TArray<FColor>& OutPixels,
		FString& OutError)
	{
		if (!RenderTarget)
		{
			OutError = TEXT("Render target became invalid during export.");
			return false;
		}

		FlushRenderingCommands();

		TArray64<uint8> ImageData;
		if (!EncodeRenderTargetImage(RenderTarget, Settings, OutputImagePath, ImageData, OutError))
		{
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(ImageData, *OutputImagePath))
		{
			OutError = FString::Printf(TEXT("Failed to write image '%s'."), *OutputImagePath);
			return false;
		}

		FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
		ReadFlags.SetLinearToGamma(true);
		if (!RenderTargetResource || !RenderTargetResource->ReadPixels(OutPixels, ReadFlags))
		{
			OutError = FString::Printf(TEXT("Failed to read captured pixels for '%s'."), *OutputImagePath);
			return false;
		}

		return true;
	}

	static bool ReadCapturedPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FString& OutError)
	{
		if (!RenderTarget)
		{
			OutError = TEXT("Render target became invalid while reading calibration pixels.");
			return false;
		}

		FlushRenderingCommands();
		FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
		ReadFlags.SetLinearToGamma(true);
		if (!RenderTargetResource || !RenderTargetResource->ReadPixels(OutPixels, ReadFlags))
		{
			OutError = TEXT("Failed to read photometric calibration pixels.");
			return false;
		}
		return true;
	}

	static float ComputeFrameMeanLuminance(const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
		{
			return 0.0f;
		}

		constexpr int32 SamplesX = 32;
		constexpr int32 SamplesY = 18;
		double Sum = 0.0;
		for (int32 SampleY = 0; SampleY < SamplesY; ++SampleY)
		{
			const int32 PixelY = FMath::Clamp((SampleY * Height + Height / 2) / SamplesY, 0, Height - 1);
			for (int32 SampleX = 0; SampleX < SamplesX; ++SampleX)
			{
				const int32 PixelX = FMath::Clamp((SampleX * Width + Width / 2) / SamplesX, 0, Width - 1);
				const FColor& Color = Pixels[PixelY * Width + PixelX];
				Sum += 0.2126 * static_cast<double>(Color.R)
					+ 0.7152 * static_cast<double>(Color.G)
					+ 0.0722 * static_cast<double>(Color.B);
			}
		}
		return static_cast<float>(Sum / static_cast<double>(SamplesX * SamplesY));
	}

	static float GetSortedPercentile(const TArray<float>& SortedValues, float Percentile)
	{
		if (SortedValues.IsEmpty())
		{
			return 0.0f;
		}
		const float Position = FMath::Clamp(Percentile, 0.0f, 1.0f) * static_cast<float>(SortedValues.Num() - 1);
		const int32 Lower = FMath::FloorToInt(Position);
		const int32 Upper = FMath::Min(Lower + 1, SortedValues.Num() - 1);
		return FMath::Lerp(SortedValues[Lower], SortedValues[Upper], Position - static_cast<float>(Lower));
	}

	static FColor SampleImageColor(const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 PixelX, int32 PixelY)
	{
		if (Pixels.Num() != Width * Height)
		{
			return FColor::White;
		}

		const int32 ClampedX = FMath::Clamp(PixelX, 0, Width - 1);
		const int32 ClampedY = FMath::Clamp(PixelY, 0, Height - 1);
		return Pixels[ClampedY * Width + ClampedX];
	}

	static void GenerateTracePointsForCamera(
		UWorld* World,
		const FUESplattingCaptureView& CaptureView,
		const FUESplattingDatasetExportSettings& Settings,
		const TArray<FColor>& Pixels,
		FExportedImage& Image,
		TArray<FExportedPoint>& Points,
		int64& NextPointId)
	{
		if (!World || !Settings.bGenerateTracePointCloud || Settings.TracePixelStep <= 0)
		{
			return;
		}

		const FVector CameraLocation = CaptureView.Transform.GetLocation();
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UESplattingDatasetTrace), Settings.bTraceComplex);

		for (int32 Y = Settings.TracePixelStep / 2; Y < Settings.ImageHeight; Y += Settings.TracePixelStep)
		{
			for (int32 X = Settings.TracePixelStep / 2; X < Settings.ImageWidth; X += Settings.TracePixelStep)
			{
				const FVector Direction = BuildWorldRayDirection(CaptureView.Transform, CaptureView.HorizontalFieldOfView, X, Y, Settings.ImageWidth, Settings.ImageHeight);
				const FVector TraceEnd = CameraLocation + Direction * Settings.TraceMaxDistance;

				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(Hit, CameraLocation, TraceEnd, Settings.TraceChannel, QueryParams) || !Hit.bBlockingHit)
				{
					continue;
				}

				FExportedPoint& Point = Points.AddDefaulted_GetRef();
				Point.PointId = NextPointId++;
				Point.Position = UnrealPositionToDataset(Hit.ImpactPoint, Settings.WorldToColmapScale);
				Point.Color = SampleImageColor(Pixels, Settings.ImageWidth, Settings.ImageHeight, X, Y);
				Point.ImageId = Image.ImageId;
				Point.Point2DIndex = Image.Observations.Num();

				FPointObservation& Observation = Image.Observations.AddDefaulted_GetRef();
				Observation.X = static_cast<double>(X) + 0.5;
				Observation.Y = static_cast<double>(Y) + 0.5;
				Observation.PointId = Point.PointId;
			}
		}
	}

	static bool WriteColmapFiles(
		const FString& SparseDirectory,
		const TArray<FExportedImage>& Images,
		const TArray<FExportedPoint>& Points,
		const FUESplattingDatasetExportSettings& Settings,
		FString& OutError)
	{
		FString CamerasText;
		CamerasText += TEXT("# Camera list with one line of data per camera:\n");
		CamerasText += TEXT("# CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]\n");
		CamerasText += FString::Printf(TEXT("# Number of cameras: %d\n"), Images.Num());

		for (const FExportedImage& Image : Images)
		{
			// Per-image cameras keep the file simple and leave room for mixed-FOV exports later.
			const double Fx = Image.FocalLength;
			const double Fy = Image.FocalLength;
			const double Cx = static_cast<double>(Settings.ImageWidth) * 0.5;
			const double Cy = static_cast<double>(Settings.ImageHeight) * 0.5;
			CamerasText += FString::Printf(
				TEXT("%d PINHOLE %d %d %s %s %s %s\n"),
				Image.CameraId,
				Settings.ImageWidth,
				Settings.ImageHeight,
				*FloatString(Fx),
				*FloatString(Fy),
				*FloatString(Cx),
				*FloatString(Cy));
		}

		FString ImagesText;
		ImagesText += TEXT("# Image list with two lines of data per image:\n");
		ImagesText += TEXT("# IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME\n");
		ImagesText += TEXT("# POINTS2D[] as (X, Y, POINT3D_ID)\n");
		ImagesText += FString::Printf(TEXT("# Number of images: %d\n"), Images.Num());
		for (const FExportedImage& Image : Images)
		{
			const FColmapQuaternion& Q = Image.Pose.Rotation;
			const FVector3d& T = Image.Pose.Translation;
			ImagesText += FString::Printf(
				TEXT("%d %s %s %s %s %s %s %s %d %s\n"),
				Image.ImageId,
				*FloatString(Q.W),
				*FloatString(Q.X),
				*FloatString(Q.Y),
				*FloatString(Q.Z),
				*FloatString(T.X),
				*FloatString(T.Y),
				*FloatString(T.Z),
				Image.CameraId,
				*Image.Name);

			FString ObservationLine;
			for (const FPointObservation& Observation : Image.Observations)
			{
				ObservationLine += FString::Printf(TEXT("%s %s %lld "), *FloatString(Observation.X), *FloatString(Observation.Y), Observation.PointId);
			}
			ObservationLine.TrimEndInline();
			ImagesText += ObservationLine + TEXT("\n");
		}

		FString PointsText;
		PointsText += TEXT("# 3D point list with one line of data per point:\n");
		PointsText += TEXT("# POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[] as (IMAGE_ID, POINT2D_IDX)\n");
		PointsText += FString::Printf(TEXT("# Number of points: %d\n"), Points.Num());
		for (const FExportedPoint& Point : Points)
		{
			PointsText += FString::Printf(
				TEXT("%lld %s %d %d %d 0 %d %d\n"),
				Point.PointId,
				*VectorString(Point.Position),
				Point.Color.R,
				Point.Color.G,
				Point.Color.B,
				Point.ImageId,
				Point.Point2DIndex);
		}

		const FString CamerasPath = FPaths::Combine(SparseDirectory, TEXT("cameras.txt"));
		const FString ImagesPath = FPaths::Combine(SparseDirectory, TEXT("images.txt"));
		const FString PointsPath = FPaths::Combine(SparseDirectory, TEXT("points3D.txt"));

		if (!FFileHelper::SaveStringToFile(CamerasText, *CamerasPath))
		{
			OutError = FString::Printf(TEXT("Failed to write '%s'."), *CamerasPath);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(ImagesText, *ImagesPath))
		{
			OutError = FString::Printf(TEXT("Failed to write '%s'."), *ImagesPath);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(PointsText, *PointsPath))
		{
			OutError = FString::Printf(TEXT("Failed to write '%s'."), *PointsPath);
			return false;
		}

		return true;
	}

	static bool WriteSparsePointCloudPly(const FString& PlyPath, const TArray<FExportedPoint>& Points, FString& OutError)
	{
		FString PlyText;
		PlyText += TEXT("ply\n");
		PlyText += TEXT("format ascii 1.0\n");
		PlyText += TEXT("comment Generated by UESplatting Unreal known-pose scene capture exporter\n");
		PlyText += FString::Printf(TEXT("element vertex %d\n"), Points.Num());
		PlyText += TEXT("property float x\n");
		PlyText += TEXT("property float y\n");
		PlyText += TEXT("property float z\n");
		PlyText += TEXT("property uchar red\n");
		PlyText += TEXT("property uchar green\n");
		PlyText += TEXT("property uchar blue\n");
		PlyText += TEXT("end_header\n");

		for (const FExportedPoint& Point : Points)
		{
			PlyText += FString::Printf(
				TEXT("%s %d %d %d\n"),
				*VectorString(Point.Position),
				Point.Color.R,
				Point.Color.G,
				Point.Color.B);
		}

		if (!FFileHelper::SaveStringToFile(PlyText, *PlyPath))
		{
			OutError = FString::Printf(TEXT("Failed to write '%s'."), *PlyPath);
			return false;
		}

		return true;
	}

	static bool WriteOptionalSparsePointCloudPly(const FString& PlyPath, const TArray<FExportedPoint>& Points, FString& OutError)
	{
		return Points.IsEmpty() || WriteSparsePointCloudPly(PlyPath, Points, OutError);
	}

	static TArray<TSharedPtr<FJsonValue>> BuildNerfstudioTransformMatrix(const FTransform& CameraToWorld, double UnitScale)
	{
		const FVector3d Center = UnrealPositionToDataset(CameraToWorld.GetLocation(), UnitScale);
		const FVector3d Right = UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::Y));
		const FVector3d Up = UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::Z));
		const FVector3d Back = -UnrealVectorToDataset(CameraToWorld.GetUnitAxis(EAxis::X));

		return MatrixArray({
			NumberArray({ Right.X, Up.X, Back.X, Center.X }),
			NumberArray({ Right.Y, Up.Y, Back.Y, Center.Y }),
			NumberArray({ Right.Z, Up.Z, Back.Z, Center.Z }),
			NumberArray({ 0.0, 0.0, 0.0, 1.0 }),
		});
	}

	static TSharedRef<FJsonObject> BuildTransformsJsonObject(
		const TArray<FExportedImage>& Images,
		const FUESplattingDatasetExportSettings& Settings,
		bool bHasPointCloud)
	{
		const double Cx = static_cast<double>(Settings.ImageWidth) * 0.5;
		const double Cy = static_cast<double>(Settings.ImageHeight) * 0.5;
		const FCameraIntrinsicsSummary Intrinsics = SummarizeCameraIntrinsics(Images);

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("camera_model"), TEXT("PINHOLE"));
		if (Intrinsics.bUniform)
		{
			Root->SetNumberField(TEXT("fl_x"), Images[0].FocalLength);
			Root->SetNumberField(TEXT("fl_y"), Images[0].FocalLength);
			Root->SetNumberField(TEXT("camera_angle_x"), FMath::DegreesToRadians(ResolveHorizontalFovDegrees(Images[0].HorizontalFieldOfView)));
		}
		Root->SetNumberField(TEXT("cx"), Cx);
		Root->SetNumberField(TEXT("cy"), Cy);
		Root->SetNumberField(TEXT("w"), Settings.ImageWidth);
		Root->SetNumberField(TEXT("h"), Settings.ImageHeight);
		if (bHasPointCloud)
		{
			Root->SetStringField(TEXT("ply_file_path"), TEXT("sparse_pc.ply"));
		}

		TArray<TSharedPtr<FJsonValue>> Frames;
		for (const FExportedImage& Image : Images)
		{
			TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetStringField(TEXT("file_path"), JsonPathString(Image.Name));
			Frame->SetNumberField(TEXT("fl_x"), Image.FocalLength);
			Frame->SetNumberField(TEXT("fl_y"), Image.FocalLength);
			Frame->SetNumberField(TEXT("cx"), Cx);
			Frame->SetNumberField(TEXT("cy"), Cy);
			Frame->SetNumberField(TEXT("w"), Settings.ImageWidth);
			Frame->SetNumberField(TEXT("h"), Settings.ImageHeight);
			Frame->SetNumberField(TEXT("colmap_im_id"), Image.ImageId);
			if (Image.StationIndex >= 0)
			{
				Frame->SetNumberField(TEXT("station_index"), Image.StationIndex);
			}
			if (!Image.CaptureGroupId.IsEmpty())
			{
				Frame->SetStringField(TEXT("capture_group_id"), Image.CaptureGroupId);
			}
			if (!Image.CaptureGroupKind.IsEmpty())
			{
				Frame->SetStringField(TEXT("capture_group_kind"), Image.CaptureGroupKind);
			}
			Frame->SetArrayField(TEXT("transform_matrix"), BuildNerfstudioTransformMatrix(Image.CameraToWorld, Settings.WorldToColmapScale));
			Frames.Add(MakeShared<FJsonValueObject>(Frame));
		}
		Root->SetArrayField(TEXT("frames"), Frames);
		return Root;
	}

	static bool WriteTransformsJson(
		const FString& TransformsPath,
		const TArray<FExportedImage>& Images,
		const FUESplattingDatasetExportSettings& Settings,
		bool bHasPointCloud,
		FString& OutError)
	{
		return SaveJsonObject(BuildTransformsJsonObject(Images, Settings, bHasPointCloud), TransformsPath, OutError);
	}

	static TSharedPtr<FJsonObject> ObjectField(std::initializer_list<TPair<FString, FString>> Fields)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Field : Fields)
		{
			Object->SetStringField(Field.Key, Field.Value);
		}
		return Object;
	}

	static bool WriteCaptureManifest(
		const FString& ManifestPath,
		const FString& CaptureId,
		UWorld* World,
		const TArray<FExportedImage>& Images,
		const TArray<FExportedPoint>& Points,
		const FUESplattingDatasetExportSettings& Settings,
		FString& OutError)
	{
		const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		const FString ProjectName = FApp::GetProjectName();
		const FString SceneId = World && World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT("unknown");
		const FString SceneName = World ? World->GetMapName() : TEXT("unknown");
		const FCameraIntrinsicsSummary Intrinsics = SummarizeCameraIntrinsics(Images);
		const TSharedPtr<IPlugin> UESplattingCapturePlugin = IPluginManager::Get().FindPlugin(TEXT("UESplattingCapture"));
		const FString PluginVersion = UESplattingCapturePlugin.IsValid() ? UESplattingCapturePlugin->GetDescriptor().VersionName : TEXT("unknown");
		const bool bLockedExposure = UsesLockedExposure(Settings);
		const bool bExplicitWhiteBalance = UsesExplicitWhiteBalance(Settings);
		const bool bCalibratedLockedPhotometrics = UsesCalibratedLockedPhotometrics(Settings);
		const bool bEyeAdaptationShowFlag = bLockedExposure || Settings.bUseEyeAdaptation;
		const bool bHasPointCloud = !Points.IsEmpty();

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("kajiba.scene_capture.v1"));
		Root->SetStringField(TEXT("capture_id"), CaptureId);
		Root->SetStringField(TEXT("created_at"), FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ")));
		Root->SetObjectField(TEXT("producer"), ObjectField({
			TPair<FString, FString>(TEXT("name"), TEXT("UESplatting Capture")),
			TPair<FString, FString>(TEXT("version"), PluginVersion),
			TPair<FString, FString>(TEXT("notes"), TEXT("Known-pose scene capture dataset exporter")),
		}));
		Root->SetObjectField(TEXT("source_engine"), ObjectField({
			TPair<FString, FString>(TEXT("name"), TEXT("Unreal Engine 5")),
			TPair<FString, FString>(TEXT("version"), FEngineVersion::Current().ToString()),
		}));
		Root->SetObjectField(TEXT("source_project"), ObjectField({
			TPair<FString, FString>(TEXT("name"), ProjectName),
			TPair<FString, FString>(TEXT("path"), ProjectPath),
			TPair<FString, FString>(TEXT("revision"), TEXT("unknown")),
		}));
		Root->SetObjectField(TEXT("scene"), ObjectField({
			TPair<FString, FString>(TEXT("id"), SceneId),
			TPair<FString, FString>(TEXT("name"), SceneName),
			TPair<FString, FString>(TEXT("notes"), TEXT("Static known-pose capture pass from Unreal")),
		}));

		TSharedPtr<FJsonObject> Render = MakeShared<FJsonObject>();
		Render->SetNumberField(TEXT("width"), Settings.ImageWidth);
		Render->SetNumberField(TEXT("height"), Settings.ImageHeight);
		Render->SetStringField(TEXT("format"), GetImageFormatName(Settings));
		Render->SetStringField(TEXT("image_extension"), GetImageFileExtension(Settings));
		if (Settings.ImageFormat == EUESplattingSceneCaptureImageFormat::JPEG)
		{
			if (Settings.Renderer == EUESplattingSceneCaptureRenderer::SceneCapture2DLegacy)
			{
				Render->SetNumberField(TEXT("jpeg_quality"), GetImageCompressionQuality(Settings));
			}
			else
			{
				Render->SetStringField(TEXT("jpeg_quality"), TEXT("movie-render-queue-engine-default"));
			}
			Render->SetStringField(TEXT("jpeg_chroma_subsampling"), TEXT("engine-default; UE 5.8 built-in JPEG uses 4:2:0 for color images"));
		}
		Render->SetStringField(
			TEXT("anti_aliasing"),
			Settings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue
				? TEXT("Movie Render Queue deferred renderer defaults")
				: TEXT("scene-capture default; Temporal AA is disabled by the UE SceneCapture2D constructor"));
		Render->SetBoolField(TEXT("motion_blur"), false);
		Render->SetStringField(
			TEXT("photometric_mode"),
			Settings.bViewportExposureMatched
				? TEXT("scene-authored-viewport-matched-locked")
				: (bCalibratedLockedPhotometrics
				? TEXT("calibrated-locked-training")
				: (bLockedExposure ? TEXT("explicit-locked-training") : TEXT("scene-authored-automatic"))));
		Render->SetStringField(
			TEXT("exposure"),
			Settings.bViewportExposureMatched
				? TEXT("one global exposure measured from the active authored editor viewport")
				: (bLockedExposure ? TEXT("one global manual exposure compensation") : (Settings.bUseEyeAdaptation ? TEXT("scene/project/camera post-process stack with temporal eye adaptation") : TEXT("scene-authored exposure with temporal eye adaptation disabled"))));
		if (bLockedExposure)
		{
			Render->SetNumberField(TEXT("manual_exposure_compensation"), Settings.ManualExposureCompensation);
		}
		if (Settings.bViewportExposureMatched)
		{
			Render->SetNumberField(TEXT("viewport_exposure_scale"), Settings.ViewportExposureScale);
			Render->SetStringField(TEXT("viewport_exposure_source"), Settings.ViewportExposureSource);
		}
		if (bExplicitWhiteBalance)
		{
			Render->SetNumberField(TEXT("white_balance_temperature_kelvin"), Settings.WhiteBalanceTemperature);
			Render->SetNumberField(TEXT("white_balance_tint"), Settings.WhiteBalanceTint);
		}
		if (Settings.bPhotometricCalibrationPerformed)
		{
			TSharedPtr<FJsonObject> Calibration = MakeShared<FJsonObject>();
			Calibration->SetStringField(TEXT("method"), TEXT("representative-view-global-exposure-preflight"));
			Calibration->SetNumberField(TEXT("target_median_luminance_0_255"), Settings.CalibrationTargetMedianLuminance);
			Calibration->SetNumberField(TEXT("dark_tolerance_0_255"), Settings.CalibrationDarkTolerance);
			Calibration->SetNumberField(TEXT("bright_tolerance_0_255"), Settings.CalibrationBrightTolerance);
			Calibration->SetNumberField(TEXT("initial_exposure_compensation"), Settings.CalibrationInitialExposureCompensation);
			Calibration->SetNumberField(TEXT("effective_exposure_compensation"), Settings.ManualExposureCompensation);
			Calibration->SetNumberField(TEXT("sample_view_count"), Settings.CalibrationViewsEvaluated);
			Calibration->SetNumberField(TEXT("frame_mean_luminance_p10"), Settings.CalibrationLuminanceP10);
			Calibration->SetNumberField(TEXT("frame_mean_luminance_median"), Settings.CalibrationLuminanceMedian);
			Calibration->SetNumberField(TEXT("frame_mean_luminance_p90"), Settings.CalibrationLuminanceP90);
			Render->SetObjectField(TEXT("exposure_calibration"), Calibration);
		}
		Render->SetStringField(TEXT("lighting"), TEXT("fixed scene lighting recommended"));
		Render->SetStringField(
			TEXT("renderer"),
			Settings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue
				? TEXT("MovieRenderQueue-SequentialKnownPoses")
				: TEXT("SceneCapture2D"));
		Render->SetStringField(TEXT("lighting_method"), StaticEnum<EUESplattingSceneCaptureLightingMethod>()->GetNameStringByValue(static_cast<int64>(Settings.LightingMethod)));
		Render->SetBoolField(TEXT("eye_adaptation_show_flag"), bEyeAdaptationShowFlag);
		Render->SetBoolField(TEXT("temporal_eye_adaptation"), UsesTemporalEyeAdaptation(Settings));
		Render->SetBoolField(TEXT("ray_tracing_if_enabled"), Settings.bUseRayTracingIfEnabled);
		Render->SetBoolField(TEXT("persist_capture_rendering_state_requested"), Settings.bPersistCaptureRenderingState);
		Render->SetBoolField(TEXT("persist_capture_rendering_state_effective"), UsesPersistentCaptureState(Settings));
		Render->SetNumberField(TEXT("garbage_collect_every_images"), Settings.GarbageCollectEveryImages);
		Render->SetNumberField(TEXT("capture_warmup_frames"), Settings.CaptureWarmupFrames);
		Render->SetNumberField(TEXT("mrq_startup_warmup_frames"), 0);
		Render->SetNumberField(TEXT("mrq_temporal_samples"), 1);
		Render->SetBoolField(TEXT("mrq_camera_cut_per_output_frame"), true);
		Render->SetStringField(TEXT("white_balance"), bExplicitWhiteBalance ? TEXT("fixed explicit temperature and tint") : TEXT("authored scene/project/post-process stack"));
		Render->SetStringField(
			TEXT("post_process"),
			Settings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue
				? TEXT("Normal deferred camera rendering through Movie Render Queue; project defaults, post-process volumes, and per-camera post process are authored inputs.")
				: TEXT("SceneCapture2D starts from project defaults and post-process volumes at each view location; per-view camera post-process settings are blended when supplied."));
		Root->SetObjectField(TEXT("render"), Render);

		TSharedPtr<FJsonObject> Camera = MakeShared<FJsonObject>();
		Camera->SetStringField(TEXT("model"), TEXT("PINHOLE"));
		Camera->SetStringField(TEXT("intrinsics_mode"), Intrinsics.bUniform ? TEXT("fixed") : TEXT("per-frame"));
		Camera->SetNumberField(TEXT("width"), Settings.ImageWidth);
		Camera->SetNumberField(TEXT("height"), Settings.ImageHeight);
		Camera->SetNumberField(TEXT("cx"), static_cast<double>(Settings.ImageWidth) * 0.5);
		Camera->SetNumberField(TEXT("cy"), static_cast<double>(Settings.ImageHeight) * 0.5);
		Camera->SetNumberField(TEXT("unique_intrinsics_count"), Intrinsics.UniqueIntrinsicsCount);
		if (Intrinsics.bUniform)
		{
			Camera->SetNumberField(TEXT("fl_x"), Images[0].FocalLength);
			Camera->SetNumberField(TEXT("fl_y"), Images[0].FocalLength);
			Camera->SetNumberField(TEXT("fov_degrees"), Intrinsics.MinHorizontalFovDegrees);
		}
		else if (Intrinsics.bHasImages)
		{
			Camera->SetNumberField(TEXT("focal_length_pixels_min"), Intrinsics.MinFocalLength);
			Camera->SetNumberField(TEXT("focal_length_pixels_max"), Intrinsics.MaxFocalLength);
			Camera->SetNumberField(TEXT("fov_degrees_min"), Intrinsics.MinHorizontalFovDegrees);
			Camera->SetNumberField(TEXT("fov_degrees_max"), Intrinsics.MaxHorizontalFovDegrees);
		}
		Root->SetObjectField(TEXT("camera"), Camera);

		TSharedPtr<FJsonObject> CameraPath = MakeShared<FJsonObject>();
		CameraPath->SetStringField(TEXT("strategy"), TEXT("known-pose-unreal-capture-views"));
		CameraPath->SetStringField(TEXT("intrinsics_mode"), Intrinsics.bUniform ? TEXT("fixed") : TEXT("per-frame"));
		CameraPath->SetNumberField(TEXT("frame_count"), Images.Num());
		if (Intrinsics.bUniform)
		{
			CameraPath->SetNumberField(TEXT("fov_degrees"), Intrinsics.MinHorizontalFovDegrees);
		}
		else if (Intrinsics.bHasImages)
		{
			CameraPath->SetNumberField(TEXT("fov_degrees_min"), Intrinsics.MinHorizontalFovDegrees);
			CameraPath->SetNumberField(TEXT("fov_degrees_max"), Intrinsics.MaxHorizontalFovDegrees);
		}
		CameraPath->SetNumberField(TEXT("near_clip_cm"), GNearClippingPlane);
		CameraPath->SetField(TEXT("far_clip_cm"), MakeShared<FJsonValueNull>());
		CameraPath->SetStringField(TEXT("coverage_notes"), TEXT("Overlapping perspective views generated or supplied by the Unreal producer"));
		CameraPath->SetStringField(
			TEXT("parameters"),
			Settings.CapturePatternNotes.TrimStartAndEnd().IsEmpty()
				? TEXT("Explicit capture views; exact per-frame poses are recorded in transforms.json and colmap/sparse/0/images.txt.")
				: Settings.CapturePatternNotes);
		Root->SetObjectField(TEXT("camera_path"), CameraPath);

		struct FCaptureGroupSummary
		{
			FString Kind;
			int32 FrameCount = 0;
			TSet<int32> StationIndices;
		};
		TMap<FString, FCaptureGroupSummary> CaptureGroupSummaries;
		for (const FExportedImage& Image : Images)
		{
			if (Image.CaptureGroupId.IsEmpty())
			{
				continue;
			}
			FCaptureGroupSummary& Summary = CaptureGroupSummaries.FindOrAdd(Image.CaptureGroupId);
			Summary.Kind = Image.CaptureGroupKind;
			++Summary.FrameCount;
			if (Image.StationIndex >= 0)
			{
				Summary.StationIndices.Add(Image.StationIndex);
			}
		}
		TArray<FString> CaptureGroupIds;
		CaptureGroupSummaries.GetKeys(CaptureGroupIds);
		CaptureGroupIds.Sort();
		TArray<TSharedPtr<FJsonValue>> CaptureGroups;
		for (const FString& GroupId : CaptureGroupIds)
		{
			const FCaptureGroupSummary& Summary = CaptureGroupSummaries.FindChecked(GroupId);
			TSharedPtr<FJsonObject> Group = MakeShared<FJsonObject>();
			Group->SetStringField(TEXT("id"), GroupId);
			Group->SetStringField(TEXT("kind"), Summary.Kind);
			Group->SetNumberField(TEXT("station_count"), Summary.StationIndices.Num());
			Group->SetNumberField(TEXT("frame_count"), Summary.FrameCount);
			CaptureGroups.Add(MakeShared<FJsonValueObject>(Group));
		}
		Root->SetArrayField(TEXT("capture_groups"), CaptureGroups);

		TSharedPtr<FJsonObject> CaptureScope = MakeShared<FJsonObject>();
		CaptureScope->SetStringField(TEXT("profile"), Settings.CaptureProfile.TrimStartAndEnd().IsEmpty() ? TEXT("unspecified") : Settings.CaptureProfile);
		CaptureScope->SetStringField(TEXT("zone_id"), Settings.CaptureZoneId.TrimStartAndEnd());
		CaptureScope->SetStringField(TEXT("block_id"), Settings.CaptureBlockId.TrimStartAndEnd());
		CaptureScope->SetNumberField(TEXT("overlap_margin_m"), FMath::Max(Settings.CaptureOverlapMarginMeters, 0.0));
		CaptureScope->SetNumberField(TEXT("requested_probe_count"), Settings.RequestedProbeCount);
		CaptureScope->SetNumberField(TEXT("accepted_probe_count"), Settings.AcceptedProbeCount);
		CaptureScope->SetNumberField(TEXT("candidate_probe_count"), Settings.CandidateProbeCount);
		CaptureScope->SetNumberField(TEXT("clearance_rejected_probe_count"), Settings.ClearanceRejectedProbeCount);
		CaptureScope->SetNumberField(TEXT("surface_patch_count"), Settings.SurfacePatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_surface_patch_count"), Settings.RepeatedSurfacePatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_surface_coverage_percent"), Settings.RepeatedSurfaceCoveragePercent);
		CaptureScope->SetNumberField(TEXT("floor_patch_count"), Settings.FloorPatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_floor_patch_count"), Settings.RepeatedFloorPatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_floor_coverage_percent"), Settings.RepeatedFloorCoveragePercent);
		CaptureScope->SetNumberField(TEXT("close_detail_patch_count"), Settings.CloseDetailPatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_close_detail_patch_count"), Settings.RepeatedCloseDetailPatchCount);
		CaptureScope->SetNumberField(TEXT("repeated_close_detail_coverage_percent"), Settings.RepeatedCloseDetailCoveragePercent);
		CaptureScope->SetNumberField(TEXT("minimum_coverage_baseline_m"), Settings.MinimumCoverageBaselineMeters);
		if (Settings.bHasCaptureWorldBounds)
		{
			TSharedPtr<FJsonObject> WorldBounds = MakeShared<FJsonObject>();
			WorldBounds->SetStringField(TEXT("coordinate_system"), TEXT("unreal_world_meters"));
			WorldBounds->SetArrayField(TEXT("min"), NumberArray({
				Settings.CaptureWorldBoundsMinMeters.X,
				Settings.CaptureWorldBoundsMinMeters.Y,
				Settings.CaptureWorldBoundsMinMeters.Z,
			}));
			WorldBounds->SetArrayField(TEXT("max"), NumberArray({
				Settings.CaptureWorldBoundsMaxMeters.X,
				Settings.CaptureWorldBoundsMaxMeters.Y,
				Settings.CaptureWorldBoundsMaxMeters.Z,
			}));
			CaptureScope->SetObjectField(TEXT("world_bounds"), WorldBounds);
		}
		else
		{
			CaptureScope->SetField(TEXT("world_bounds"), MakeShared<FJsonValueNull>());
		}
		Root->SetObjectField(TEXT("capture_scope"), CaptureScope);

		TSharedPtr<FJsonObject> Coordinates = MakeShared<FJsonObject>();
		Coordinates->SetStringField(TEXT("source"), TEXT("Unreal left-handed, X forward, Y right, Z up, centimeters"));
		Coordinates->SetStringField(TEXT("nerfstudio"), TEXT("OpenGL camera convention, +X right, +Y up, +Z back, -Z look direction, world +Z up"));
		Coordinates->SetNumberField(TEXT("unit_scale_to_meters"), Settings.WorldToColmapScale);
		Coordinates->SetStringField(TEXT("nerfstudio_transform"), TEXT("World positions/vectors are converted from Unreal to dataset space as (X, -Y, Z) * unit_scale. Nerfstudio camera-to-world columns are [UE right, UE up, -UE forward] after that world conversion."));
		Root->SetObjectField(TEXT("coordinate_system"), Coordinates);

		TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
		Outputs->SetStringField(TEXT("images"), TEXT("images"));
		Outputs->SetStringField(TEXT("transforms"), TEXT("transforms.json"));
		Outputs->SetStringField(TEXT("colmap_sparse"), TEXT("colmap/sparse/0"));
		if (bHasPointCloud)
		{
			Outputs->SetStringField(TEXT("point_cloud"), TEXT("sparse_pc.ply"));
		}
		else
		{
			Outputs->SetField(TEXT("point_cloud"), MakeShared<FJsonValueNull>());
		}
		Outputs->SetField(TEXT("depth"), MakeShared<FJsonValueNull>());
		Outputs->SetField(TEXT("masks"), MakeShared<FJsonValueNull>());
		Outputs->SetField(TEXT("raw"), MakeShared<FJsonValueNull>());
		Root->SetObjectField(TEXT("outputs"), Outputs);

		TSharedPtr<FJsonObject> PointCloud = MakeShared<FJsonObject>();
		PointCloud->SetStringField(
			TEXT("status"),
			!Settings.bGenerateTracePointCloud ? TEXT("disabled") : (bHasPointCloud ? TEXT("generated") : TEXT("no_collision_hits")));
		PointCloud->SetStringField(
			TEXT("method"),
			Settings.bGenerateTracePointCloud ? TEXT("unreal-physics-collision-line-trace-grid-experimental") : TEXT("disabled"));
		PointCloud->SetStringField(TEXT("purpose"), TEXT("optional_trainer_initialization"));
		PointCloud->SetNumberField(TEXT("point_count"), Points.Num());
		PointCloud->SetBoolField(TEXT("authoritative_scene_geometry"), false);
		if (Settings.bGenerateTracePointCloud)
		{
			PointCloud->SetStringField(TEXT("position_source"), TEXT("Unreal physics collision Hit.ImpactPoint"));
			PointCloud->SetStringField(TEXT("color_source"), TEXT("final composite RGB image sample"));
			PointCloud->SetNumberField(TEXT("trace_pixel_step"), Settings.TracePixelStep);
			PointCloud->SetNumberField(TEXT("trace_max_distance_cm"), Settings.TraceMaxDistance);
			PointCloud->SetNumberField(TEXT("trace_channel"), static_cast<int32>(Settings.TraceChannel.GetValue()));
			PointCloud->SetBoolField(TEXT("trace_complex"), Settings.bTraceComplex);
		}
		Root->SetObjectField(TEXT("point_cloud"), PointCloud);

		TSharedPtr<FJsonObject> DataPolicy = MakeShared<FJsonObject>();
		DataPolicy->SetStringField(TEXT("depth"), TEXT("not exported"));
		DataPolicy->SetStringField(TEXT("masks"), TEXT("not exported"));
		DataPolicy->SetStringField(TEXT("sky"), TEXT("not automatically masked"));
		DataPolicy->SetStringField(TEXT("point_cloud"), TEXT("optional trainer initialization; never used to gate RGB capture"));
		Root->SetObjectField(TEXT("data_policy"), DataPolicy);

		TSharedPtr<FJsonObject> Quality = MakeShared<FJsonObject>();
		Quality->SetField(TEXT("static_scene"), MakeShared<FJsonValueNull>());
		Quality->SetBoolField(TEXT("scene_freeze_requested"), Settings.bFreezeSceneDuringCapture);
		Quality->SetBoolField(TEXT("fixed_exposure"), bLockedExposure);
		Quality->SetBoolField(TEXT("fixed_white_balance"), bExplicitWhiteBalance);
		Quality->SetBoolField(TEXT("no_motion_blur"), true);
		Quality->SetBoolField(TEXT("known_camera_poses"), true);
		Quality->SetBoolField(TEXT("initialization_point_cloud"), bHasPointCloud);
		Quality->SetBoolField(TEXT("authoritative_scene_geometry"), false);
		Root->SetObjectField(TEXT("quality_gates"), Quality);

		TArray<TSharedPtr<FJsonValue>> KnownLimitations;
		KnownLimitations.Add(MakeShared<FJsonValueString>(TEXT("Depth and masks are not exported yet.")));
		KnownLimitations.Add(MakeShared<FJsonValueString>(TEXT("Sky, volumetrics, transient actors, and temporal post effects are not automatically excluded.")));
		KnownLimitations.Add(MakeShared<FJsonValueString>(TEXT("Scene state and lighting stability must still be verified in the source level.")));
		if (bHasPointCloud)
		{
			KnownLimitations.Add(MakeShared<FJsonValueString>(TEXT("The optional seed cloud uses physics collision positions colored from final RGB; it can disagree with rendered surfaces and omits visible content without collision hits.")));
		}
		if (UsesTemporalEyeAdaptation(Settings))
		{
			KnownLimitations.Add(MakeShared<FJsonValueString>(TEXT("Scene-authored photometrics may vary between views; verify exposure and white-balance consistency before training.")));
		}
		Root->SetArrayField(TEXT("known_limitations"), KnownLimitations);
		Root->SetStringField(TEXT("agent_notes"), TEXT("Generated by UESplatting editor exporter. Verify exposure/lighting stability before training production splats."));

		return SaveJsonObject(Root, ManifestPath, OutError);
	}

	enum class EDatasetExportPhase : uint8
	{
		CalibrationConfigure,
		CalibrationWarmup,
		CalibrationCapture,
		CalibrationPreviewConfigure,
		CalibrationPreviewWarmup,
		CalibrationPreviewCapture,
		ConfigureView,
		Warmup,
		Capture,
		Finalize
	};

	class FDatasetExportJob : public TSharedFromThis<FDatasetExportJob>
	{
	public:
		bool Initialize(
			UObject* WorldContextObject,
			const TArray<FUESplattingCaptureView>& InCaptureViews,
			const FUESplattingDatasetExportSettings& InSettings,
			FUESplattingDatasetExportCompleted InCompletion,
			FUESplattingDatasetExportResult& StartResult)
		{
			Result = FUESplattingDatasetExportResult();
			CaptureViews = InCaptureViews;
			Settings = InSettings;
			Completion = MoveTemp(InCompletion);

			if (CaptureViews.IsEmpty())
			{
				StartResult.Message = TEXT("No capture views were supplied.");
				return false;
			}

			UWorld* ResolvedWorld = nullptr;
			if (WorldContextObject)
			{
				if (AActor* Actor = Cast<AActor>(WorldContextObject))
				{
					ResolvedWorld = Actor->GetWorld();
				}
				else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
				{
					ResolvedWorld = Component->GetWorld();
				}
				else if (GEngine)
				{
					ResolvedWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
				}
			}

			if (!ResolvedWorld)
			{
				StartResult.Message = TEXT("Could not resolve an editor world for dataset export.");
				return false;
			}
			if (Settings.ImageWidth <= 0 || Settings.ImageHeight <= 0)
			{
				StartResult.Message = TEXT("Image dimensions must be positive.");
				return false;
			}
			if (Settings.WorldToColmapScale <= 0.0)
			{
				StartResult.Message = TEXT("World to Dataset Scale must be positive.");
				return false;
			}

			ExportWorld = ResolvedWorld;
			OutputDirectory = ResolveOutputDirectory(Settings);
			CaptureId = ResolveCaptureId(Settings, OutputDirectory);
			ImagesDirectory = FPaths::Combine(OutputDirectory, TEXT("images"));
			SparseDirectory = FPaths::Combine(OutputDirectory, TEXT("colmap"), TEXT("sparse"), TEXT("0"));
			TransformsPath = FPaths::Combine(OutputDirectory, TEXT("transforms.json"));
			PointCloudPath = FPaths::Combine(OutputDirectory, TEXT("sparse_pc.ply"));
			ManifestPath = FPaths::Combine(OutputDirectory, TEXT("capture-manifest.json"));
			IncompleteMarkerPath = FPaths::Combine(OutputDirectory, TEXT(".uesplatting-capture-incomplete"));
			Result.OutputDirectory = OutputDirectory;

			if (IFileManager::Get().DirectoryExists(*OutputDirectory) && !IsDirectoryEmpty(OutputDirectory))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(
					TEXT("Output folder is not empty. Choose an empty folder so stale files cannot contaminate the dataset: %s"),
					*OutputDirectory);
				return false;
			}

			if (!EnsureDirectory(ImagesDirectory) || !EnsureDirectory(SparseDirectory))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(TEXT("Failed to create dataset directories under '%s'."), *OutputDirectory);
				return false;
			}

			if (!FFileHelper::SaveStringToFile(TEXT("UESplatting scene capture is still in progress.\n"), *IncompleteMarkerPath))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(TEXT("Failed to create capture marker under '%s'."), *OutputDirectory);
				return false;
			}

			RenderTarget.Reset(CreateRenderTarget(GetTransientPackage(), Settings.ImageWidth, Settings.ImageHeight));
			if (!RenderTarget.IsValid())
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = TEXT("Failed to create transient render target.");
				return false;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.Name = MakeUniqueObjectName(ResolvedWorld, ASceneCapture2D::StaticClass(), TEXT("UESplattingSceneCapture"));
			ASceneCapture2D* NewCaptureActor = ResolvedWorld->SpawnActor<ASceneCapture2D>(
				ASceneCapture2D::StaticClass(), CaptureViews[0].Transform, SpawnParameters);
			USceneCaptureComponent2D* NewCaptureComponent = NewCaptureActor ? NewCaptureActor->GetCaptureComponent2D() : nullptr;
			if (!NewCaptureActor || !NewCaptureComponent)
			{
				if (NewCaptureActor)
				{
					NewCaptureActor->Destroy();
				}
				RenderTarget->ReleaseResource();
				RenderTarget.Reset();
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = TEXT("Failed to spawn reusable transient SceneCapture2D.");
				return false;
			}

			CaptureActor = NewCaptureActor;
			CaptureComponent = NewCaptureComponent;
			SlowTask = MakeUnique<FScopedSlowTask>(static_cast<float>(CaptureViews.Num()), FText::FromString(TEXT("Exporting UESplatting scene capture dataset")));
			SlowTask->MakeDialog(true);

			if (UsesCalibratedLockedPhotometrics(Settings))
			{
				CalibrationInitialCompensation = FMath::Clamp(Settings.ManualExposureCompensation, -10.0f, 10.0f);
				CalibrationEffectiveCompensation = CalibrationInitialCompensation;
				const int32 DesiredSampleCount = FMath::Clamp(Settings.CalibrationSampleViewCount, 8, 64);
				const int32 SampleCount = FMath::Min(DesiredSampleCount, CaptureViews.Num());
				CalibrationSampleIndices.Reserve(SampleCount);
				for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
				{
					const int32 ViewIndex = FMath::Clamp(
						FMath::FloorToInt((static_cast<double>(SampleIndex) + 0.5) * static_cast<double>(CaptureViews.Num()) / static_cast<double>(SampleCount)),
						0,
						CaptureViews.Num() - 1);
					CalibrationSampleIndices.AddUnique(ViewIndex);
				}
				Phase = EDatasetExportPhase::CalibrationConfigure;
			}
			else
			{
				Phase = EDatasetExportPhase::ConfigureView;
			}

			StartResult.bSuccess = true;
			StartResult.OutputDirectory = OutputDirectory;
			StartResult.Message = FString::Printf(TEXT("Started multi-frame export of %d images."), CaptureViews.Num());
			return true;
		}

		bool Tick(float DeltaTime)
		{
			(void)DeltaTime;
			if (bFinished)
			{
				return false;
			}

			if (!ExportWorld.IsValid() || !CaptureActor.IsValid() || !CaptureComponent.IsValid() || !RenderTarget.IsValid())
			{
				Fail(TEXT("Scene capture resources became invalid during export."));
				return false;
			}

			if (SlowTask && SlowTask->ShouldCancel())
			{
				Fail(FString::Printf(TEXT("Scene capture dataset export canceled after %d images."), ExportedImages.Num()));
				return false;
			}

			switch (Phase)
			{
			case EDatasetExportPhase::CalibrationConfigure:
				return ConfigureCalibrationView();
			case EDatasetExportPhase::CalibrationWarmup:
				return CaptureCalibrationWarmupFrame();
			case EDatasetExportPhase::CalibrationCapture:
				return CaptureCalibrationSample();
			case EDatasetExportPhase::CalibrationPreviewConfigure:
				return ConfigureCalibrationPreview();
			case EDatasetExportPhase::CalibrationPreviewWarmup:
				return CaptureCalibrationPreviewWarmupFrame();
			case EDatasetExportPhase::CalibrationPreviewCapture:
				return CaptureCalibrationPreview();
			case EDatasetExportPhase::ConfigureView:
				return ConfigureCurrentView();
			case EDatasetExportPhase::Warmup:
				return CaptureWarmupFrame();
			case EDatasetExportPhase::Capture:
				return CaptureCurrentImage();
			case EDatasetExportPhase::Finalize:
				Finalize();
				return false;
			default:
				Fail(TEXT("Invalid dataset export state."));
				return false;
			}
		}

	private:
		bool ConfigureCalibrationView()
		{
			if (!CalibrationSampleIndices.IsValidIndex(CalibrationSampleCursor))
			{
				Fail(TEXT("Photometric calibration lost its representative-view cursor."));
				return false;
			}

			Settings.ManualExposureCompensation = CalibrationEffectiveCompensation;
			FString Error;
			if (!ConfigureCaptureView(
				CaptureComponent.Get(),
				CaptureViews[CalibrationSampleIndices[CalibrationSampleCursor]],
				Settings,
				RenderTarget.Get(),
				Error))
			{
				Fail(Error);
				return false;
			}

			WarmupFrameIndex = 0;
			Phase = Settings.CaptureWarmupFrames > 0
				? EDatasetExportPhase::CalibrationWarmup
				: EDatasetExportPhase::CalibrationCapture;
			return true;
		}

		bool CaptureCalibrationWarmupFrame()
		{
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), WarmupFrameIndex == 0, Error))
			{
				Fail(Error);
				return false;
			}
			++WarmupFrameIndex;
			if (WarmupFrameIndex >= FMath::Max(0, Settings.CaptureWarmupFrames))
			{
				Phase = EDatasetExportPhase::CalibrationCapture;
			}
			return true;
		}

		bool CaptureCalibrationSample()
		{
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), Settings.CaptureWarmupFrames == 0, Error))
			{
				Fail(Error);
				return false;
			}
			TArray<FColor> Pixels;
			if (!ReadCapturedPixels(RenderTarget.Get(), Pixels, Error))
			{
				Fail(Error);
				return false;
			}
			CalibrationFrameMeanLuminances.Add(ComputeFrameMeanLuminance(Pixels, Settings.ImageWidth, Settings.ImageHeight));
			++CalibrationSampleCursor;
			if (CalibrationSampleCursor < CalibrationSampleIndices.Num())
			{
				Phase = EDatasetExportPhase::CalibrationConfigure;
				return true;
			}
			return EvaluateCalibrationIteration();
		}

		bool EvaluateCalibrationIteration()
		{
			if (CalibrationFrameMeanLuminances.IsEmpty())
			{
				Fail(TEXT("Photometric calibration produced no luminance samples."));
				return false;
			}

			CalibrationUnsortedLuminances = CalibrationFrameMeanLuminances;
			CalibrationFrameMeanLuminances.Sort();
			const float P10 = GetSortedPercentile(CalibrationFrameMeanLuminances, 0.1f);
			const float Median = GetSortedPercentile(CalibrationFrameMeanLuminances, 0.5f);
			const float P90 = GetSortedPercentile(CalibrationFrameMeanLuminances, 0.9f);
			const float TargetMedian = FMath::Clamp(Settings.CalibrationTargetMedianLuminance, 32.0f, 192.0f);
			const float DarkTolerance = FMath::Clamp(Settings.CalibrationDarkTolerance, 2.0f, 32.0f);
			const float BrightTolerance = FMath::Clamp(Settings.CalibrationBrightTolerance, 2.0f, 32.0f);
			const bool bInsideHealthyBand = Median >= TargetMedian - DarkTolerance && Median <= TargetMedian + BrightTolerance;
			const float Adjustment = FMath::Clamp(FMath::Log2(TargetMedian / FMath::Max(Median, 1.0f)), -3.0f, 3.0f);
			UE_LOG(
				LogUESplatting,
				Display,
				TEXT("UESplatting export exposure preflight iteration %d: compensation=%+.3f, luma_p10=%.1f, median=%.1f, p90=%.1f, proposed_adjustment=%+.3f"),
				CalibrationIterationIndex + 1,
				CalibrationEffectiveCompensation,
				P10,
				Median,
				P90,
				Adjustment);

			constexpr int32 MaximumCalibrationIterations = 10;
			if (!bInsideHealthyBand)
			{
				if (CalibrationIterationIndex + 1 >= MaximumCalibrationIterations || FMath::Abs(Adjustment) <= 0.08f)
				{
					Fail(FString::Printf(
						TEXT("Exposure preflight could not reach the healthy luminance band. Last p10 / median / p90: %.1f / %.1f / %.1f at %+.2f EV. Use Explicit Locked Exposure or inspect scene lighting."),
						P10,
						Median,
						P90,
						CalibrationEffectiveCompensation));
					return false;
				}
				CalibrationEffectiveCompensation = FMath::Clamp(CalibrationEffectiveCompensation + Adjustment, -10.0f, 10.0f);
				++CalibrationIterationIndex;
				CalibrationSampleCursor = 0;
				CalibrationFrameMeanLuminances.Reset();
				Phase = EDatasetExportPhase::CalibrationConfigure;
				return true;
			}

			Settings.ManualExposureCompensation = CalibrationEffectiveCompensation;
			Settings.bPhotometricCalibrationPerformed = true;
			Settings.CalibrationInitialExposureCompensation = CalibrationInitialCompensation;
			Settings.CalibrationLuminanceP10 = P10;
			Settings.CalibrationLuminanceMedian = Median;
			Settings.CalibrationLuminanceP90 = P90;
			Settings.CalibrationViewsEvaluated = CalibrationFrameMeanLuminances.Num();

			float BestDifference = FLT_MAX;
			int32 RepresentativeSampleIndex = 0;
			for (int32 SampleIndex = 0; SampleIndex < CalibrationUnsortedLuminances.Num(); ++SampleIndex)
			{
				const float Difference = FMath::Abs(CalibrationUnsortedLuminances[SampleIndex] - Median);
				if (Difference < BestDifference)
				{
					BestDifference = Difference;
					RepresentativeSampleIndex = SampleIndex;
				}
			}
			CalibrationRepresentativeViewIndex = CalibrationSampleIndices[RepresentativeSampleIndex];

			if (!FApp::IsUnattended())
			{
				const FString Prompt = FString::Printf(
					TEXT("Global exposure preflight\n\nExposure compensation: %+.2f\nFrame mean luminance p10 / median / p90: %.1f / %.1f / %.1f\nRepresentative views: %d\n\nUse this one exposure for the full export?"),
					CalibrationEffectiveCompensation,
					P10,
					Median,
					P90,
					Settings.CalibrationViewsEvaluated);
				if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Prompt)) != EAppReturnType::Yes)
				{
					Fail(TEXT("Dataset export canceled after exposure preflight."));
					return false;
				}
			}

			Phase = EDatasetExportPhase::CalibrationPreviewConfigure;
			return true;
		}

		bool ConfigureCalibrationPreview()
		{
			FString Error;
			if (!CaptureViews.IsValidIndex(CalibrationRepresentativeViewIndex)
				|| !ConfigureCaptureView(CaptureComponent.Get(), CaptureViews[CalibrationRepresentativeViewIndex], Settings, RenderTarget.Get(), Error))
			{
				Fail(Error.IsEmpty() ? TEXT("Invalid representative calibration view.") : Error);
				return false;
			}
			WarmupFrameIndex = 0;
			Phase = Settings.CaptureWarmupFrames > 0
				? EDatasetExportPhase::CalibrationPreviewWarmup
				: EDatasetExportPhase::CalibrationPreviewCapture;
			return true;
		}

		bool CaptureCalibrationPreviewWarmupFrame()
		{
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), WarmupFrameIndex == 0, Error))
			{
				Fail(Error);
				return false;
			}
			++WarmupFrameIndex;
			if (WarmupFrameIndex >= FMath::Max(0, Settings.CaptureWarmupFrames))
			{
				Phase = EDatasetExportPhase::CalibrationPreviewCapture;
			}
			return true;
		}

		bool CaptureCalibrationPreview()
		{
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), Settings.CaptureWarmupFrames == 0, Error))
			{
				Fail(Error);
				return false;
			}
			const FString PreviewDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UESplatting"), TEXT("ExposurePreviews"));
			IFileManager::Get().MakeDirectory(*PreviewDirectory, true);
			const FString PreviewPath = FPaths::Combine(PreviewDirectory, CaptureId + TEXT("_calibrated.jpg"));
			FUESplattingDatasetExportSettings PreviewSettings = Settings;
			PreviewSettings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
			PreviewSettings.JpegQuality = 94;
			TArray<FColor> PreviewPixels;
			if (!SaveCapturedImage(RenderTarget.Get(), PreviewSettings, PreviewPath, PreviewPixels, Error))
			{
				Fail(Error);
				return false;
			}

			CurrentViewIndex = 0;
			Phase = EDatasetExportPhase::ConfigureView;
			return true;
		}

		bool ConfigureCurrentView()
		{
			if (!CaptureViews.IsValidIndex(CurrentViewIndex))
			{
				Phase = EDatasetExportPhase::Finalize;
				return true;
			}

			const int32 ImageId = CurrentViewIndex + 1;
			if (SlowTask)
			{
				SlowTask->EnterProgressFrame(
					1.0f,
					FText::FromString(FString::Printf(TEXT("Capturing image %d of %d"), ImageId, CaptureViews.Num())));
			}

			FString Error;
			if (!ConfigureCaptureView(CaptureComponent.Get(), CaptureViews[CurrentViewIndex], Settings, RenderTarget.Get(), Error))
			{
				Fail(Error);
				return false;
			}

			WarmupFrameIndex = 0;
			Phase = Settings.CaptureWarmupFrames > 0 ? EDatasetExportPhase::Warmup : EDatasetExportPhase::Capture;
			return true;
		}

		bool CaptureWarmupFrame()
		{
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), WarmupFrameIndex == 0, Error))
			{
				Fail(Error);
				return false;
			}

			++WarmupFrameIndex;
			if (WarmupFrameIndex >= FMath::Max(0, Settings.CaptureWarmupFrames))
			{
				Phase = EDatasetExportPhase::Capture;
			}
			return true;
		}

		bool CaptureCurrentImage()
		{
			const FUESplattingCaptureView& CaptureView = CaptureViews[CurrentViewIndex];
			FString Error;
			if (!CaptureSceneFrame(CaptureComponent.Get(), Settings.CaptureWarmupFrames == 0, Error))
			{
				Fail(Error);
				return false;
			}

			const int32 ImageId = CurrentViewIndex + 1;
			const FString ImageFileName = FString::Printf(TEXT("frame_%06d.%s"), ImageId, *GetImageFileExtension(Settings));
			const FString ImageName = FPaths::Combine(TEXT("images"), ImageFileName);
			const FString ImagePath = FPaths::Combine(ImagesDirectory, ImageFileName);
			TArray<FColor> CapturedPixels;
			if (!SaveCapturedImage(RenderTarget.Get(), Settings, ImagePath, CapturedPixels, Error))
			{
				Fail(Error);
				return false;
			}

			FExportedImage& ExportedImage = ExportedImages.AddDefaulted_GetRef();
			ExportedImage.ImageId = ImageId;
			ExportedImage.CameraId = ImageId;
			ExportedImage.StationIndex = CaptureView.StationIndex;
			ExportedImage.CaptureGroupId = CaptureView.CaptureGroupId;
			ExportedImage.CaptureGroupKind = CaptureView.CaptureGroupKind;
			ExportedImage.Name = ImageName;
			ExportedImage.CameraToWorld = CaptureView.Transform;
			ExportedImage.Pose = BuildColmapPose(CaptureView.Transform, Settings.WorldToColmapScale);
			ExportedImage.FocalLength = ResolveFocalLengthPixels(CaptureView.HorizontalFieldOfView, Settings.ImageWidth);
			ExportedImage.HorizontalFieldOfView = CaptureView.HorizontalFieldOfView;
			GenerateTracePointsForCamera(ExportWorld.Get(), CaptureView, Settings, CapturedPixels, ExportedImage, ExportedPoints, NextPointId);

			if (Settings.GarbageCollectEveryImages > 0 && (ImageId % Settings.GarbageCollectEveryImages) == 0)
			{
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
			}

			++CurrentViewIndex;
			Phase = CurrentViewIndex < CaptureViews.Num() ? EDatasetExportPhase::ConfigureView : EDatasetExportPhase::Finalize;
			return true;
		}

		void Finalize()
		{
			CleanupCaptureResources();

			FString Error;
			const bool bHasPointCloud = !ExportedPoints.IsEmpty();
			if (!WriteColmapFiles(SparseDirectory, ExportedImages, ExportedPoints, Settings, Error)
				|| !WriteOptionalSparsePointCloudPly(PointCloudPath, ExportedPoints, Error)
				|| !WriteTransformsJson(TransformsPath, ExportedImages, Settings, bHasPointCloud, Error)
				|| !WriteCaptureManifest(ManifestPath, CaptureId, ExportWorld.Get(), ExportedImages, ExportedPoints, Settings, Error))
			{
				Fail(Error, false);
				return;
			}

			IFileManager::Get().Delete(*IncompleteMarkerPath, false, true, true);
			Result.bSuccess = true;
			Result.ImageCount = ExportedImages.Num();
			Result.SparsePointCount = ExportedPoints.Num();
			Result.Message = bHasPointCloud
				? FString::Printf(
					TEXT("Exported known-pose scene capture dataset with %d images and %d optional collision seed points."),
					Result.ImageCount,
					Result.SparsePointCount)
				: FString::Printf(
					TEXT("Exported known-pose scene capture dataset with %d images and exact camera poses."),
					Result.ImageCount);
			Complete();
		}

		void Fail(const FString& Message, bool bCleanupResources = true)
		{
			if (bCleanupResources)
			{
				CleanupCaptureResources();
			}
			Result.bSuccess = false;
			Result.ImageCount = ExportedImages.Num();
			Result.SparsePointCount = ExportedPoints.Num();
			Result.Message = Message + FString::Printf(
				TEXT("\n\nThe partial folder remains marked incomplete:\n%s"),
				*OutputDirectory);
			Complete();
		}

		void CleanupCaptureResources()
		{
			if (USceneCaptureComponent2D* Component = CaptureComponent.Get())
			{
				Component->TextureTarget = nullptr;
			}
			if (ASceneCapture2D* Actor = CaptureActor.Get())
			{
				Actor->Destroy();
			}
			CaptureActor.Reset();
			CaptureComponent.Reset();
			if (RenderTarget.IsValid())
			{
				RenderTarget->ReleaseResource();
				RenderTarget.Reset();
			}
			FlushRenderingCommands();
		}

		void Complete()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			SlowTask.Reset();
			if (Completion)
			{
				Completion(Result);
			}
		}

		TWeakObjectPtr<UWorld> ExportWorld;
		TArray<FUESplattingCaptureView> CaptureViews;
		FUESplattingDatasetExportSettings Settings;
		FUESplattingDatasetExportResult Result;
		FUESplattingDatasetExportCompleted Completion;
		TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget;
		TWeakObjectPtr<ASceneCapture2D> CaptureActor;
		TWeakObjectPtr<USceneCaptureComponent2D> CaptureComponent;
		TUniquePtr<FScopedSlowTask> SlowTask;
		TArray<FExportedImage> ExportedImages;
		TArray<FExportedPoint> ExportedPoints;
		int64 NextPointId = 1;
		int32 CurrentViewIndex = 0;
		int32 WarmupFrameIndex = 0;
		TArray<int32> CalibrationSampleIndices;
		TArray<float> CalibrationFrameMeanLuminances;
		TArray<float> CalibrationUnsortedLuminances;
		int32 CalibrationSampleCursor = 0;
		int32 CalibrationIterationIndex = 0;
		int32 CalibrationRepresentativeViewIndex = INDEX_NONE;
		float CalibrationInitialCompensation = 0.0f;
		float CalibrationEffectiveCompensation = 0.0f;
		EDatasetExportPhase Phase = EDatasetExportPhase::ConfigureView;
		bool bFinished = false;
		FString OutputDirectory;
		FString CaptureId;
		FString ImagesDirectory;
		FString SparseDirectory;
		FString TransformsPath;
		FString PointCloudPath;
		FString ManifestPath;
		FString IncompleteMarkerPath;
	};

	class FMovieRenderQueueDatasetExportJob;
	static TSharedPtr<FMovieRenderQueueDatasetExportJob> ActiveMovieRenderQueueExportJob;

	class FMovieRenderQueueDatasetExportJob : public TSharedFromThis<FMovieRenderQueueDatasetExportJob>
	{
		struct FCaptureVisualizationState
		{
			TWeakObjectPtr<AUESplattingCaptureVolume> Volume;
			bool bActorHiddenInGame = false;
			bool bBoundsVisible = false;
			bool bBoundsHiddenInGame = true;
			bool bProbeVisible = false;
			bool bProbeHiddenInGame = false;
		};

		struct FGraphWarmUpState
		{
			TWeakObjectPtr<UMovieGraphWarmUpSettingNode> Node;
			bool bOverrideNumWarmUpFrames = false;
			int32 NumWarmUpFrames = 0;
		};

		struct FGraphCameraState
		{
			TWeakObjectPtr<UMovieGraphCameraSettingNode> Node;
			bool bOverrideRenderAllCameras = false;
			bool bRenderAllCameras = false;
		};

		struct FGraphJpegOutputState
		{
			TWeakObjectPtr<UMovieGraphImageSequenceOutputNode_JPG> Node;
			bool bOverrideFileNameFormat = false;
			FString FileNameFormat;
		};

		struct FGraphSamplingState
		{
			TWeakObjectPtr<UMovieGraphSamplingMethodNode> Node;
			bool bOverrideSamplingMethodClass = false;
			bool bOverrideTemporalSampleCount = false;
			FSoftClassPath SamplingMethodClass;
			int32 TemporalSampleCount = 1;
		};

		struct FInjectedGraphNodeState
		{
			TWeakObjectPtr<UMovieGraphConfig> Graph;
			TWeakObjectPtr<UMovieGraphNode> Node;
			TWeakObjectPtr<UMovieGraphPin> UpstreamPin;
			TWeakObjectPtr<UMovieGraphPin> TargetPin;
		};

		struct FCaptureStation
		{
			int32 StationIndex = INDEX_NONE;
			TArray<int32> ViewIndices;
		};

	public:
		bool Initialize(
			UObject* WorldContextObject,
			const TArray<FUESplattingCaptureView>& InCaptureViews,
			const FUESplattingDatasetExportSettings& InSettings,
			FUESplattingDatasetExportCompleted InCompletion,
			FUESplattingDatasetExportResult& StartResult)
		{
			CaptureViews = InCaptureViews;
			Settings = InSettings;
			Completion = MoveTemp(InCompletion);
			Result = FUESplattingDatasetExportResult();

			if (!GEditor || CaptureViews.IsEmpty())
			{
				StartResult.Message = CaptureViews.IsEmpty()
					? TEXT("No capture views were supplied.")
					: TEXT("Movie Render Queue dataset export requires the Unreal Editor.");
				return false;
			}
			if (Settings.ImageFormat != EUESplattingSceneCaptureImageFormat::JPEG)
			{
				StartResult.Message = TEXT("The Movie Render Queue dataset backend currently exports JPEG. Select JPEG, or explicitly use the legacy SceneCapture2D backend for PNG.");
				return false;
			}
			if (Settings.PhotometricMode != EUESplattingSceneCapturePhotometricMode::SceneAuthored)
			{
				StartResult.Message = TEXT("The Movie Render Queue backend preserves authored camera, volume, and project post processing. Select Match Active Viewport; explicit SceneCapture exposure modes are only available on the legacy backend.");
				return false;
			}
			if (Settings.ImageWidth <= 0 || Settings.ImageHeight <= 0 || Settings.WorldToColmapScale <= 0.0)
			{
				StartResult.Message = TEXT("Image dimensions and World to Dataset Scale must be positive.");
				return false;
			}
			if (!BuildCaptureStations(StartResult.Message))
			{
				return false;
			}
			ExportedImages.Reset();
			ExportedImages.Reserve(CaptureViews.Num());
			ExportedPoints.Reset();
			NextPointId = 1;

			UWorld* ResolvedWorld = nullptr;
			if (AActor* Actor = Cast<AActor>(WorldContextObject))
			{
				ResolvedWorld = Actor->GetWorld();
			}
			else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
			{
				ResolvedWorld = Component->GetWorld();
			}
			else if (GEngine && WorldContextObject)
			{
				ResolvedWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
			}
			if (!ResolvedWorld || ResolvedWorld->WorldType != EWorldType::Editor)
			{
				StartResult.Message = TEXT("Could not resolve the active editor world for Movie Render Queue dataset export.");
				return false;
			}

			UMoviePipelineQueueSubsystem* QueueSubsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
			if (!QueueSubsystem || QueueSubsystem->IsRendering())
			{
				StartResult.Message = TEXT("Movie Render Queue is already rendering another job.");
				return false;
			}

			ExportWorld = ResolvedWorld;
			OutputDirectory = ResolveOutputDirectory(Settings);
			CaptureId = ResolveCaptureId(Settings, OutputDirectory);
			ImagesDirectory = FPaths::Combine(OutputDirectory, TEXT("images"));
			MrqStagingDirectory = FPaths::Combine(OutputDirectory, TEXT(".uesplatting-mrq-staging"));
			SparseDirectory = FPaths::Combine(OutputDirectory, TEXT("colmap"), TEXT("sparse"), TEXT("0"));
			TransformsPath = FPaths::Combine(OutputDirectory, TEXT("transforms.json"));
			PointCloudPath = FPaths::Combine(OutputDirectory, TEXT("sparse_pc.ply"));
			ManifestPath = FPaths::Combine(OutputDirectory, TEXT("capture-manifest.json"));
			IncompleteMarkerPath = FPaths::Combine(OutputDirectory, TEXT(".uesplatting-capture-incomplete"));
			Result.OutputDirectory = OutputDirectory;

			if (IFileManager::Get().DirectoryExists(*OutputDirectory) && !IsDirectoryEmpty(OutputDirectory))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(
					TEXT("Output folder is not empty. Choose an empty folder so stale files cannot contaminate the dataset: %s"),
					*OutputDirectory);
				return false;
			}
			if (!EnsureDirectory(ImagesDirectory) || !EnsureDirectory(MrqStagingDirectory) || !EnsureDirectory(SparseDirectory))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(TEXT("Failed to create dataset directories under '%s'."), *OutputDirectory);
				return false;
			}
			if (!FFileHelper::SaveStringToFile(TEXT("UESplatting Movie Render Queue capture is still in progress.\n"), *IncompleteMarkerPath))
			{
				StartResult.OutputDirectory = OutputDirectory;
				StartResult.Message = FString::Printf(TEXT("Failed to create capture marker under '%s'."), *OutputDirectory);
				return false;
			}

			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				if (AActor* SelectedActor = Cast<AActor>(*It))
				{
					PreviousSelection.Add(SelectedActor);
				}
			}

			StartResult.bSuccess = true;
			StartResult.OutputDirectory = OutputDirectory;
			StartResult.Message = FString::Printf(
				TEXT("Prepared one-shot Movie Render Queue export of %d sequential known-pose images across %d capture stations."),
				CaptureViews.Num(),
				CaptureStations.Num());
			return true;
		}

		bool Start(FUESplattingDatasetExportResult& StartResult)
		{
			HideCaptureVisualizations();

			GEditor->SelectNone(false, true, false);
			GEditor->NoteSelectionChange();
			return StartRender(StartResult);
		}

	private:
		bool StartRender(FUESplattingDatasetExportResult& StartResult)
		{
			if (!GEditor || !ExportWorld.IsValid())
			{
				StartResult.Message = TEXT("Movie Render Queue capture state is invalid.");
				CleanupEditorState();
				return false;
			}

			UMovieGraphQuickRenderSubsystem* QuickRenderSubsystem = GEditor->GetEditorSubsystem<UMovieGraphQuickRenderSubsystem>();
			UMoviePipelineQueueSubsystem* QueueSubsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
			if (!QuickRenderSubsystem || !QueueSubsystem || QueueSubsystem->IsRendering() || GEditor->PlayWorld)
			{
				StartResult.Message = TEXT("Movie Render Queue is not ready to start the UESplatting capture.");
				CleanupEditorState();
				return false;
			}

			if (!EnsureDirectory(MrqStagingDirectory))
			{
				StartResult.Message = FString::Printf(TEXT("Failed to create MRQ staging folder '%s'."), *MrqStagingDirectory);
				CleanupEditorState();
				return false;
			}

			const FUESplattingCaptureView& FirstView = CaptureViews[0];
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transient | RF_NonPIEDuplicateTransient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.Name = MakeUniqueObjectName(ExportWorld.Get(), ACameraActor::StaticClass(), TEXT("UESplattingMRQCamera"));
			ACameraActor* CameraActor = ExportWorld->SpawnActor<ACameraActor>(
				ACameraActor::StaticClass(), FirstView.Transform, SpawnParameters);
			if (!CameraActor || !CameraActor->GetCameraComponent())
			{
				StartResult.Message = TEXT("Failed to create the transient sequential MRQ camera.");
				CleanupEditorState();
				return false;
			}
			TemporaryCamera = CameraActor;
			CameraActor->SetActorLabel(TEXT("UESplatting Sequential Capture Camera"), false);
			CameraActor->SetFolderPath(TEXT("UESplatting/Temporary Capture"));
			UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
			CameraComponent->SetFieldOfView(FMath::Clamp(FirstView.HorizontalFieldOfView, 1.0f, 179.0f));
			CameraComponent->SetAspectRatio(static_cast<float>(Settings.ImageWidth) / static_cast<float>(Settings.ImageHeight));
			CameraComponent->SetConstraintAspectRatio(true);
			CameraComponent->PostProcessSettings = FirstView.PostProcessSettings;
			CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount = true;
			CameraComponent->PostProcessSettings.MotionBlurAmount = 0.0f;
			CameraComponent->PostProcessBlendWeight = FirstView.PostProcessBlendWeight;

			FString Error;
			if (!BuildSequentialRenderSequence(Error))
			{
				StartResult.Message = Error;
				CleanupEditorState();
				return false;
			}

			UMovieGraphConfig* DefaultGraph = LoadObject<UMovieGraphConfig>(
				nullptr,
				TEXT("/MovieRenderPipeline/DefaultQuickRenderGraph.DefaultQuickRenderGraph"));
			ActiveMovieGraph.Reset(DefaultGraph);
			bMovieGraphPackageWasDirty = DefaultGraph && DefaultGraph->GetOutermost()->IsDirty();
			if (!DefaultGraph || !ApplyMovieGraphSettings(DefaultGraph, Error))
			{
				StartResult.Message = DefaultGraph ? Error : TEXT("Could not load Unreal's DefaultQuickRenderGraph.");
				CleanupEditorState();
				return false;
			}

			QuickRenderSettings.Reset(NewObject<UMovieGraphQuickRenderModeSettings>(GetTransientPackage()));
			QuickRenderSettings->GraphPreset = DefaultGraph;
			QuickRenderSettings->LevelSequenceOverride = MasterRenderSequence.Get();
			QuickRenderSettings->PostRenderBehavior = EMoviePipelinePostRenderActionType::DoNothing;
			QuickRenderSettings->bOverride_ViewportLookFlags = false;
			QuickRenderSettings->ViewportLookFlags = static_cast<int32>(EMovieGraphQuickRenderViewportLookFlags::None);
			UMovieGraphQuickRenderModeSettings::RefreshVariableAssignments(QuickRenderSettings.Get());

			UMovieJobVariableAssignmentContainer* Assignments = QuickRenderSettings->GetVariableAssignmentsForGraph(DefaultGraph);
			if (!Assignments)
			{
				StartResult.Message = TEXT("Could not create Movie Render Queue graph variable assignments.");
				CleanupEditorState();
				return false;
			}

			FString NormalizedStagingDirectory = FPaths::ConvertRelativePathToFull(MrqStagingDirectory);
			FPaths::NormalizeFilename(NormalizedStagingDirectory);
			NormalizedStagingDirectory.ReplaceInline(TEXT("\""), TEXT("\\\""));
			bool bConfiguredOutputDirectory = false;
			bool bConfiguredOutputResolution = false;
			for (UMovieGraphVariable* Variable : DefaultGraph->GetVariables())
			{
				if (!Variable)
				{
					continue;
				}
				if (Variable->GetMemberName() == TEXT("OutputDirectory"))
				{
					bConfiguredOutputDirectory = Assignments->SetValueSerializedString(
						Variable,
						FString::Printf(TEXT("(Path=\"%s\")"), *NormalizedStagingDirectory));
					Assignments->SetVariableAssignmentEnableState(Variable, true);
				}
				else if (Variable->GetMemberName() == TEXT("OutputResolution"))
				{
					bConfiguredOutputResolution = Assignments->SetValueSerializedString(
						Variable,
						FString::Printf(
							TEXT("(ProfileName=\"\",Resolution=(X=%d,Y=%d),Description=\"\")"),
							Settings.ImageWidth,
							Settings.ImageHeight));
					Assignments->SetVariableAssignmentEnableState(Variable, true);
				}
			}
			if (!bConfiguredOutputDirectory || !bConfiguredOutputResolution)
			{
				StartResult.Message = TEXT("Unreal's DefaultQuickRenderGraph no longer exposes the expected OutputDirectory and OutputResolution variables.");
				CleanupEditorState();
				return false;
			}

			UUESplattingCaptureTimeStep::SetSceneFreezeRequested(Settings.bFreezeSceneDuringCapture);
			QuickRenderSubsystem->BeginQuickRender(EMovieGraphQuickRenderMode::CurrentSequence, QuickRenderSettings.Get());
			RestoreMovieGraphSettings();
			UMoviePipelineExecutorBase* Executor = QueueSubsystem->GetActiveExecutor();
			if (!Executor)
			{
				StartResult.Message = TEXT("Movie Render Queue did not start the UESplatting sequential capture.");
				CleanupEditorState();
				return false;
			}

			const TWeakPtr<FMovieRenderQueueDatasetExportJob> WeakThis = AsShared();
			Executor->OnExecutorFinished().AddLambda(
				[WeakThis](UMoviePipelineExecutorBase*, bool bSuccess)
				{
					if (const TSharedPtr<FMovieRenderQueueDatasetExportJob> Job = WeakThis.Pin())
					{
						Job->HandleRenderFinished(bSuccess);
						if (Job->bFinished && ActiveMovieRenderQueueExportJob == Job)
						{
							ActiveMovieRenderQueueExportJob.Reset();
						}
					}
				});

			StartResult.bSuccess = true;
			StartResult.OutputDirectory = OutputDirectory;
			StartResult.Message = FString::Printf(
				TEXT("Rendering %d known-pose images in one sequential MRQ shot."),
				CaptureViews.Num());
			return true;
		}

		void HandleRenderFinished(bool bSuccess)
		{
			if (bFinished)
			{
				return;
			}
			if (!bSuccess)
			{
				Fail(TEXT("Movie Render Queue reported a render failure."));
				return;
			}

			TArray<FString> RenderedFiles;
			IFileManager::Get().FindFilesRecursive(RenderedFiles, *MrqStagingDirectory, TEXT("*.jpg"), true, false, false);
			IFileManager::Get().FindFilesRecursive(RenderedFiles, *MrqStagingDirectory, TEXT("*.jpeg"), true, false, false);
			if (RenderedFiles.Num() != CaptureViews.Num())
			{
				Fail(FString::Printf(
					TEXT("Movie Render Queue produced %d JPEGs for %d sequential capture views. The partial output remains for diagnosis."),
					RenderedFiles.Num(),
					CaptureViews.Num()));
				return;
			}

			auto ExtractRelativeFrameNumber = [](const FString& Path)
			{
				const FString BaseName = FPaths::GetBaseFilename(Path);
				int32 SeparatorIndex = INDEX_NONE;
				return BaseName.FindLastChar(TEXT('_'), SeparatorIndex)
					? FCString::Atoi(*BaseName.Mid(SeparatorIndex + 1))
					: INDEX_NONE;
			};
			RenderedFiles.Sort([&ExtractRelativeFrameNumber](const FString& A, const FString& B)
			{
				return ExtractRelativeFrameNumber(A) < ExtractRelativeFrameNumber(B);
			});
			const int32 FirstRelativeFrame = ExtractRelativeFrameNumber(RenderedFiles[0]);
			for (int32 FileIndex = 0; FileIndex < RenderedFiles.Num(); ++FileIndex)
			{
				if (FirstRelativeFrame < 0 || ExtractRelativeFrameNumber(RenderedFiles[FileIndex]) != FirstRelativeFrame + FileIndex)
				{
					Fail(TEXT("Movie Render Queue produced duplicate, missing, or unrecognized sequential frame numbers. The partial output remains for diagnosis."));
					return;
				}
			}

			for (int32 ViewIndex = 0; ViewIndex < CaptureViews.Num(); ++ViewIndex)
			{
				const int32 ImageId = ViewIndex + 1;
				const FString ImageFileName = FString::Printf(TEXT("frame_%06d.jpg"), ImageId);
				const FString ImagePath = FPaths::Combine(ImagesDirectory, ImageFileName);
				if (!IFileManager::Get().Move(*ImagePath, *RenderedFiles[ViewIndex], true, true, false, true))
				{
					Fail(FString::Printf(TEXT("Failed to move MRQ output into '%s'."), *ImagePath));
					return;
				}

				const FUESplattingCaptureView& CaptureView = CaptureViews[ViewIndex];
				FExportedImage& ExportedImage = ExportedImages.AddDefaulted_GetRef();
				ExportedImage.ImageId = ImageId;
				ExportedImage.CameraId = ImageId;
				ExportedImage.StationIndex = CaptureView.StationIndex;
				ExportedImage.CaptureGroupId = CaptureView.CaptureGroupId;
				ExportedImage.CaptureGroupKind = CaptureView.CaptureGroupKind;
				ExportedImage.Name = FPaths::Combine(TEXT("images"), ImageFileName);
				ExportedImage.CameraToWorld = CaptureView.Transform;
				ExportedImage.Pose = BuildColmapPose(CaptureView.Transform, Settings.WorldToColmapScale);
				ExportedImage.FocalLength = ResolveFocalLengthPixels(CaptureView.HorizontalFieldOfView, Settings.ImageWidth);
				ExportedImage.HorizontalFieldOfView = CaptureView.HorizontalFieldOfView;

				TArray<FColor> Pixels;
				if (Settings.bGenerateTracePointCloud)
				{
					FImage LoadedImage;
					if (!FImageUtils::LoadImage(*ImagePath, LoadedImage))
					{
						Fail(FString::Printf(TEXT("Failed to read rendered image '%s' for sparse-point colors."), *ImagePath));
						return;
					}
					if (LoadedImage.SizeX != Settings.ImageWidth || LoadedImage.SizeY != Settings.ImageHeight)
					{
						Fail(FString::Printf(
							TEXT("MRQ output '%s' is %lldx%lld; expected %dx%d."),
							*ImagePath,
							LoadedImage.SizeX,
							LoadedImage.SizeY,
							Settings.ImageWidth,
							Settings.ImageHeight));
						return;
					}
					LoadedImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
					const TArrayView64<FColor> LoadedPixels = LoadedImage.AsBGRA8();
					Pixels.Append(LoadedPixels.GetData(), static_cast<int32>(LoadedPixels.Num()));
				}

				GenerateTracePointsForCamera(
					ExportWorld.Get(),
					CaptureView,
					Settings,
					Pixels,
					ExportedImage,
					ExportedPoints,
					NextPointId);
			}

			ExportedImages.Sort([](const FExportedImage& A, const FExportedImage& B)
			{
				return A.ImageId < B.ImageId;
			});
			CleanupEditorState();
			IFileManager::Get().DeleteDirectory(*MrqStagingDirectory, false, true);

			FString Error;
			const bool bHasPointCloud = !ExportedPoints.IsEmpty();
			if (!WriteColmapFiles(SparseDirectory, ExportedImages, ExportedPoints, Settings, Error)
				|| !WriteOptionalSparsePointCloudPly(PointCloudPath, ExportedPoints, Error)
				|| !WriteTransformsJson(TransformsPath, ExportedImages, Settings, bHasPointCloud, Error)
				|| !WriteCaptureManifest(ManifestPath, CaptureId, ExportWorld.Get(), ExportedImages, ExportedPoints, Settings, Error))
			{
				Fail(Error, false);
				return;
			}

			IFileManager::Get().Delete(*IncompleteMarkerPath, false, true, true);
			Result.bSuccess = true;
			Result.ImageCount = ExportedImages.Num();
			Result.SparsePointCount = ExportedPoints.Num();
			Result.Message = bHasPointCloud
				? FString::Printf(
					TEXT("Exported Movie Render Queue dataset with %d images and %d optional collision seed points."),
					Result.ImageCount,
					Result.SparsePointCount)
				: FString::Printf(
					TEXT("Exported Movie Render Queue dataset with %d images and exact camera poses."),
					Result.ImageCount);
			Complete();
		}

		void Fail(const FString& Message, bool bCleanupEditorState = true)
		{
			if (bCleanupEditorState)
			{
				CleanupEditorState();
			}
			Result.bSuccess = false;
			Result.Message = Message + FString::Printf(
				TEXT("\n\nThe partial folder remains marked incomplete:\n%s"),
				*OutputDirectory);
			Complete();
		}

		void CleanupRenderState()
		{
			UUESplattingCaptureTimeStep::SetSceneFreezeRequested(false);
			RestoreMovieGraphSettings();
			QuickRenderSettings.Reset();
			ActiveMovieGraph.Reset();
			bMovieGraphPackageWasDirty = false;

			if (TemporaryCamera.IsValid())
			{
				TemporaryCamera->Destroy();
			}
			TemporaryCamera.Reset();
			MasterRenderSequence.Reset();
		}

		void CleanupEditorState()
		{
			CleanupRenderState();
			RestoreCaptureVisualizations();

			if (!GEditor)
			{
				PreviousSelection.Reset();
				return;
			}

			GEditor->SelectNone(false, true, false);
			for (const TWeakObjectPtr<AActor>& Actor : PreviousSelection)
			{
				if (Actor.IsValid())
				{
					GEditor->SelectActor(Actor.Get(), true, false, true);
				}
			}
			PreviousSelection.Reset();
			GEditor->NoteSelectionChange();
		}

		void HideCaptureVisualizations()
		{
			UWorld* World = ExportWorld.Get();
			if (!World || !CaptureVisualizationStates.IsEmpty())
			{
				return;
			}

			for (TActorIterator<AUESplattingCaptureVolume> It(World); It; ++It)
			{
				AUESplattingCaptureVolume* Volume = *It;
				if (!Volume)
				{
					continue;
				}

				FCaptureVisualizationState& State = CaptureVisualizationStates.AddDefaulted_GetRef();
				State.Volume = Volume;
				State.bActorHiddenInGame = Volume->IsHidden();
				if (Volume->CaptureBounds)
				{
					State.bBoundsVisible = Volume->CaptureBounds->IsVisible();
					State.bBoundsHiddenInGame = Volume->CaptureBounds->bHiddenInGame;
				}
				if (Volume->ProbePreview)
				{
					State.bProbeVisible = Volume->ProbePreview->IsVisible();
					State.bProbeHiddenInGame = Volume->ProbePreview->bHiddenInGame;
				}

				Volume->SetActorHiddenInGame(true);
				if (Volume->CaptureBounds)
				{
					Volume->CaptureBounds->SetVisibility(false, true);
					Volume->CaptureBounds->SetHiddenInGame(true, true);
				}
				if (Volume->ProbePreview)
				{
					Volume->ProbePreview->SetVisibility(false, true);
					Volume->ProbePreview->SetHiddenInGame(true, true);
				}
			}
		}

		bool BuildCaptureStations(FString& OutError)
		{
			CaptureStations.Reset();
			const int32 StationCount = UUESplattingDatasetExporter::NormalizeCaptureStationIndices(CaptureViews);
			if (StationCount <= 0)
			{
				OutError = TEXT("No capture stations could be built from the supplied views.");
				return false;
			}

			CaptureStations.SetNum(StationCount);
			for (int32 StationIndex = 0; StationIndex < StationCount; ++StationIndex)
			{
				CaptureStations[StationIndex].StationIndex = StationIndex;
			}

			for (int32 ViewIndex = 0; ViewIndex < CaptureViews.Num(); ++ViewIndex)
			{
				const int32 StationIndex = CaptureViews[ViewIndex].StationIndex;
				if (!CaptureStations.IsValidIndex(StationIndex))
				{
					OutError = FString::Printf(TEXT("Capture view %d has an invalid normalized station id."), ViewIndex);
					return false;
				}
				CaptureStations[StationIndex].ViewIndices.Add(ViewIndex);
			}
			return true;
		}

		bool BuildSequentialRenderSequence(FString& OutError)
		{
			ACameraActor* CameraActor = TemporaryCamera.Get();
			if (!CameraActor || !CameraActor->GetCameraComponent() || CaptureViews.IsEmpty())
			{
				OutError = TEXT("Movie Render Queue sequential camera setup is invalid.");
				return false;
			}

			ULevelSequence* MasterSequence = NewObject<ULevelSequence>(
				GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), ULevelSequence::StaticClass(), TEXT("UESplattingMRQSequentialCapture")),
				RF_Transient);
			if (!MasterSequence)
			{
				OutError = TEXT("Failed to create the transient sequential Movie Render Queue sequence.");
				return false;
			}
			MasterSequence->Initialize();
			MasterRenderSequence.Reset(MasterSequence);

			UMovieScene* MovieScene = MasterSequence->GetMovieScene();
			if (!MovieScene)
			{
				OutError = TEXT("The transient sequential Movie Render Queue sequence has no Movie Scene.");
				return false;
			}

			const FFrameRate DisplayRate(30, 1);
			const FFrameRate TickResolution(24000, 1);
			const FFrameNumber FrameDuration = FFrameRate::TransformTime(
				FFrameTime(FFrameNumber(1)), DisplayRate, TickResolution).RoundToFrame();
			MovieScene->SetDisplayRate(DisplayRate);
			MovieScene->SetTickResolutionDirectly(TickResolution);

			UE::Sequencer::FCreateBindingParams BindingParams;
			BindingParams.bSpawnable = true;
			BindingParams.bAllowCustomBinding = true;
			BindingParams.BindingNameOverride = TEXT("UESplatting Sequential Capture Camera");
			const FGuid CameraActorBinding = FSequencerUtilities::CreateOrReplaceBinding(
				nullptr,
				MasterSequence,
				CameraActor,
				BindingParams);
			if (!CameraActorBinding.IsValid())
			{
				OutError = TEXT("Failed to create the sequential MRQ camera actor binding.");
				return false;
			}

			UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
			const FGuid CameraComponentBinding = MovieScene->AddPossessable(
				CameraComponent->GetName(),
				UCameraComponent::StaticClass());
			FMovieScenePossessable* CameraPossessable = MovieScene->FindPossessable(CameraComponentBinding);
			if (!CameraComponentBinding.IsValid() || !CameraPossessable)
			{
				OutError = TEXT("Failed to create the sequential MRQ camera component binding.");
				return false;
			}
			CameraPossessable->SetParent(CameraActorBinding, MovieScene);
			if (FMovieSceneSpawnable* ParentSpawnable = MovieScene->FindSpawnable(CameraActorBinding))
			{
				ParentSpawnable->AddChildPossessable(CameraComponentBinding);
			}
			MasterSequence->BindPossessableObject(CameraComponentBinding, *CameraComponent, CameraActor);

			UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraActorBinding);
			UMovieScene3DTransformSection* TransformSection = TransformTrack
				? Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection())
				: nullptr;
			if (!TransformTrack || !TransformSection)
			{
				OutError = TEXT("Failed to create the sequential MRQ camera transform track.");
				return false;
			}
			TransformTrack->AddSection(*TransformSection);
			TransformSection->SetMask(FMovieSceneTransformMask(EMovieSceneTransformChannel::All));

			UMovieSceneFloatTrack* FovTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(CameraComponentBinding);
			UMovieSceneFloatSection* FovSection = FovTrack
				? Cast<UMovieSceneFloatSection>(FovTrack->CreateNewSection())
				: nullptr;
			if (!FovTrack || !FovSection)
			{
				OutError = TEXT("Failed to create the sequential MRQ camera field-of-view track.");
				return false;
			}
			FovTrack->SetPropertyNameAndPath(GET_MEMBER_NAME_CHECKED(UCameraComponent, FieldOfView), TEXT("FieldOfView"));
			FovTrack->AddSection(*FovSection);

			const FFrameNumber EndFrame(CaptureViews.Num() * FrameDuration.Value);
			const TRange<FFrameNumber> CaptureRange(FFrameNumber(0), EndFrame);
			TransformSection->SetRange(CaptureRange);
			FovSection->SetRange(CaptureRange);
			TArrayView<FMovieSceneDoubleChannel*> TransformChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			FMovieSceneFloatChannel* FovChannel = FovSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
			if (TransformChannels.Num() < 9 || !FovChannel)
			{
				OutError = TEXT("Sequential MRQ camera tracks do not expose the expected transform and FOV channels.");
				return false;
			}

			for (int32 ViewIndex = 0; ViewIndex < CaptureViews.Num(); ++ViewIndex)
			{
				const FUESplattingCaptureView& View = CaptureViews[ViewIndex];
				const FFrameNumber KeyTime(ViewIndex * FrameDuration.Value);
				const FVector Location = View.Transform.GetLocation();
				const FVector Rotation = View.Transform.GetRotation().Euler();
				const FVector Scale = View.Transform.GetScale3D();
				TransformChannels[0]->AddConstantKey(KeyTime, Location.X);
				TransformChannels[1]->AddConstantKey(KeyTime, Location.Y);
				TransformChannels[2]->AddConstantKey(KeyTime, Location.Z);
				TransformChannels[3]->AddConstantKey(KeyTime, Rotation.X);
				TransformChannels[4]->AddConstantKey(KeyTime, Rotation.Y);
				TransformChannels[5]->AddConstantKey(KeyTime, Rotation.Z);
				TransformChannels[6]->AddConstantKey(KeyTime, Scale.X);
				TransformChannels[7]->AddConstantKey(KeyTime, Scale.Y);
				TransformChannels[8]->AddConstantKey(KeyTime, Scale.Z);
				FovChannel->AddConstantKey(KeyTime, FMath::Clamp(View.HorizontalFieldOfView, 1.0f, 179.0f));
			}

			UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(
				MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
			UMovieSceneCameraCutSection* CameraCutSection = CameraCutTrack
				? CameraCutTrack->AddNewCameraCut(
					UE::MovieScene::FRelativeObjectBindingID(CameraActorBinding),
					FFrameNumber(0))
				: nullptr;
			if (!CameraCutSection)
			{
				OutError = TEXT("Failed to create the single camera cut for the sequential MRQ shot.");
				return false;
			}
			CameraCutSection->SetRange(CaptureRange);

			MovieScene->SetPlaybackRange(CaptureRange);
			return true;
		}

		bool ApplyMovieGraphSettings(UMovieGraphConfig* Graph, FString& OutError)
		{
			if (!Graph || bMovieGraphSettingsApplied || !GraphWarmUpStates.IsEmpty() || !GraphCameraStates.IsEmpty()
				|| !GraphJpegOutputStates.IsEmpty() || !GraphSamplingStates.IsEmpty())
			{
				OutError = TEXT("Movie Render Queue graph settings could not be initialized.");
				return false;
			}
			bMovieGraphSettingsApplied = true;

			TSet<UMovieGraphConfig*> GraphsToConfigure;
			GraphsToConfigure.Add(Graph);
			Graph->GetAllContainedSubgraphs(GraphsToConfigure);
			for (UMovieGraphConfig* GraphToConfigure : GraphsToConfigure)
			{
				if (!GraphToConfigure)
				{
					continue;
				}

				for (UMovieGraphNode* Node : GraphToConfigure->GetNodes())
				{
					if (UMovieGraphWarmUpSettingNode* WarmUpNode = Cast<UMovieGraphWarmUpSettingNode>(Node))
					{
						FGraphWarmUpState& State = GraphWarmUpStates.AddDefaulted_GetRef();
						State.Node = WarmUpNode;
						State.bOverrideNumWarmUpFrames = WarmUpNode->bOverride_NumWarmUpFrames;
						State.NumWarmUpFrames = WarmUpNode->NumWarmUpFrames;
						WarmUpNode->bOverride_NumWarmUpFrames = true;
						WarmUpNode->NumWarmUpFrames = 0;
					}
					else if (UMovieGraphCameraSettingNode* CameraNode = Cast<UMovieGraphCameraSettingNode>(Node))
					{
						FGraphCameraState& State = GraphCameraStates.AddDefaulted_GetRef();
						State.Node = CameraNode;
						State.bOverrideRenderAllCameras = CameraNode->bOverride_bRenderAllCameras;
						State.bRenderAllCameras = CameraNode->bRenderAllCameras;
						CameraNode->bOverride_bRenderAllCameras = true;
						CameraNode->bRenderAllCameras = false;
					}
					else if (UMovieGraphSamplingMethodNode* SamplingNode = Cast<UMovieGraphSamplingMethodNode>(Node))
					{
						FGraphSamplingState& State = GraphSamplingStates.AddDefaulted_GetRef();
						State.Node = SamplingNode;
						State.bOverrideSamplingMethodClass = SamplingNode->bOverride_SamplingMethodClass;
						State.bOverrideTemporalSampleCount = SamplingNode->bOverride_TemporalSampleCount;
						State.SamplingMethodClass = SamplingNode->SamplingMethodClass;
						State.TemporalSampleCount = SamplingNode->TemporalSampleCount;
						SamplingNode->bOverride_SamplingMethodClass = true;
						SamplingNode->SamplingMethodClass = FSoftClassPath(UUESplattingCaptureTimeStep::StaticClass());
						SamplingNode->bOverride_TemporalSampleCount = true;
						SamplingNode->TemporalSampleCount = 1;
					}
					else if (UMovieGraphImageSequenceOutputNode_JPG* JpegNode = Cast<UMovieGraphImageSequenceOutputNode_JPG>(Node))
					{
						FGraphJpegOutputState& State = GraphJpegOutputStates.AddDefaulted_GetRef();
						State.Node = JpegNode;
						State.bOverrideFileNameFormat = JpegNode->bOverride_FileNameFormat;
						State.FileNameFormat = JpegNode->FileNameFormat;
						JpegNode->bOverride_FileNameFormat = true;
						JpegNode->FileNameFormat = TEXT("frame_{frame_number_rel}");
					}
				}
			}

			if (GraphCameraStates.IsEmpty())
			{
				UMovieGraphPin* TargetPin = Graph->GetOutputNode()->GetInputPin(UMovieGraphNode::GlobalsPinName);
				UMovieGraphPin* UpstreamPin = TargetPin ? TargetPin->GetFirstConnectedPin() : nullptr;
				UMovieGraphCameraSettingNode* CameraNode = Cast<UMovieGraphCameraSettingNode>(
					Graph->InsertBefore(
						Graph->GetOutputNode(),
						UMovieGraphCameraSettingNode::StaticClass(),
						UMovieGraphNode::GlobalsPinName));
				if (CameraNode)
				{
					FInjectedGraphNodeState& InjectedState = InjectedGraphNodeStates.AddDefaulted_GetRef();
					InjectedState.Graph = Graph;
					InjectedState.Node = CameraNode;
					InjectedState.UpstreamPin = UpstreamPin;
					InjectedState.TargetPin = TargetPin;
					FGraphCameraState& CameraState = GraphCameraStates.AddDefaulted_GetRef();
					CameraState.Node = CameraNode;
					CameraState.bOverrideRenderAllCameras = CameraNode->bOverride_bRenderAllCameras;
					CameraState.bRenderAllCameras = CameraNode->bRenderAllCameras;
					CameraNode->bOverride_bRenderAllCameras = true;
					CameraNode->bRenderAllCameras = false;
				}
			}

			if (GraphSamplingStates.IsEmpty())
			{
				UMovieGraphPin* TargetPin = Graph->GetOutputNode()->GetInputPin(UMovieGraphNode::GlobalsPinName);
				UMovieGraphPin* UpstreamPin = TargetPin ? TargetPin->GetFirstConnectedPin() : nullptr;
				UMovieGraphSamplingMethodNode* SamplingNode = Cast<UMovieGraphSamplingMethodNode>(
					Graph->InsertBefore(
						Graph->GetOutputNode(),
						UMovieGraphSamplingMethodNode::StaticClass(),
						UMovieGraphNode::GlobalsPinName));
				if (SamplingNode)
				{
					FInjectedGraphNodeState& InjectedState = InjectedGraphNodeStates.AddDefaulted_GetRef();
					InjectedState.Graph = Graph;
					InjectedState.Node = SamplingNode;
					InjectedState.UpstreamPin = UpstreamPin;
					InjectedState.TargetPin = TargetPin;
					FGraphSamplingState& State = GraphSamplingStates.AddDefaulted_GetRef();
					State.Node = SamplingNode;
					State.bOverrideSamplingMethodClass = SamplingNode->bOverride_SamplingMethodClass;
					State.bOverrideTemporalSampleCount = SamplingNode->bOverride_TemporalSampleCount;
					State.SamplingMethodClass = SamplingNode->SamplingMethodClass;
					State.TemporalSampleCount = SamplingNode->TemporalSampleCount;
					SamplingNode->bOverride_SamplingMethodClass = true;
					SamplingNode->SamplingMethodClass = FSoftClassPath(UUESplattingCaptureTimeStep::StaticClass());
					SamplingNode->bOverride_TemporalSampleCount = true;
					SamplingNode->TemporalSampleCount = 1;
				}
			}

			if (GraphWarmUpStates.IsEmpty() || GraphCameraStates.IsEmpty() || GraphJpegOutputStates.IsEmpty() || GraphSamplingStates.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Unreal's DefaultQuickRenderGraph is missing a node required by UESplatting (graphs=%d, warmup=%d, camera=%d, sampling=%d, JPEG=%d)."),
					GraphsToConfigure.Num(),
					GraphWarmUpStates.Num(),
					GraphCameraStates.Num(),
					GraphSamplingStates.Num(),
					GraphJpegOutputStates.Num());
				return false;
			}
			return true;
		}

		void RestoreMovieGraphSettings()
		{
			if (!bMovieGraphSettingsApplied)
			{
				return;
			}
			for (const FGraphWarmUpState& State : GraphWarmUpStates)
			{
				if (UMovieGraphWarmUpSettingNode* Node = State.Node.Get())
				{
					Node->bOverride_NumWarmUpFrames = State.bOverrideNumWarmUpFrames;
					Node->NumWarmUpFrames = State.NumWarmUpFrames;
				}
			}
			for (const FGraphCameraState& State : GraphCameraStates)
			{
				if (UMovieGraphCameraSettingNode* Node = State.Node.Get())
				{
					Node->bOverride_bRenderAllCameras = State.bOverrideRenderAllCameras;
					Node->bRenderAllCameras = State.bRenderAllCameras;
				}
			}
			for (const FGraphSamplingState& State : GraphSamplingStates)
			{
				if (UMovieGraphSamplingMethodNode* Node = State.Node.Get())
				{
					Node->bOverride_SamplingMethodClass = State.bOverrideSamplingMethodClass;
					Node->bOverride_TemporalSampleCount = State.bOverrideTemporalSampleCount;
					Node->SamplingMethodClass = State.SamplingMethodClass;
					Node->TemporalSampleCount = State.TemporalSampleCount;
				}
			}
			for (const FGraphJpegOutputState& State : GraphJpegOutputStates)
			{
				if (UMovieGraphImageSequenceOutputNode_JPG* Node = State.Node.Get())
				{
					Node->bOverride_FileNameFormat = State.bOverrideFileNameFormat;
					Node->FileNameFormat = State.FileNameFormat;
				}
			}
			for (const FInjectedGraphNodeState& State : InjectedGraphNodeStates)
			{
				if (UMovieGraphConfig* Graph = State.Graph.Get())
				{
					if (UMovieGraphNode* Node = State.Node.Get())
					{
						Graph->RemoveNode(Node);
					}
					if (UMovieGraphPin* UpstreamPin = State.UpstreamPin.Get())
					{
						if (UMovieGraphPin* TargetPin = State.TargetPin.Get())
						{
							UpstreamPin->AddEdgeTo(TargetPin);
						}
					}
				}
			}
			if (UMovieGraphConfig* Graph = ActiveMovieGraph.Get())
			{
				if (!bMovieGraphPackageWasDirty)
				{
					Graph->GetOutermost()->SetDirtyFlag(false);
				}
			}
			GraphWarmUpStates.Reset();
			GraphCameraStates.Reset();
			GraphSamplingStates.Reset();
			GraphJpegOutputStates.Reset();
			InjectedGraphNodeStates.Reset();
			bMovieGraphSettingsApplied = false;
		}

		void RestoreCaptureVisualizations()
		{
			for (const FCaptureVisualizationState& State : CaptureVisualizationStates)
			{
				AUESplattingCaptureVolume* Volume = State.Volume.Get();
				if (!Volume)
				{
					continue;
				}

				Volume->SetActorHiddenInGame(State.bActorHiddenInGame);
				if (Volume->CaptureBounds)
				{
					Volume->CaptureBounds->SetHiddenInGame(State.bBoundsHiddenInGame, true);
					Volume->CaptureBounds->SetVisibility(State.bBoundsVisible, true);
				}
				if (Volume->ProbePreview)
				{
					Volume->ProbePreview->SetHiddenInGame(State.bProbeHiddenInGame, true);
					Volume->ProbePreview->SetVisibility(State.bProbeVisible, true);
				}
			}
			CaptureVisualizationStates.Reset();
		}

		void Complete()
		{
			if (bFinished)
			{
				return;
			}
			bFinished = true;
			UUESplattingCaptureTimeStep::SetSceneFreezeRequested(false);
			QuickRenderSettings.Reset();
			MasterRenderSequence.Reset();
			if (Completion)
			{
				Completion(Result);
			}
		}

		TWeakObjectPtr<UWorld> ExportWorld;
		TArray<FUESplattingCaptureView> CaptureViews;
		FUESplattingDatasetExportSettings Settings;
		FUESplattingDatasetExportResult Result;
		FUESplattingDatasetExportCompleted Completion;
		TStrongObjectPtr<UMovieGraphQuickRenderModeSettings> QuickRenderSettings;
		TStrongObjectPtr<UMovieGraphConfig> ActiveMovieGraph;
		TStrongObjectPtr<ULevelSequence> MasterRenderSequence;
		TWeakObjectPtr<ACameraActor> TemporaryCamera;
		TArray<TWeakObjectPtr<AActor>> PreviousSelection;
		TArray<FCaptureStation> CaptureStations;
		TArray<FExportedImage> ExportedImages;
		TArray<FExportedPoint> ExportedPoints;
		TArray<FCaptureVisualizationState> CaptureVisualizationStates;
		TArray<FGraphWarmUpState> GraphWarmUpStates;
		TArray<FGraphCameraState> GraphCameraStates;
		TArray<FGraphSamplingState> GraphSamplingStates;
		TArray<FGraphJpegOutputState> GraphJpegOutputStates;
		TArray<FInjectedGraphNodeState> InjectedGraphNodeStates;
		FString OutputDirectory;
		FString CaptureId;
		FString ImagesDirectory;
		FString MrqStagingDirectory;
		FString SparseDirectory;
		FString TransformsPath;
		FString PointCloudPath;
		FString ManifestPath;
		FString IncompleteMarkerPath;
		int64 NextPointId = 1;
		bool bFinished = false;
		bool bMovieGraphPackageWasDirty = false;
		bool bMovieGraphSettingsApplied = false;
	};

	static TSharedPtr<FDatasetExportJob> ActiveDatasetExportJob;

	static bool StartDatasetExportJob(
		UObject* WorldContextObject,
		const TArray<FUESplattingCaptureView>& CaptureViews,
		const FUESplattingDatasetExportSettings& Settings,
		FUESplattingDatasetExportCompleted Completion,
		FUESplattingDatasetExportResult& StartResult)
	{
		if (ActiveDatasetExportJob.IsValid() || ActiveMovieRenderQueueExportJob.IsValid())
		{
			StartResult = FUESplattingDatasetExportResult();
			StartResult.Message = TEXT("A UESplatting scene capture export is already running.");
			return false;
		}

		if (Settings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue)
		{
			TSharedPtr<FMovieRenderQueueDatasetExportJob> Job = MakeShared<FMovieRenderQueueDatasetExportJob>();
			if (!Job->Initialize(WorldContextObject, CaptureViews, Settings, MoveTemp(Completion), StartResult))
			{
				return false;
			}
			ActiveMovieRenderQueueExportJob = Job;
			if (!Job->Start(StartResult))
			{
				ActiveMovieRenderQueueExportJob.Reset();
				return false;
			}
			return true;
		}

		TSharedPtr<FDatasetExportJob> Job = MakeShared<FDatasetExportJob>();
		if (!Job->Initialize(WorldContextObject, CaptureViews, Settings, MoveTemp(Completion), StartResult))
		{
			return false;
		}

		ActiveDatasetExportJob = Job;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Job](float DeltaTime)
		{
			const bool bContinue = Job->Tick(DeltaTime);
			if (!bContinue && ActiveDatasetExportJob == Job)
			{
				ActiveDatasetExportJob.Reset();
			}
			return bContinue;
		}));
		return true;
	}
}

int32 UUESplattingDatasetExporter::NormalizeCaptureStationIndices(
	TArray<FUESplattingCaptureView>& InOutCaptureViews)
{
	struct FSourceStation
	{
		int32 SourceStationIndex = INDEX_NONE;
		FString CaptureGroupId;
		FVector Anchor = FVector::ZeroVector;
	};

	TArray<FSourceStation> SourceStations;
	for (FUESplattingCaptureView& View : InOutCaptureViews)
	{
		const int32 SourceStationIndex = View.StationIndex;
		int32 MatchingStationIndex = INDEX_NONE;
		for (int32 StationIndex = 0; StationIndex < SourceStations.Num(); ++StationIndex)
		{
			const FSourceStation& Station = SourceStations[StationIndex];
			if (Station.CaptureGroupId != View.CaptureGroupId)
			{
				continue;
			}

			const bool bExplicitMatch = SourceStationIndex >= 0
				&& Station.SourceStationIndex == SourceStationIndex;
			const bool bSpatialFallbackMatch = SourceStationIndex < 0
				&& Station.SourceStationIndex < 0
				&& FVector::DistSquared(View.Transform.GetLocation(), Station.Anchor) < 1.0f;
			if (bExplicitMatch || bSpatialFallbackMatch)
			{
				MatchingStationIndex = StationIndex;
				break;
			}
		}

		if (MatchingStationIndex == INDEX_NONE)
		{
			FSourceStation& Station = SourceStations.AddDefaulted_GetRef();
			Station.SourceStationIndex = SourceStationIndex;
			Station.CaptureGroupId = View.CaptureGroupId;
			Station.Anchor = View.Transform.GetLocation();
			MatchingStationIndex = SourceStations.Num() - 1;
		}

		View.StationIndex = MatchingStationIndex;
	}

	return SourceStations.Num();
}

#if WITH_DEV_AUTOMATION_TESTS
FString UUESplattingDatasetExporter::BuildTransformsJsonForAutomationTests(
	const TArray<FUESplattingCaptureView>& CaptureViews,
	const FUESplattingDatasetExportSettings& Settings,
	bool bHasPointCloud)
{
	using namespace UESplattingDataset;

	TArray<FExportedImage> Images;
	Images.Reserve(CaptureViews.Num());
	for (int32 ViewIndex = 0; ViewIndex < CaptureViews.Num(); ++ViewIndex)
	{
		const FUESplattingCaptureView& View = CaptureViews[ViewIndex];
		FExportedImage& Image = Images.AddDefaulted_GetRef();
		Image.ImageId = ViewIndex + 1;
		Image.CameraId = ViewIndex + 1;
		Image.StationIndex = View.StationIndex;
		Image.CaptureGroupId = View.CaptureGroupId;
		Image.CaptureGroupKind = View.CaptureGroupKind;
		Image.Name = FString::Printf(TEXT("images/frame_%06d.jpg"), ViewIndex + 1);
		Image.CameraToWorld = View.Transform;
		Image.FocalLength = ResolveFocalLengthPixels(View.HorizontalFieldOfView, Settings.ImageWidth);
		Image.HorizontalFieldOfView = View.HorizontalFieldOfView;
	}

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	return FJsonSerializer::Serialize(BuildTransformsJsonObject(Images, Settings, bHasPointCloud), Writer) ? JsonText : FString();
}
#endif

bool UUESplattingDatasetExporter::CalibrateGlobalExposure(
	UObject* WorldContextObject,
	const TArray<FUESplattingCaptureView>& CaptureViews,
	const FUESplattingDatasetExportSettings& InSettings,
	FUESplattingPhotometricCalibrationResult& Result)
{
	using namespace UESplattingDataset;
	Result = FUESplattingPhotometricCalibrationResult();
	if (CaptureViews.IsEmpty())
	{
		Result.Warning = TEXT("No capture views were supplied for photometric calibration.");
		return false;
	}

	UWorld* World = nullptr;
	if (AActor* Actor = Cast<AActor>(WorldContextObject))
	{
		World = Actor->GetWorld();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
	{
		World = Component->GetWorld();
	}
	else if (GEngine && WorldContextObject)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	}
	if (!World)
	{
		Result.Warning = TEXT("Could not resolve an editor world for photometric calibration.");
		return false;
	}

	constexpr int32 CalibrationWidth = 640;
	constexpr int32 CalibrationHeight = 360;
	TStrongObjectPtr<UTextureRenderTarget2D> CalibrationTarget(CreateRenderTarget(GetTransientPackage(), CalibrationWidth, CalibrationHeight));
	if (!CalibrationTarget.IsValid())
	{
		Result.Warning = TEXT("Failed to create the photometric calibration render target.");
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.Name = MakeUniqueObjectName(World, ASceneCapture2D::StaticClass(), TEXT("UESplattingExposureCalibration"));
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(
		ASceneCapture2D::StaticClass(), CaptureViews[0].Transform, SpawnParameters);
	USceneCaptureComponent2D* CaptureComponent = CaptureActor ? CaptureActor->GetCaptureComponent2D() : nullptr;
	if (!CaptureActor || !CaptureComponent)
	{
		if (CaptureActor)
		{
			CaptureActor->Destroy();
		}
		CalibrationTarget->ReleaseResource();
		Result.Warning = TEXT("Failed to create the photometric calibration SceneCapture2D.");
		return false;
	}

	const auto Cleanup = [&]()
	{
		CaptureComponent->TextureTarget = nullptr;
		CaptureActor->Destroy();
		CalibrationTarget->ReleaseResource();
		FlushRenderingCommands();
	};

	FUESplattingDatasetExportSettings Settings = InSettings;
	Settings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::CalibratedLocked;
	Settings.ImageWidth = CalibrationWidth;
	Settings.ImageHeight = CalibrationHeight;
	const int32 DesiredSampleCount = FMath::Clamp(Settings.CalibrationSampleViewCount, 8, 64);
	const int32 SampleCount = FMath::Min(DesiredSampleCount, CaptureViews.Num());
	TArray<int32> SampleIndices;
	SampleIndices.Reserve(SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const int32 ViewIndex = FMath::Clamp(
			FMath::FloorToInt((static_cast<double>(SampleIndex) + 0.5) * static_cast<double>(CaptureViews.Num()) / static_cast<double>(SampleCount)),
			0,
			CaptureViews.Num() - 1);
		SampleIndices.AddUnique(ViewIndex);
	}

	const float InitialCompensation = FMath::Clamp(Settings.ManualExposureCompensation, -10.0f, 10.0f);
	float EffectiveCompensation = InitialCompensation;
	TArray<float> FrameMeanLuminances;
	TArray<float> UnsortedFrameMeanLuminances;
	FString Error;
	constexpr int32 CalibrationIterations = 10;
	for (int32 Iteration = 0; Iteration < CalibrationIterations; ++Iteration)
	{
		Settings.ManualExposureCompensation = EffectiveCompensation;
		FrameMeanLuminances.Reset(SampleIndices.Num());
		for (const int32 ViewIndex : SampleIndices)
		{
			if (!ConfigureCaptureView(CaptureComponent, CaptureViews[ViewIndex], Settings, CalibrationTarget.Get(), Error))
			{
				Cleanup();
				Result.Warning = Error;
				return false;
			}
			const int32 WarmupFrames = FMath::Max(0, Settings.CaptureWarmupFrames);
			for (int32 WarmupIndex = 0; WarmupIndex < WarmupFrames; ++WarmupIndex)
			{
				if (!CaptureSceneFrame(CaptureComponent, WarmupIndex == 0, Error))
				{
					Cleanup();
					Result.Warning = Error;
					return false;
				}
				FlushRenderingCommands();
			}
			if (!CaptureSceneFrame(CaptureComponent, WarmupFrames == 0, Error))
			{
				Cleanup();
				Result.Warning = Error;
				return false;
			}
			FlushRenderingCommands();
			TArray<FColor> Pixels;
			if (!ReadCapturedPixels(CalibrationTarget.Get(), Pixels, Error))
			{
				Cleanup();
				Result.Warning = Error;
				return false;
			}
			FrameMeanLuminances.Add(ComputeFrameMeanLuminance(Pixels, CalibrationWidth, CalibrationHeight));
		}

		UnsortedFrameMeanLuminances = FrameMeanLuminances;
		FrameMeanLuminances.Sort();
		const float Median = GetSortedPercentile(FrameMeanLuminances, 0.5f);
		const float TargetMedian = FMath::Clamp(Settings.CalibrationTargetMedianLuminance, 32.0f, 192.0f);
		const float DarkTolerance = FMath::Clamp(Settings.CalibrationDarkTolerance, 2.0f, 32.0f);
		const float BrightTolerance = FMath::Clamp(Settings.CalibrationBrightTolerance, 2.0f, 32.0f);
		const bool bInsideHealthyBand = Median >= TargetMedian - DarkTolerance && Median <= TargetMedian + BrightTolerance;
		const float Adjustment = FMath::Clamp(FMath::Log2(TargetMedian / FMath::Max(Median, 1.0f)), -3.0f, 3.0f);
		UE_LOG(
			LogUESplatting,
			Display,
			TEXT("UESplatting exposure calibration iteration %d: compensation=%+.3f, luma_p10=%.1f, median=%.1f, p90=%.1f, proposed_adjustment=%+.3f"),
			Iteration + 1,
			EffectiveCompensation,
			GetSortedPercentile(FrameMeanLuminances, 0.1f),
			Median,
			GetSortedPercentile(FrameMeanLuminances, 0.9f),
			Adjustment);
		if (Iteration + 1 >= CalibrationIterations
			|| bInsideHealthyBand
			|| FMath::Abs(Adjustment) <= 0.08f)
		{
			break;
		}
		EffectiveCompensation = FMath::Clamp(EffectiveCompensation + Adjustment, -10.0f, 10.0f);
	}

	Result.bSuccess = !FrameMeanLuminances.IsEmpty();
	Result.EffectiveExposureCompensation = EffectiveCompensation;
	Result.LuminanceP10 = GetSortedPercentile(FrameMeanLuminances, 0.1f);
	Result.LuminanceMedian = GetSortedPercentile(FrameMeanLuminances, 0.5f);
	Result.LuminanceP90 = GetSortedPercentile(FrameMeanLuminances, 0.9f);
	Result.SampleViewCount = FrameMeanLuminances.Num();

	if (Result.bSuccess && UnsortedFrameMeanLuminances.Num() == SampleIndices.Num())
	{
		int32 RepresentativeSampleIndex = 0;
		float BestDifference = FLT_MAX;
		for (int32 SampleIndex = 0; SampleIndex < UnsortedFrameMeanLuminances.Num(); ++SampleIndex)
		{
			const float Difference = FMath::Abs(UnsortedFrameMeanLuminances[SampleIndex] - Result.LuminanceMedian);
			if (Difference < BestDifference)
			{
				BestDifference = Difference;
				RepresentativeSampleIndex = SampleIndex;
			}
		}

		Settings.ManualExposureCompensation = EffectiveCompensation;
		const int32 RepresentativeViewIndex = SampleIndices[RepresentativeSampleIndex];
		bool bPreviewCaptured = ConfigureCaptureView(CaptureComponent, CaptureViews[RepresentativeViewIndex], Settings, CalibrationTarget.Get(), Error);
		const int32 PreviewWarmupFrames = FMath::Max(0, Settings.CaptureWarmupFrames);
		for (int32 WarmupIndex = 0; bPreviewCaptured && WarmupIndex < PreviewWarmupFrames; ++WarmupIndex)
		{
			bPreviewCaptured = CaptureSceneFrame(CaptureComponent, WarmupIndex == 0, Error);
			if (bPreviewCaptured)
			{
				FlushRenderingCommands();
			}
		}
		bPreviewCaptured = bPreviewCaptured && CaptureSceneFrame(CaptureComponent, PreviewWarmupFrames == 0, Error);
		if (bPreviewCaptured)
		{
			FlushRenderingCommands();
		}
		if (bPreviewCaptured)
		{
			const FString PreviewDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UESplatting"), TEXT("ExposurePreviews"));
			IFileManager::Get().MakeDirectory(*PreviewDirectory, true);
			const FString ContextName = WorldContextObject ? FPaths::MakeValidFileName(WorldContextObject->GetName()) : TEXT("Capture");
			const FString PreviewPath = FPaths::Combine(PreviewDirectory, ContextName + TEXT("_calibrated.jpg"));
			FUESplattingDatasetExportSettings PreviewSettings = Settings;
			PreviewSettings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
			PreviewSettings.JpegQuality = 94;
			TArray<FColor> PreviewPixels;
			if (SaveCapturedImage(CalibrationTarget.Get(), PreviewSettings, PreviewPath, PreviewPixels, Error))
			{
				Result.PreviewImagePath = FPaths::ConvertRelativePathToFull(PreviewPath);
			}
		}
	}

	Cleanup();
	if (Result.LuminanceMedian < 60.0f || Result.LuminanceP90 < 80.0f)
	{
		Result.Warning = TEXT("Calibration remains dark. Raise the luminance target or inspect scene lighting before export.");
	}
	else if (Result.LuminanceMedian > 160.0f || Result.LuminanceP90 > 235.0f)
	{
		Result.Warning = TEXT("Calibration risks highlight clipping. Lower the luminance target before export.");
	}
	return Result.bSuccess;
}

bool UUESplattingDatasetExporter::ResolveActiveViewportExposure(
	UObject* WorldContextObject,
	FUESplattingDatasetExportSettings& InOutSettings,
	FUESplattingCaptureView& OutViewportView,
	FString& OutError)
{
	UWorld* World = nullptr;
	if (AActor* Actor = Cast<AActor>(WorldContextObject))
	{
		World = Actor->GetWorld();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
	{
		World = Component->GetWorld();
	}
	else if (GEngine && WorldContextObject)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	}

	if (!GEditor || !World)
	{
		OutError = TEXT("Could not resolve the active editor world for viewport-matched exposure.");
		return false;
	}

	FViewport* ActiveViewport = GEditor->GetActiveViewport();
	FLevelEditorViewportClient* ActiveViewportClient = nullptr;
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient && ViewportClient->Viewport == ActiveViewport && ViewportClient->GetWorld() == World)
		{
			ActiveViewportClient = ViewportClient;
			break;
		}
	}

	if (!ActiveViewportClient || ActiveViewportClient->ViewportType != LVT_Perspective)
	{
		OutError = TEXT("Activate a perspective level viewport showing the intended authored look, then try again.");
		return false;
	}
	if (!ActiveViewportClient->EngineShowFlags.PostProcessing)
	{
		OutError = TEXT("The active viewport has post processing disabled. Enable it before matching the authored look.");
		return false;
	}

	static const auto ExtendRangeCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
	static const auto LensAttenuationCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.EyeAdaptation.LensAttenuation"));
	const bool bExtendedRange = ExtendRangeCVar && ExtendRangeCVar->GetValueOnGameThread() != 0;
	const float LensAttenuation = LensAttenuationCVar ? LensAttenuationCVar->GetValueOnGameThread() : 0.78f;
	const float LuminanceMax = bExtendedRange ? 0.78f / FMath::Max(LensAttenuation, 0.01f) : 1.0f;

	float ExposureScale = 0.0f;
	FString ExposureSource;
	if (ActiveViewportClient->ExposureSettings.bFixed)
	{
		ExposureScale = 1.0f / (LuminanceMax * FMath::Exp2(ActiveViewportClient->ExposureSettings.FixedEV100));
		ExposureSource = TEXT("active-editor-viewport-fixed-ev100");
	}
	else
	{
		FSceneViewStateInterface* ViewState = ActiveViewportClient->ViewState.GetReference();
		if (!ViewState || !ViewState->HasValidEyeAdaptationBuffer())
		{
			OutError = TEXT("The active viewport has no valid exposure history yet. Leave it on the intended view for a moment, then try again.");
			return false;
		}
		ExposureScale = ViewState->GetLastEyeAdaptationExposure();
		ExposureSource = TEXT("active-editor-viewport-eye-adaptation-state");
	}

	if (!FMath::IsFinite(ExposureScale) || ExposureScale <= 0.0f)
	{
		OutError = FString::Printf(TEXT("The active viewport returned an invalid exposure scale (%g)."), ExposureScale);
		return false;
	}

	InOutSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
	InOutSettings.bViewportExposureMatched = true;
	InOutSettings.ViewportExposureScale = ExposureScale;
	InOutSettings.ViewportExposureSource = ExposureSource;
	InOutSettings.ManualExposureCompensation = FMath::Clamp(FMath::Log2(ExposureScale * LuminanceMax), -15.0f, 15.0f);
	InOutSettings.bUseEyeAdaptation = true;

	OutViewportView = FUESplattingCaptureView();
	OutViewportView.Transform = FTransform(ActiveViewportClient->GetViewRotation(), ActiveViewportClient->GetViewLocation());
	OutViewportView.HorizontalFieldOfView = FMath::Clamp(ActiveViewportClient->ViewFOV, 1.0f, 179.0f);
	OutViewportView.StationIndex = 0;
	OutViewportView.DebugName = TEXT("ActiveEditorViewportMatch");
	return true;
}

bool UUESplattingDatasetExporter::ResolveActiveViewportView(
	UObject* WorldContextObject,
	FUESplattingCaptureView& OutViewportView,
	FString& OutError)
{
	UWorld* World = nullptr;
	if (AActor* Actor = Cast<AActor>(WorldContextObject))
	{
		World = Actor->GetWorld();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
	{
		World = Component->GetWorld();
	}
	else if (GEngine && WorldContextObject)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	}

	if (!GEditor || !World)
	{
		OutError = TEXT("Could not resolve the active editor world for the MRQ preview.");
		return false;
	}

	FViewport* ActiveViewport = GEditor->GetActiveViewport();
	FLevelEditorViewportClient* ActiveViewportClient = nullptr;
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient && ViewportClient->Viewport == ActiveViewport && ViewportClient->GetWorld() == World)
		{
			ActiveViewportClient = ViewportClient;
			break;
		}
	}

	if (!ActiveViewportClient || ActiveViewportClient->ViewportType != LVT_Perspective)
	{
		OutError = TEXT("Activate a perspective level viewport showing the intended authored look, then try again.");
		return false;
	}
	if (!ActiveViewportClient->EngineShowFlags.PostProcessing)
	{
		OutError = TEXT("The active viewport has post processing disabled. Enable it before rendering the MRQ test frame.");
		return false;
	}

	OutViewportView = FUESplattingCaptureView();
	OutViewportView.Transform = FTransform(ActiveViewportClient->GetViewRotation(), ActiveViewportClient->GetViewLocation());
	OutViewportView.HorizontalFieldOfView = FMath::Clamp(ActiveViewportClient->ViewFOV, 1.0f, 179.0f);
	OutViewportView.StationIndex = 0;
	OutViewportView.DebugName = TEXT("ActiveEditorViewportMRQTest");
	return true;
}

bool UUESplattingDatasetExporter::CaptureViewportMatchedPreview(
	UObject* WorldContextObject,
	const FUESplattingCaptureView& ViewportView,
	const FUESplattingDatasetExportSettings& InSettings,
	FUESplattingPhotometricCalibrationResult& Result)
{
	using namespace UESplattingDataset;
	Result = FUESplattingPhotometricCalibrationResult();

	UWorld* World = nullptr;
	if (AActor* Actor = Cast<AActor>(WorldContextObject))
	{
		World = Actor->GetWorld();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
	{
		World = Component->GetWorld();
	}
	else if (GEngine && WorldContextObject)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	}
	if (!World)
	{
		Result.Warning = TEXT("Could not resolve an editor world for the viewport-match preview.");
		return false;
	}

	FUESplattingDatasetExportSettings Settings = InSettings;
	const int32 PreviewWidth = FMath::Clamp(Settings.ImageWidth, 640, 1920);
	const int32 PreviewHeight = FMath::Clamp(Settings.ImageHeight, 360, 1080);
	Settings.ImageWidth = PreviewWidth;
	Settings.ImageHeight = PreviewHeight;
	Settings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
	Settings.JpegQuality = 94;

	TStrongObjectPtr<UTextureRenderTarget2D> PreviewTarget(CreateRenderTarget(GetTransientPackage(), PreviewWidth, PreviewHeight));
	if (!PreviewTarget.IsValid())
	{
		Result.Warning = TEXT("Failed to create the viewport-match preview render target.");
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.Name = MakeUniqueObjectName(World, ASceneCapture2D::StaticClass(), TEXT("UESplattingViewportMatchPreview"));
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), ViewportView.Transform, SpawnParameters);
	USceneCaptureComponent2D* CaptureComponent = CaptureActor ? CaptureActor->GetCaptureComponent2D() : nullptr;
	if (!CaptureActor || !CaptureComponent)
	{
		if (CaptureActor)
		{
			CaptureActor->Destroy();
		}
		PreviewTarget->ReleaseResource();
		Result.Warning = TEXT("Failed to create the viewport-match preview SceneCapture2D.");
		return false;
	}

	const auto Cleanup = [&]()
	{
		CaptureComponent->TextureTarget = nullptr;
		CaptureActor->Destroy();
		PreviewTarget->ReleaseResource();
		FlushRenderingCommands();
	};

	FString Error;
	if (!ConfigureCaptureView(CaptureComponent, ViewportView, Settings, PreviewTarget.Get(), Error))
	{
		Cleanup();
		Result.Warning = Error;
		return false;
	}
	const int32 WarmupFrames = FMath::Max(0, Settings.CaptureWarmupFrames);
	for (int32 WarmupIndex = 0; WarmupIndex < WarmupFrames; ++WarmupIndex)
	{
		if (!CaptureSceneFrame(CaptureComponent, WarmupIndex == 0, Error))
		{
			Cleanup();
			Result.Warning = Error;
			return false;
		}
		FlushRenderingCommands();
	}
	if (!CaptureSceneFrame(CaptureComponent, WarmupFrames == 0, Error))
	{
		Cleanup();
		Result.Warning = Error;
		return false;
	}
	FlushRenderingCommands();

	const FString PreviewDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UESplatting"), TEXT("ExposurePreviews"));
	IFileManager::Get().MakeDirectory(*PreviewDirectory, true);
	const FString ContextName = WorldContextObject ? FPaths::MakeValidFileName(WorldContextObject->GetName()) : TEXT("Capture");
	const FString PreviewPath = FPaths::Combine(PreviewDirectory, ContextName + TEXT("_viewport_match.jpg"));
	TArray<FColor> PreviewPixels;
	if (!SaveCapturedImage(PreviewTarget.Get(), Settings, PreviewPath, PreviewPixels, Error))
	{
		Cleanup();
		Result.Warning = Error;
		return false;
	}

	const float MeanLuminance = ComputeFrameMeanLuminance(PreviewPixels, PreviewWidth, PreviewHeight);
	Result.bSuccess = true;
	Result.EffectiveExposureCompensation = Settings.ManualExposureCompensation;
	Result.LuminanceP10 = MeanLuminance;
	Result.LuminanceMedian = MeanLuminance;
	Result.LuminanceP90 = MeanLuminance;
	Result.SampleViewCount = 1;
	Result.PreviewImagePath = FPaths::ConvertRelativePathToFull(PreviewPath);
	Cleanup();
	return true;
}

bool UUESplattingDatasetExporter::ConfirmLargeCaptureIfNeeded(int32 ImageCount)
{
	if (ImageCount <= LargeCaptureWarningImageCount)
	{
		return true;
	}

	const FText Message = FText::FromString(FString::Printf(
		TEXT("This capture will export %d images. That is larger than the normal room-scale training unit and may take substantial time and storage.\n\nConsider increasing probe spacing or splitting the scene into overlapping blocks.\n\nContinue?"),
		ImageCount));
	return FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes;
}

bool UUESplattingDatasetExporter::ExportColmapDatasetFromCaptureViews(
	UObject* WorldContextObject,
	const TArray<FUESplattingCaptureView>& CaptureViews,
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportResult& Result)
{
	return StartColmapDatasetExportFromCaptureViews(
		WorldContextObject,
		CaptureViews,
		Settings,
		FUESplattingDatasetExportCompleted(),
		Result);
}

bool UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
	UObject* WorldContextObject,
	const TArray<FUESplattingCaptureView>& CaptureViews,
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportCompleted Completion,
	FUESplattingDatasetExportResult& StartResult)
{
	return UESplattingDataset::StartDatasetExportJob(
		WorldContextObject,
		CaptureViews,
		Settings,
		MoveTemp(Completion),
		StartResult);
}

bool UUESplattingDatasetExporter::ExportColmapDatasetFromCameraActors(
	const TArray<AActor*>& CameraActors,
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportResult& Result)
{
	return StartColmapDatasetExportFromCameraActors(
		CameraActors,
		Settings,
		FUESplattingDatasetExportCompleted(),
		Result);
}

bool UUESplattingDatasetExporter::StartColmapDatasetExportFromCameraActors(
	const TArray<AActor*>& CameraActors,
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportCompleted Completion,
	FUESplattingDatasetExportResult& StartResult)
{
	using namespace UESplattingDataset;

	if (CameraActors.IsEmpty())
	{
		StartResult = FUESplattingDatasetExportResult();
		StartResult.Message = TEXT("No camera actors were supplied.");
		return false;
	}

	UObject* WorldContextObject = nullptr;
	TArray<FUESplattingCaptureView> CaptureViews;
	FUESplattingDatasetExportSettings EffectiveSettings = Settings;
	for (AActor* Actor : CameraActors)
	{
		UCameraComponent* CameraComponent = ResolveCameraComponent(Actor);
		if (!CameraComponent)
		{
			continue;
		}

		if (CameraComponent->ProjectionMode != ECameraProjectionMode::Perspective)
		{
			UE_LOG(LogUESplatting, Warning, TEXT("UESplatting dataset export skipped non-perspective camera '%s'."), *Actor->GetName());
			continue;
		}

		if (!WorldContextObject)
		{
			WorldContextObject = Actor;
		}

		FUESplattingCaptureView& CaptureView = CaptureViews.AddDefaulted_GetRef();
		CaptureView.Transform = CameraComponent->GetComponentTransform();
		CaptureView.HorizontalFieldOfView = CameraComponent->FieldOfView;
		CaptureView.PostProcessSettings = CameraComponent->PostProcessSettings;
		CaptureView.PostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
		CaptureView.DebugName = Actor->GetName();
	}

	if (EffectiveSettings.CapturePatternNotes.TrimStartAndEnd().IsEmpty())
	{
		EffectiveSettings.CapturePatternNotes = FString::Printf(TEXT("Selected Unreal camera actors: cameras=%d; exact per-frame component transforms recorded in transforms.json."), CaptureViews.Num());
	}

	return StartColmapDatasetExportFromCaptureViews(
		WorldContextObject,
		CaptureViews,
		EffectiveSettings,
		MoveTemp(Completion),
		StartResult);
}

bool UUESplattingDatasetExporter::ExportColmapDatasetFromSelectedCameras(
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportResult& Result)
{
	return StartColmapDatasetExportFromSelectedCameras(
		Settings,
		FUESplattingDatasetExportCompleted(),
		Result);
}

bool UUESplattingDatasetExporter::StartColmapDatasetExportFromSelectedCameras(
	const FUESplattingDatasetExportSettings& Settings,
	FUESplattingDatasetExportCompleted Completion,
	FUESplattingDatasetExportResult& StartResult)
{
	TArray<AActor*> CameraActors;

	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				if (Actor->FindComponentByClass<UCameraComponent>())
				{
					CameraActors.Add(Actor);
				}
			}
		}
	}

	return StartColmapDatasetExportFromCameraActors(CameraActors, Settings, MoveTemp(Completion), StartResult);
}
