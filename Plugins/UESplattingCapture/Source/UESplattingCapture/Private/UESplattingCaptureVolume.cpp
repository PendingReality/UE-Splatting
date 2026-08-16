// SPDX-License-Identifier: MIT

#include "UESplattingCaptureVolume.h"

#include "Camera/CameraComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "MeshElementCollector.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"

namespace
{
	static bool ResolveReferencePostProcess(
		AActor* ReferenceActor,
		FPostProcessSettings& OutSettings,
		float& OutBlendWeight,
		bool& bOutNeedsPerViewOverride)
	{
		bOutNeedsPerViewOverride = false;
		if (!ReferenceActor)
		{
			return false;
		}

		if (UCameraComponent* CameraComponent = ReferenceActor->FindComponentByClass<UCameraComponent>())
		{
			FMinimalViewInfo CameraView;
			CameraComponent->GetCameraView(0.0f, CameraView);
			OutSettings = CameraView.PostProcessSettings;
			OutBlendWeight = CameraView.PostProcessBlendWeight;
			bOutNeedsPerViewOverride = true;
			return true;
		}

		if (APostProcessVolume* PostProcessVolume = Cast<APostProcessVolume>(ReferenceActor))
		{
			if (!PostProcessVolume->bEnabled || PostProcessVolume->IsHiddenEd() || PostProcessVolume->BlendWeight <= 0.0f)
			{
				return false;
			}
			OutSettings = PostProcessVolume->Settings;
			OutBlendWeight = PostProcessVolume->BlendWeight;
			// An enabled unbound volume in this world is already blended by
			// FSceneView::StartFinalPostprocessSettings at every generated pose.
			bOutNeedsPerViewOverride = !PostProcessVolume->bUnbound;
			return true;
		}

		return false;
	}
}

namespace UESplattingCaptureVolumePreview
{
	struct FPreviewStation
	{
		FVector Location = FVector::ZeroVector;
		TArray<const FUESplattingCaptureView*> Views;
	};

	static TArray<FPreviewStation> BuildStations(const TArray<FUESplattingCaptureView>& Views)
	{
		TArray<FPreviewStation> Stations;
		Stations.Reserve(Views.Num());

		for (const FUESplattingCaptureView& CaptureView : Views)
		{
			const FVector Location = CaptureView.Transform.GetLocation();
			FPreviewStation* ExistingStation = nullptr;
			for (FPreviewStation& Station : Stations)
			{
				if (FVector::DistSquared(Station.Location, Location) < 1.0f)
				{
					ExistingStation = &Station;
					break;
				}
			}

			if (!ExistingStation)
			{
				ExistingStation = &Stations.AddDefaulted_GetRef();
				ExistingStation->Location = Location;
			}

			ExistingStation->Views.Add(&CaptureView);
		}

		return Stations;
	}

	class FUESplattingCapturePreviewSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FUESplattingCapturePreviewSceneProxy(const UUESplattingCapturePreviewComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
		{
			const AUESplattingCaptureVolume* CaptureVolume = Cast<AUESplattingCaptureVolume>(InComponent->GetOwner());
			if (!CaptureVolume || !CaptureVolume->CaptureBounds || !CaptureVolume->bShowProbePreview)
			{
				return;
			}

			ProbeRadius = FMath::Max(1.0f, CaptureVolume->PreviewProbeRadius);
			DirectionLength = FMath::Max(1.0f, CaptureVolume->PreviewDirectionLength);
			PreviewMode = CaptureVolume->PreviewMode;
			PreviewDirectionLimit = CaptureVolume->PreviewDirectionLimit;
			BoundsTransform = CaptureVolume->CaptureBounds->GetComponentTransform();
			BoundsExtent = CaptureVolume->CaptureBounds->GetUnscaledBoxExtent();
			if (CaptureVolume->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
			{
				BoundsColor = FLinearColor(1.0f, 0.55f, 0.08f, 0.95f);
				ProbeColor = FLinearColor(1.0f, 0.78f, 0.15f, 1.0f);
				DirectionColor = FLinearColor(1.0f, 0.62f, 0.18f, 0.8f);
				DirectionTipColor = FLinearColor(1.0f, 0.92f, 0.55f, 1.0f);
			}

			CaptureViews = CaptureVolume->GetCachedPreviewCaptureViews();
			Stations = BuildStations(CaptureViews);
			bHasPreview = true;
			bWillEverBeLit = false;
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			if (!bHasPreview)
			{
				return;
			}

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1 << ViewIndex)))
				{
					continue;
				}

				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				DrawVolumeBox(PDI);

				for (const FPreviewStation& Station : Stations)
				{
					DrawWireSphere(PDI, Station.Location, ProbeColor, ProbeRadius, 12, SDPG_Foreground, 1.25f);
					PDI->DrawPoint(Station.Location, ProbeColor, ProbeRadius * 1.25f, SDPG_Foreground);
				}

				DrawDirections(PDI);
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			const bool bEditorGameView = View->Family->EngineShowFlags.Game && !View->bIsSceneCapture;
			Result.bDrawRelevance = bHasPreview && !View->bIsSceneCapture && (bEditorGameView || IsShown(View));
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override
		{
			return sizeof(*this) + GetAllocatedSize();
		}

		uint32 GetAllocatedSize() const
		{
			return FPrimitiveSceneProxy::GetAllocatedSize() + CaptureViews.GetAllocatedSize() + Stations.GetAllocatedSize();
		}

	private:
		void DrawLine(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness = 0.0f) const
		{
			PDI->DrawLine(Start, End, Color, SDPG_World, Thickness);
		}

		void DrawVolumeBox(FPrimitiveDrawInterface* PDI) const
		{
			FVector Corners[8];
			int32 CornerIndex = 0;
			for (int32 X = -1; X <= 1; X += 2)
			{
				for (int32 Y = -1; Y <= 1; Y += 2)
				{
					for (int32 Z = -1; Z <= 1; Z += 2)
					{
						Corners[CornerIndex++] = BoundsTransform.TransformPosition(FVector(BoundsExtent.X * X, BoundsExtent.Y * Y, BoundsExtent.Z * Z));
					}
				}
			}

			const auto DrawEdge = [&](int32 A, int32 B)
			{
				DrawLine(PDI, Corners[A], Corners[B], BoundsColor, 2.0f);
			};

			DrawEdge(0, 1);
			DrawEdge(0, 2);
			DrawEdge(0, 4);
			DrawEdge(3, 1);
			DrawEdge(3, 2);
			DrawEdge(3, 7);
			DrawEdge(5, 1);
			DrawEdge(5, 4);
			DrawEdge(5, 7);
			DrawEdge(6, 2);
			DrawEdge(6, 4);
			DrawEdge(6, 7);
		}

		void DrawDirectionGlyph(FPrimitiveDrawInterface* PDI, const FTransform& ViewTransform) const
		{
			const FVector Start = ViewTransform.GetLocation();
			const FVector Forward = ViewTransform.GetUnitAxis(EAxis::X);
			const FVector Right = ViewTransform.GetUnitAxis(EAxis::Y);
			const FVector Up = ViewTransform.GetUnitAxis(EAxis::Z);
			const FVector End = Start + Forward * DirectionLength;
			const float TipSize = FMath::Max(4.0f, DirectionLength * 0.055f);

			DrawLine(PDI, Start, End, DirectionColor, 1.0f);
			DrawLine(PDI, End - Right * TipSize, End + Right * TipSize, DirectionTipColor, 0.75f);
			DrawLine(PDI, End - Up * TipSize, End + Up * TipSize, DirectionTipColor, 0.75f);
		}

		void DrawDirections(FPrimitiveDrawInterface* PDI) const
		{
			if (PreviewMode == EUESplattingCaptureVolumePreviewMode::StationsOnly || PreviewDirectionLimit <= 0 || Stations.IsEmpty())
			{
				return;
			}

			const int32 DirectionBudget = PreviewMode == EUESplattingCaptureVolumePreviewMode::AllDirections
				? CaptureViews.Num()
				: FMath::Min(PreviewDirectionLimit, CaptureViews.Num());
			int32 MaxViewsPerStation = 1;
			for (const FPreviewStation& Station : Stations)
			{
				MaxViewsPerStation = FMath::Max(MaxViewsPerStation, Station.Views.Num());
			}

			const int32 StationDrawCount = PreviewMode == EUESplattingCaptureVolumePreviewMode::AllDirections
				? Stations.Num()
				: FMath::Clamp(FMath::CeilToInt(static_cast<float>(DirectionBudget) / static_cast<float>(MaxViewsPerStation)), 1, Stations.Num());
			int32 DrawnDirections = 0;
			for (int32 DrawIndex = 0; DrawIndex < StationDrawCount && DrawnDirections < DirectionBudget; ++DrawIndex)
			{
				const int32 StationIndex = FMath::Clamp(
					FMath::FloorToInt(static_cast<double>(DrawIndex) * static_cast<double>(Stations.Num()) / static_cast<double>(StationDrawCount)),
					0,
					Stations.Num() - 1);
				for (const FUESplattingCaptureView* CaptureView : Stations[StationIndex].Views)
				{
					if (!CaptureView || DrawnDirections >= DirectionBudget)
					{
						break;
					}
					DrawDirectionGlyph(PDI, CaptureView->Transform);
					++DrawnDirections;
				}
			}
		}

		bool bHasPreview = false;
		float ProbeRadius = 8.0f;
		float DirectionLength = 120.0f;
		int32 PreviewDirectionLimit = 96;
		EUESplattingCaptureVolumePreviewMode PreviewMode = EUESplattingCaptureVolumePreviewMode::SampleDirections;
		FTransform BoundsTransform = FTransform::Identity;
		FVector BoundsExtent = FVector::ZeroVector;
		TArray<FUESplattingCaptureView> CaptureViews;
		TArray<FPreviewStation> Stations;
		FLinearColor BoundsColor = FLinearColor(0.0f, 0.72f, 1.0f, 0.9f);
		FLinearColor ProbeColor = FLinearColor(0.0f, 0.95f, 1.0f, 1.0f);
		FLinearColor DirectionColor = FLinearColor(0.35f, 0.72f, 1.0f, 0.75f);
		FLinearColor DirectionTipColor = FLinearColor(0.78f, 0.95f, 1.0f, 0.95f);
	};
}

namespace
{
	enum class EUESplattingCoverageSurfaceType : uint8
	{
		Floor,
		VerticalOrDetail,
		Ceiling
	};

	struct FUESplattingCoveragePatchKey
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		EUESplattingCoverageSurfaceType SurfaceType = EUESplattingCoverageSurfaceType::VerticalOrDetail;

		bool operator==(const FUESplattingCoveragePatchKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z && SurfaceType == Other.SurfaceType;
		}

		friend uint32 GetTypeHash(const FUESplattingCoveragePatchKey& Key)
		{
			uint32 Hash = HashCombine(GetTypeHash(Key.X), GetTypeHash(Key.Y));
			Hash = HashCombine(Hash, GetTypeHash(Key.Z));
			return HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.SurfaceType)));
		}
	};

	struct FUESplattingCoverageCandidate
	{
		FVector LocalLocation = FVector::ZeroVector;
		FVector WorldLocation = FVector::ZeroVector;
		int32 BandIndex = 0;
		TSet<FUESplattingCoveragePatchKey> VisiblePatches;
		TSet<FUESplattingCoveragePatchKey> CloseDetailPatches;
	};

	struct FUESplattingDetailCandidate
	{
		FVector WorldLocation = FVector::ZeroVector;
		int32 ElevationBand = 0;
		int32 DistanceRing = 0;
		TMap<FIntVector, FVector> VisiblePatchTargets;
	};

	struct FUESplattingCoverageTraceDirection
	{
		FVector Direction = FVector::ForwardVector;
		int32 ViewIndex = 0;
	};

	struct FUESplattingCoverageObservers
	{
		TArray<FVector> WorldLocations;
	};

	static FString GetCaptureProfileName(EUESplattingCaptureProfile Profile)
	{
		const UEnum* Enum = StaticEnum<EUESplattingCaptureProfile>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Profile)) : TEXT("Unknown");
	}

	static FString GetDetailCaptureProfileName(EUESplattingDetailCaptureProfile Profile)
	{
		const UEnum* Enum = StaticEnum<EUESplattingDetailCaptureProfile>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Profile)) : TEXT("Unknown");
	}

	static FString GetRoomCoverageViewSetName(EUESplattingRoomCoverageViewSet ViewSet)
	{
		const UEnum* Enum = StaticEnum<EUESplattingRoomCoverageViewSet>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(ViewSet)) : TEXT("Unknown");
	}

	static TArray<FRotator> BuildRoomCoverageViewRotations(
		EUESplattingRoomCoverageViewSet ViewSet,
		float YawPhaseDegrees = 0.0f)
	{
		TArray<FRotator> Rotations;
		const auto AddView = [&Rotations, YawPhaseDegrees](float Pitch, float Yaw)
		{
			Rotations.Add(FRotator(Pitch, Yaw + YawPhaseDegrees, 0.0f));
		};
		const auto AddYawRing = [&AddView](float Pitch, int32 Count, float YawOffset = 0.0f)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				AddView(Pitch, YawOffset + 360.0f * static_cast<float>(Index) / static_cast<float>(Count));
			}
		};

		switch (ViewSet)
		{
		case EUESplattingRoomCoverageViewSet::Minimum6:
			AddYawRing(0.0f, 4);
			AddView(90.0f, 0.0f);
			AddView(-90.0f, 0.0f);
			break;
		case EUESplattingRoomCoverageViewSet::Quality14:
			AddYawRing(0.0f, 6);
			{
				const float DiagonalYaws[] = {45.0f, 135.0f, 225.0f, 315.0f};
				for (const float Yaw : DiagonalYaws)
				{
					AddView(60.0f, Yaw);
				}
				for (const float Yaw : DiagonalYaws)
				{
					AddView(-60.0f, Yaw);
				}
			}
			break;
		case EUESplattingRoomCoverageViewSet::Standard8:
		default:
			AddYawRing(0.0f, 4);
			AddView(60.0f, 45.0f);
			AddView(60.0f, 225.0f);
			AddView(-60.0f, 135.0f);
			AddView(-60.0f, 315.0f);
			break;
		}

		return Rotations;
	}

	static float GetRoomCoverageYawPhaseDegrees(int32 BandIndex, int32 HeightBandCount)
	{
		if (HeightBandCount <= 1)
		{
			return 0.0f;
		}

		// Standard8 cannot cover the full sphere from one 90-degree 16:9 camera
		// center. Spread its blind solid angle across the vertical bands instead of
		// repeating the same missing world directions at every translated station.
		return 90.0f * static_cast<float>(BandIndex) / static_cast<float>(HeightBandCount);
	}

	static TArray<FUESplattingCoverageTraceDirection> BuildCoverageTraceDirections(
		const TArray<FRotator>& ViewRotations,
		float HorizontalFieldOfViewDegrees,
		double AspectRatio)
	{
		TArray<FUESplattingCoverageTraceDirection> Directions;
		Directions.Reserve(ViewRotations.Num() * 9);
		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(HorizontalFieldOfViewDegrees), 1.0, 179.0)) * 0.5);
		const double TanHalfVertical = TanHalfHorizontal / FMath::Max(AspectRatio, 0.01);
		const double SampleCoordinates[] = {-0.7, 0.0, 0.7};

		for (int32 ViewIndex = 0; ViewIndex < ViewRotations.Num(); ++ViewIndex)
		{
			const FRotator& ViewRotation = ViewRotations[ViewIndex];
			const FQuat ViewQuaternion = ViewRotation.Quaternion();
			for (const double NormalizedY : SampleCoordinates)
			{
				for (const double NormalizedX : SampleCoordinates)
				{
					const FVector CameraDirection(1.0, NormalizedX * TanHalfHorizontal, NormalizedY * TanHalfVertical);
					FUESplattingCoverageTraceDirection& TraceDirection = Directions.AddDefaulted_GetRef();
					TraceDirection.Direction = ViewQuaternion.RotateVector(CameraDirection.GetSafeNormal());
					TraceDirection.ViewIndex = ViewIndex;
				}
			}
		}
		return Directions;
	}

	static bool IsBaselineSeparated(
		const FVector& CandidateLocation,
		const FUESplattingCoverageObservers* ExistingObservers,
		float MinimumBaselineCentimeters)
	{
		if (!ExistingObservers || ExistingObservers->WorldLocations.IsEmpty())
		{
			return true;
		}

		const float MinimumBaselineSquared = FMath::Square(MinimumBaselineCentimeters);
		for (const FVector& ObserverLocation : ExistingObservers->WorldLocations)
		{
			if (FVector::DistSquared(CandidateLocation, ObserverLocation) < MinimumBaselineSquared)
			{
				return false;
			}
		}
		return true;
	}

	static int32 GetRequiredObservations(
		const FUESplattingCoveragePatchKey& Patch,
		int32 SurfaceRequiredObservations,
		int32 FloorRequiredObservations)
	{
		return Patch.SurfaceType == EUESplattingCoverageSurfaceType::Floor
			? FloorRequiredObservations
			: SurfaceRequiredObservations;
	}

	static double GetCoverageWeight(EUESplattingCoverageSurfaceType SurfaceType)
	{
		switch (SurfaceType)
		{
		case EUESplattingCoverageSurfaceType::Floor:
			return 8.0;
		case EUESplattingCoverageSurfaceType::Ceiling:
			return 4.0;
		case EUESplattingCoverageSurfaceType::VerticalOrDetail:
		default:
			return 5.0;
		}
	}

	static int32 GetDirectionalArrayAxisCount(float DimensionCm, float SpacingCm)
	{
		return FMath::Max(1, FMath::CeilToInt(DimensionCm / FMath::Max(SpacingCm, 1.0f)) + 1);
	}

}

UUESplattingCapturePreviewComponent::UUESplattingCapturePreviewComponent()
{
	// The explicit preview toggle should also work in the editor's Game View.
	bHiddenInGame = false;
	bHiddenInSceneCapture = true;
	bUseEditorCompositing = true;
	SetIsVisualizationComponent(true);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
}

FPrimitiveSceneProxy* UUESplattingCapturePreviewComponent::CreateSceneProxy()
{
	return new UESplattingCaptureVolumePreview::FUESplattingCapturePreviewSceneProxy(this);
}

FBoxSphereBounds UUESplattingCapturePreviewComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const AUESplattingCaptureVolume* CaptureVolume = Cast<AUESplattingCaptureVolume>(GetOwner());
	if (CaptureVolume && CaptureVolume->CaptureBounds)
	{
		FBox PreviewBounds = CaptureVolume->CaptureBounds->Bounds.GetBox();
		const float DirectionLength = FMath::Max(CaptureVolume->PreviewDirectionLength, 1.0f);
		const float ProbeRadius = FMath::Max(CaptureVolume->PreviewProbeRadius, 1.0f);
		for (const FUESplattingCaptureView& View : CaptureVolume->GetCachedPreviewCaptureViews())
		{
			const FVector Location = View.Transform.GetLocation();
			PreviewBounds += Location - FVector(ProbeRadius);
			PreviewBounds += Location + FVector(ProbeRadius);
			PreviewBounds += Location + View.Transform.GetUnitAxis(EAxis::X) * DirectionLength;
		}
		return FBoxSphereBounds(PreviewBounds);
	}

	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(1.0f), 1.0f);
}

AUESplattingCaptureVolume::AUESplattingCaptureVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;

	CaptureBounds = CreateDefaultSubobject<UUESplattingCaptureBoundsComponent>(TEXT("CaptureBounds"));
	SetRootComponent(CaptureBounds);
	CaptureBounds->InitBoxExtent(FVector(500.0, 500.0, 200.0));
	CaptureBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CaptureBounds->ShapeColor = FColor(0, 180, 255);
	CaptureBounds->bHiddenInGame = true;

	ProbePreview = CreateDefaultSubobject<UUESplattingCapturePreviewComponent>(TEXT("ProbePreview"));
	ProbePreview->SetupAttachment(CaptureBounds);
}

void AUESplattingCaptureVolume::ConfigureAsFocusedDetailRegion()
{
	CapturePattern = EUESplattingCaptureVolumePattern::FocusedDetail;
	DetailCaptureProfile = EUESplattingDetailCaptureProfile::Medium;
	if (CaptureBounds)
	{
		CaptureBounds->SetBoxExtent(FVector(100.0f, 100.0f, 75.0f));
		CaptureBounds->ShapeColor = FColor(255, 140, 20);
		CaptureBounds->MarkRenderStateDirty();
	}
	RequestPreviewRefresh();
}

void AUESplattingCaptureVolume::ConfigureAsDirectionalArray()
{
	CapturePattern = EUESplattingCaptureVolumePattern::SimpleSweep;
	DirectionalArraySpacingMeters = 1.0f;
	HorizontalFieldOfView = 90.0f;
	if (CaptureBounds)
	{
		// The local Y/Z face is the camera plane; local +X is the capture direction.
		CaptureBounds->SetBoxExtent(FVector(10.0f, 200.0f, 100.0f));
		CaptureBounds->ShapeColor = FColor(20, 210, 130);
		CaptureBounds->MarkRenderStateDirty();
	}
	RequestPreviewRefresh();
}

AUESplattingCaptureVolume* AUESplattingCaptureVolume::GetLinkedRoomCoverageVolume() const
{
	if (CapturePattern != EUESplattingCaptureVolumePattern::FocusedDetail
		|| !RoomCoverageVolume
		|| RoomCoverageVolume == this
		|| RoomCoverageVolume->CapturePattern != EUESplattingCaptureVolumePattern::RoomCoverage
		|| RoomCoverageVolume->GetWorld() != GetWorld())
	{
		return nullptr;
	}

	return RoomCoverageVolume;
}

void AUESplattingCaptureVolume::GetLinkedFocusedDetailRegions(
	TArray<AUESplattingCaptureVolume*>& OutDetailRegions) const
{
	OutDetailRegions.Reset();
	if (CapturePattern != EUESplattingCaptureVolumePattern::RoomCoverage || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AUESplattingCaptureVolume> It(GetWorld()); It; ++It)
	{
		AUESplattingCaptureVolume* Candidate = *It;
		if (Candidate && Candidate->GetLinkedRoomCoverageVolume() == this)
		{
			OutDetailRegions.Add(Candidate);
		}
	}

	OutDetailRegions.Sort([](const AUESplattingCaptureVolume& Left, const AUESplattingCaptureVolume& Right)
	{
		return Left.GetPathName().Compare(Right.GetPathName(), ESearchCase::IgnoreCase) < 0;
	});
}

void AUESplattingCaptureVolume::ExpandCaptureVolumeSet(
	const TArray<AUESplattingCaptureVolume*>& RequestedVolumes,
	TArray<AUESplattingCaptureVolume*>& OutCaptureVolumes)
{
	OutCaptureVolumes.Reset();

	const auto AddRoomCaptureSet = [&OutCaptureVolumes](AUESplattingCaptureVolume* RoomVolume)
	{
		if (!RoomVolume)
		{
			return;
		}

		OutCaptureVolumes.AddUnique(RoomVolume);
		TArray<AUESplattingCaptureVolume*> LinkedDetails;
		RoomVolume->GetLinkedFocusedDetailRegions(LinkedDetails);
		for (AUESplattingCaptureVolume* DetailRegion : LinkedDetails)
		{
			OutCaptureVolumes.AddUnique(DetailRegion);
		}
	};

	for (AUESplattingCaptureVolume* RequestedVolume : RequestedVolumes)
	{
		if (!IsValid(RequestedVolume))
		{
			continue;
		}

		if (RequestedVolume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
		{
			AddRoomCaptureSet(RequestedVolume);
		}
		else if (AUESplattingCaptureVolume* LinkedRoom = RequestedVolume->GetLinkedRoomCoverageVolume())
		{
			AddRoomCaptureSet(LinkedRoom);
		}
		else
		{
			OutCaptureVolumes.AddUnique(RequestedVolume);
		}
	}

	OutCaptureVolumes.Sort([](const AUESplattingCaptureVolume& Left, const AUESplattingCaptureVolume& Right)
	{
		return Left.GetPathName().Compare(Right.GetPathName(), ESearchCase::IgnoreCase) < 0;
	});
}

void AUESplattingCaptureVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (CaptureBounds)
	{
		switch (CapturePattern)
		{
		case EUESplattingCaptureVolumePattern::FocusedDetail:
			CaptureBounds->ShapeColor = FColor(255, 140, 20);
			break;
		case EUESplattingCaptureVolumePattern::SimpleSweep:
			CaptureBounds->ShapeColor = FColor(20, 210, 130);
			break;
		case EUESplattingCaptureVolumePattern::RoomCoverage:
		default:
			CaptureBounds->ShapeColor = FColor(0, 180, 255);
			break;
		}
	}
	if (HasValidReferencePostProcessSource())
	{
		// Keep old serialized actors and the Details panel aligned with the
		// authored-reference behavior enforced by export.
		ExportSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		ExportSettings.bUseEyeAdaptation = true;
	}
	RequestPreviewRefresh();
}

void AUESplattingCaptureVolume::BeginDestroy()
{
	if (PreviewRefreshTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewRefreshTickerHandle);
		PreviewRefreshTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

#if WITH_EDITOR
void AUESplattingCaptureVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AUESplattingCaptureVolume, RoomCoverageVolume)
		&& RoomCoverageVolume
		&& !GetLinkedRoomCoverageVolume())
	{
		RoomCoverageVolume = nullptr;
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("Room Coverage must reference a Room Coverage capture actor in the same world.")));
	}
	RequestPreviewRefresh();
}

void AUESplattingCaptureVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	RequestPreviewRefresh();
}
#endif

void AUESplattingCaptureVolume::ExportColmapDataset()
{
	ExportCaptureVolumeSet({this});
}

void AUESplattingCaptureVolume::ExportSingleCaptureVolume()
{
	if (ReferencePostProcessCamera && !HasValidReferencePostProcessSource())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("Reference Post Process Source must be an enabled Camera, Cine Camera, or Post Process Volume with non-zero blend weight. Export was stopped rather than silently changing the authored look.")));
		return;
	}

	TArray<FUESplattingCaptureView> CaptureViews;
	FUESplattingCaptureCoverageStats CoverageStats;
	const int32 StationCount = GenerateCaptureViews(CaptureViews, &CoverageStats);
	LastPreviewStationCount = StationCount;
	LastPreviewViewCount = CaptureViews.Num();
	LastCoverageStats = CoverageStats;

	FUESplattingDatasetExportResult StartResult;
	FUESplattingDatasetExportSettings EffectiveSettings = ExportSettings;
	if (HasValidReferencePostProcessSource())
	{
		// A selected reference camera is an explicit authored-look contract. This
		// also repairs existing actors saved while calibrated exposure was the default.
		EffectiveSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		EffectiveSettings.bUseEyeAdaptation = true;
	}
	if (EffectiveSettings.Renderer == EUESplattingSceneCaptureRenderer::SceneCapture2DLegacy
		&& EffectiveSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored
		&& !PrepareViewportMatchedPhotometrics(EffectiveSettings, true))
	{
		return;
	}
	if (EffectiveSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked)
	{
		// Export performs a definitive ticker-driven preflight using the same frame
		// cadence as the training images, even if the user already ran the preview.
		EffectiveSettings.bPhotometricCalibrationPerformed = false;
	}
	if (!UUESplattingDatasetExporter::ConfirmLargeCaptureIfNeeded(CaptureViews.Num()))
	{
		return;
	}
	if (CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage
		|| CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail
		|| CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep)
	{
		EffectiveSettings.RequestedProbeCount = GetRequestedCaptureStationCount();
		EffectiveSettings.AcceptedProbeCount = StationCount;
		EffectiveSettings.CandidateProbeCount = CoverageStats.CandidateStationCount;
		EffectiveSettings.ClearanceRejectedProbeCount = CoverageStats.ClearanceRejectedStationCount;
		EffectiveSettings.SurfacePatchCount = CoverageStats.SurfacePatchCount;
		EffectiveSettings.RepeatedSurfacePatchCount = CoverageStats.RepeatedSurfacePatchCount;
		EffectiveSettings.FloorPatchCount = CoverageStats.FloorPatchCount;
		EffectiveSettings.RepeatedFloorPatchCount = CoverageStats.RepeatedFloorPatchCount;
		EffectiveSettings.CloseDetailPatchCount = CoverageStats.CloseDetailPatchCount;
		EffectiveSettings.RepeatedCloseDetailPatchCount = CoverageStats.RepeatedCloseDetailPatchCount;
		EffectiveSettings.RepeatedSurfaceCoveragePercent = CoverageStats.RepeatedSurfaceCoveragePercent;
		EffectiveSettings.RepeatedFloorCoveragePercent = CoverageStats.RepeatedFloorCoveragePercent;
		EffectiveSettings.RepeatedCloseDetailCoveragePercent = CoverageStats.RepeatedCloseDetailCoveragePercent;
		EffectiveSettings.MinimumCoverageBaselineMeters = CoverageStats.MinimumObservationBaselineMeters;
	}
	if (EffectiveSettings.CapturePatternNotes.TrimStartAndEnd().IsEmpty())
	{
		if (CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
		{
			const FVector DimensionsMeters = GetCaptureDimensionsMeters();
			const FString ProfileName = GetResolvedCaptureProfileName();
			const FString ViewSetName = GetRoomCoverageViewSetName(GetResolvedRoomCoverageViewSet());
			EffectiveSettings.CapturePatternNotes = FString::Printf(
				TEXT("UESplatting room coverage volume: density_preset=%s; view_set=%s; placement=%s; probe_spacing_m=%.2f; volume_m=%.2f x %.2f x %.2f; floor_area_m2=%.2f; requested_probes=%d; accepted_probes=%d; candidate_probes=%d; clearance_rejected=%d; achieved_probe_spacing_m=%.2f; exported_images=%d; height_bands=%d; perspective_images_per_probe=%d; horizontal_fov_degrees=%.2f; camera_model=PINHOLE; panorama_mode=off; surface_patches=%d; repeated_surface_coverage_percent=%.1f; floor_patches=%d; repeated_floor_coverage_percent=%.1f; close_detail_patches=%d; repeated_close_detail_coverage_percent=%.1f; close_detail_distance_m=%.2f; minimum_observation_baseline_m=%.2f; zone_id=%s; block_id=%s; overlap_margin_m=%.2f; clearance_filter=%s; clearance_radius_cm=%.2f."),
				*ProfileName,
				*ViewSetName,
				bUseSceneAwarePlacement ? TEXT("scene-aware-trace-selection") : TEXT("spatial-candidate-selection"),
				GetResolvedRoomCoverageProbeSpacingMeters(),
				DimensionsMeters.X,
				DimensionsMeters.Y,
				DimensionsMeters.Z,
				GetEstimatedFloorAreaSquareMeters(),
				GetRequestedRoomCoverageProbeCount(),
				StationCount,
				CoverageStats.CandidateStationCount,
				CoverageStats.ClearanceRejectedStationCount,
				GetAchievedRoomCoverageProbeSpacingMeters(StationCount),
				CaptureViews.Num(),
				GetResolvedRoomCoverageHeightBands(),
				GetResolvedRoomCoverageViewsPerStation(),
				GetResolvedHorizontalFieldOfView(),
				CoverageStats.SurfacePatchCount,
				CoverageStats.RepeatedSurfaceCoveragePercent,
				CoverageStats.FloorPatchCount,
				CoverageStats.RepeatedFloorCoveragePercent,
				CoverageStats.CloseDetailPatchCount,
				CoverageStats.RepeatedCloseDetailCoveragePercent,
				CloseDetailDistanceMeters,
				CoverageStats.MinimumObservationBaselineMeters,
				*ZoneId,
				*BlockId,
				OverlapMarginMeters,
				bFilterByClearance ? TEXT("enabled") : TEXT("disabled"),
				CameraClearanceRadius);
		}
		else if (CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
		{
			const FVector DimensionsMeters = GetCaptureDimensionsMeters();
			EffectiveSettings.CapturePatternNotes = FString::Printf(
				TEXT("UESplatting focused detail region: quality=%s; target_m=%.2f x %.2f x %.2f; requested_camera_origins=%d; accepted_camera_origins=%d; exported_images=%d; azimuth_samples=%d; elevation_bands=%d; distance_rings=%d; views_per_origin=%d; close_camera_distance_m=%.2f; context_camera_distance_m=%.2f; minimum_elevation_degrees=%.1f; maximum_elevation_degrees=%.1f; horizontal_fov_degrees=%.1f; target_patches=%d; repeated_target_coverage_percent=%.1f; minimum_target_baseline_m=%.2f; clearance_filter=%s; clearance_radius_cm=%.2f; camera_model=PINHOLE."),
				*GetDetailCaptureProfileName(DetailCaptureProfile),
				DimensionsMeters.X,
				DimensionsMeters.Y,
				DimensionsMeters.Z,
				GetRequestedDetailCandidateCount(),
				StationCount,
				CaptureViews.Num(),
				GetResolvedDetailAzimuthSamples(),
				GetResolvedDetailElevationBands(),
				GetResolvedDetailDistanceRings(),
				GetResolvedDetailViewsPerStation(),
				DetailNearStandoffMeters,
				DetailFarStandoffMeters,
				DetailMinimumElevationDegrees,
				DetailMaximumElevationDegrees,
				GetResolvedHorizontalFieldOfView(),
				CoverageStats.SurfacePatchCount,
				CoverageStats.RepeatedSurfaceCoveragePercent,
				CoverageStats.MinimumObservationBaselineMeters,
				bFilterByClearance ? TEXT("enabled") : TEXT("disabled"),
				CameraClearanceRadius);
		}
		else
		{
			const FVector DimensionsMeters = GetCaptureDimensionsMeters();
			const FVector Forward = CaptureBounds->GetComponentTransform().GetRotation().GetForwardVector();
			EffectiveSettings.CapturePatternNotes = FString::Printf(
				TEXT("UESplatting directional camera array: layout=planar-yz; orientation=parallel-actor-forward; array_width_m=%.2f; array_height_m=%.2f; requested_camera_origins=%d; accepted_camera_origins=%d; clearance_rejected=%d; exported_images=%d; images_per_origin=1; camera_spacing_m=%.2f; forward_xyz=%.6f,%.6f,%.6f; horizontal_fov_degrees=%.2f; camera_model=PINHOLE; clearance_filter=%s; clearance_radius_cm=%.2f."),
				DimensionsMeters.Y,
				DimensionsMeters.Z,
				GetRequestedDirectionalArrayStationCount(),
				StationCount,
				CoverageStats.ClearanceRejectedStationCount,
				CaptureViews.Num(),
				DirectionalArraySpacingMeters,
				Forward.X,
				Forward.Y,
				Forward.Z,
				GetResolvedHorizontalFieldOfView(),
				bFilterByClearance ? TEXT("enabled") : TEXT("disabled"),
				CameraClearanceRadius);
		}
	}

	ApplyCaptureScopeToExportSettings(EffectiveSettings);

	TWeakObjectPtr<AUESplattingCaptureVolume> WeakThis(this);
	const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
		this,
		CaptureViews,
		EffectiveSettings,
		[WeakThis](const FUESplattingDatasetExportResult& CompletedResult)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}
			const FString Message = CompletedResult.Message
				+ (CompletedResult.OutputDirectory.IsEmpty()
					? TEXT("")
					: FString::Printf(TEXT("\n\nOutput:\n%s"), *CompletedResult.OutputDirectory));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
		},
		StartResult);

	if (!bStarted)
	{
		const FString Message = StartResult.Message
			+ (StartResult.OutputDirectory.IsEmpty()
				? TEXT("")
				: FString::Printf(TEXT("\n\nOutput:\n%s"), *StartResult.OutputDirectory));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	}
}

void AUESplattingCaptureVolume::ExportCaptureVolumeSet(
	const TArray<AUESplattingCaptureVolume*>& RequestedVolumes)
{
	TArray<AUESplattingCaptureVolume*> CaptureVolumes;
	ExpandCaptureVolumeSet(RequestedVolumes, CaptureVolumes);
	if (CaptureVolumes.IsEmpty())
	{
		return;
	}
	if (CaptureVolumes.Num() == 1)
	{
		CaptureVolumes[0]->ExportSingleCaptureVolume();
		return;
	}

	AUESplattingCaptureVolume* PrimaryVolume = FindPrimaryCaptureVolume(CaptureVolumes);
	if (!PrimaryVolume)
	{
		return;
	}

	FUESplattingDatasetExportSettings Settings = PrimaryVolume->ExportSettings;
	if (PrimaryVolume->ReferencePostProcessCamera && !PrimaryVolume->HasValidReferencePostProcessSource())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("The Room Coverage actor has an unsupported or disabled Reference Post Process Source. Export was stopped rather than silently changing the authored look.")));
		return;
	}
	if (PrimaryVolume->HasValidReferencePostProcessSource())
	{
		Settings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		Settings.bUseEyeAdaptation = true;
	}
	if (Settings.Renderer == EUESplattingSceneCaptureRenderer::SceneCapture2DLegacy
		&& Settings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored
		&& !PrimaryVolume->PrepareViewportMatchedPhotometrics(Settings, true))
	{
		return;
	}

	TArray<FUESplattingCaptureView> CaptureViews;
	int32 StationCount = 0;
	int32 RequestedStationCount = 0;
	int32 CandidateStationCount = 0;
	int32 ClearanceRejectedCount = 0;
	int32 SurfacePatchCount = 0;
	int32 RepeatedSurfacePatchCount = 0;
	int32 FloorPatchCount = 0;
	int32 RepeatedFloorPatchCount = 0;
	int32 CloseDetailPatchCount = 0;
	int32 RepeatedCloseDetailPatchCount = 0;
	int32 RoomVolumeCount = 0;
	int32 DetailRegionCount = 0;
	float MinimumCoverageBaselineMeters = 0.0f;

	for (AUESplattingCaptureVolume* CaptureVolume : CaptureVolumes)
	{
		RoomVolumeCount += CaptureVolume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage ? 1 : 0;
		DetailRegionCount += CaptureVolume->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail ? 1 : 0;

		TArray<FUESplattingCaptureView> VolumeViews;
		FUESplattingCaptureCoverageStats CoverageStats;
		const int32 VolumeStationCount = CaptureVolume->GenerateCaptureViews(VolumeViews, &CoverageStats);
		CaptureVolume->LastPreviewStationCount = VolumeStationCount;
		CaptureVolume->LastPreviewViewCount = VolumeViews.Num();
		CaptureVolume->LastCoverageStats = CoverageStats;

		StationCount += VolumeStationCount;
		RequestedStationCount += CaptureVolume->GetRequestedCaptureStationCount();
		CandidateStationCount += CoverageStats.CandidateStationCount;
		ClearanceRejectedCount += CoverageStats.ClearanceRejectedStationCount;
		SurfacePatchCount += CoverageStats.SurfacePatchCount;
		RepeatedSurfacePatchCount += CoverageStats.RepeatedSurfacePatchCount;
		FloorPatchCount += CoverageStats.FloorPatchCount;
		RepeatedFloorPatchCount += CoverageStats.RepeatedFloorPatchCount;
		CloseDetailPatchCount += CoverageStats.CloseDetailPatchCount;
		RepeatedCloseDetailPatchCount += CoverageStats.RepeatedCloseDetailPatchCount;
		MinimumCoverageBaselineMeters = FMath::Max(
			MinimumCoverageBaselineMeters,
			CoverageStats.MinimumObservationBaselineMeters);
		CaptureViews.Append(MoveTemp(VolumeViews));
	}

	PrimaryVolume->ApplyReferencePostProcessToViews(CaptureViews);
	Settings.RequestedProbeCount = RequestedStationCount;
	Settings.AcceptedProbeCount = StationCount;
	Settings.CandidateProbeCount = CandidateStationCount;
	Settings.ClearanceRejectedProbeCount = ClearanceRejectedCount;
	Settings.SurfacePatchCount = SurfacePatchCount;
	Settings.RepeatedSurfacePatchCount = RepeatedSurfacePatchCount;
	Settings.RepeatedSurfaceCoveragePercent = SurfacePatchCount > 0
		? 100.0 * static_cast<double>(RepeatedSurfacePatchCount) / static_cast<double>(SurfacePatchCount)
		: 0.0;
	Settings.FloorPatchCount = FloorPatchCount;
	Settings.RepeatedFloorPatchCount = RepeatedFloorPatchCount;
	Settings.RepeatedFloorCoveragePercent = FloorPatchCount > 0
		? 100.0 * static_cast<double>(RepeatedFloorPatchCount) / static_cast<double>(FloorPatchCount)
		: 0.0;
	Settings.CloseDetailPatchCount = CloseDetailPatchCount;
	Settings.RepeatedCloseDetailPatchCount = RepeatedCloseDetailPatchCount;
	Settings.RepeatedCloseDetailCoveragePercent = CloseDetailPatchCount > 0
		? 100.0 * static_cast<double>(RepeatedCloseDetailPatchCount) / static_cast<double>(CloseDetailPatchCount)
		: 0.0;
	Settings.MinimumCoverageBaselineMeters = MinimumCoverageBaselineMeters;

	if (Settings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::CalibratedLocked)
	{
		const float InitialCompensation = Settings.ManualExposureCompensation;
		FUESplattingPhotometricCalibrationResult Calibration;
		if (!UUESplattingDatasetExporter::CalibrateGlobalExposure(PrimaryVolume, CaptureViews, Settings, Calibration))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(Calibration.Warning.IsEmpty() ? TEXT("Photometric calibration failed.") : Calibration.Warning));
			return;
		}

		PrimaryVolume->LastPhotometricCalibration = Calibration;
		Settings.ManualExposureCompensation = Calibration.EffectiveExposureCompensation;
		Settings.bPhotometricCalibrationPerformed = true;
		Settings.CalibrationInitialExposureCompensation = InitialCompensation;
		Settings.CalibrationLuminanceP10 = Calibration.LuminanceP10;
		Settings.CalibrationLuminanceMedian = Calibration.LuminanceMedian;
		Settings.CalibrationLuminanceP90 = Calibration.LuminanceP90;
		Settings.CalibrationViewsEvaluated = Calibration.SampleViewCount;
		const FString Prompt = FString::Printf(
			TEXT("Global exposure: %+.2f\nLuma p10 / median / p90: %.1f / %.1f / %.1f\n\nUse this one exposure for the complete linked capture set?"),
			Calibration.EffectiveExposureCompensation,
			Calibration.LuminanceP10,
			Calibration.LuminanceMedian,
			Calibration.LuminanceP90);
		if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Prompt)) != EAppReturnType::Yes)
		{
			return;
		}
	}

	if (!UUESplattingDatasetExporter::ConfirmLargeCaptureIfNeeded(CaptureViews.Num()))
	{
		return;
	}
	if (Settings.CapturePatternNotes.TrimStartAndEnd().IsEmpty())
	{
		Settings.CapturePatternNotes = FString::Printf(
			TEXT("UESplatting linked capture set: volumes=%d; room_volumes=%d; focused_detail_regions=%d; accepted_camera_origins=%d; exported_images=%d; primary_settings_actor=%s; camera_model=PINHOLE."),
			CaptureVolumes.Num(),
			RoomVolumeCount,
			DetailRegionCount,
			StationCount,
			CaptureViews.Num(),
			*PrimaryVolume->GetName());
	}
	ApplyCombinedCaptureScopeToExportSettings(CaptureVolumes, Settings);

	const FString Prefix = FString::Printf(
		TEXT("Capture set: %d actor%s\nCamera origins: %d\nImages: %d\n\n"),
		CaptureVolumes.Num(),
		CaptureVolumes.Num() == 1 ? TEXT("") : TEXT("s"),
		StationCount,
		CaptureViews.Num());
	FUESplattingDatasetExportResult StartResult;
	const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
		PrimaryVolume,
		CaptureViews,
		Settings,
		[Prefix](const FUESplattingDatasetExportResult& CompletedResult)
		{
			const FString OutputText = CompletedResult.OutputDirectory.IsEmpty()
				? TEXT("")
				: FString::Printf(TEXT("\n\nOutput:\n%s"), *CompletedResult.OutputDirectory);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Prefix + CompletedResult.Message + OutputText));
		},
		StartResult);
	if (!bStarted)
	{
		const FString OutputText = StartResult.OutputDirectory.IsEmpty()
			? TEXT("")
			: FString::Printf(TEXT("\n\nOutput:\n%s"), *StartResult.OutputDirectory);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Prefix + StartResult.Message + OutputText));
	}
}

void AUESplattingCaptureVolume::CalibrateCaptureExposure()
{
	if (ExportSettings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue)
	{
		FUESplattingCaptureView ViewportView;
		FString Error;
		if (!UUESplattingDatasetExporter::ResolveActiveViewportView(this, ViewportView, Error))
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
			return;
		}

		TArray<FUESplattingCaptureView> PreviewViews;
		PreviewViews.Add(ViewportView);
		ApplyReferencePostProcess(PreviewViews);

		FUESplattingDatasetExportSettings PreviewSettings = ExportSettings;
		PreviewSettings.Renderer = EUESplattingSceneCaptureRenderer::MovieRenderQueue;
		PreviewSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		PreviewSettings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
		PreviewSettings.bGenerateTracePointCloud = false;
		PreviewSettings.CaptureId = FString::Printf(
			TEXT("mrq_viewport_%s"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		PreviewSettings.OutputDirectory.Path = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UESplatting"),
			TEXT("MRQPreviews"),
			PreviewSettings.CaptureId);

		FUESplattingDatasetExportResult StartResult;
		TWeakObjectPtr<AUESplattingCaptureVolume> WeakThis(this);
		const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
			this,
			PreviewViews,
			PreviewSettings,
			[WeakThis](const FUESplattingDatasetExportResult& CompletedResult)
			{
				AUESplattingCaptureVolume* CaptureVolume = WeakThis.Get();
				if (!CaptureVolume)
				{
					return;
				}
				CaptureVolume->LastPhotometricCalibration = FUESplattingPhotometricCalibrationResult();
				CaptureVolume->LastPhotometricCalibration.bSuccess = CompletedResult.bSuccess;
				if (CompletedResult.bSuccess)
				{
					const FString PreviewPath = FPaths::Combine(
						CompletedResult.OutputDirectory,
						TEXT("images"),
						TEXT("frame_000001.jpg"));
					CaptureVolume->LastPhotometricCalibration.PreviewImagePath = PreviewPath;
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*PreviewPath);
				}
				else
				{
					CaptureVolume->LastPhotometricCalibration.Warning = CompletedResult.Message;
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompletedResult.Message));
				}
			},
			StartResult);
		if (!bStarted)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(StartResult.Message));
		}
		return;
	}

	if (ExportSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored
		|| HasValidReferencePostProcessSource())
	{
		FUESplattingDatasetExportSettings EffectiveSettings = ExportSettings;
		EffectiveSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		PrepareViewportMatchedPhotometrics(EffectiveSettings, false);
		return;
	}

	TArray<FUESplattingCaptureView> CaptureViews;
	FUESplattingCaptureCoverageStats CoverageStats;
	LastPreviewStationCount = GenerateCaptureViews(CaptureViews, &CoverageStats);
	LastPreviewViewCount = CaptureViews.Num();
	LastCoverageStats = CoverageStats;
	CalibrateCaptureExposureForViews(CaptureViews, false);
}

bool AUESplattingCaptureVolume::CalibrateCaptureExposureForViews(
	const TArray<FUESplattingCaptureView>& CaptureViews,
	bool bConfirmBeforeExport)
{
	if (ExportSettings.PhotometricMode == EUESplattingSceneCapturePhotometricMode::SceneAuthored
		|| HasValidReferencePostProcessSource())
	{
		FUESplattingDatasetExportSettings EffectiveSettings = ExportSettings;
		EffectiveSettings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
		return PrepareViewportMatchedPhotometrics(EffectiveSettings, bConfirmBeforeExport);
	}

	if (ExportSettings.PhotometricMode != EUESplattingSceneCapturePhotometricMode::CalibratedLocked)
	{
		return true;
	}

	const float InitialCompensation = ExportSettings.ManualExposureCompensation;
	FUESplattingPhotometricCalibrationResult Calibration;
	if (!UUESplattingDatasetExporter::CalibrateGlobalExposure(this, CaptureViews, ExportSettings, Calibration))
	{
		LastPhotometricCalibration = Calibration;
		if (!FApp::IsUnattended())
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(Calibration.Warning.IsEmpty() ? TEXT("Photometric calibration failed.") : Calibration.Warning));
		}
		return false;
	}

	Modify();
	LastPhotometricCalibration = Calibration;
	ExportSettings.ManualExposureCompensation = Calibration.EffectiveExposureCompensation;
	ExportSettings.bPhotometricCalibrationPerformed = true;
	ExportSettings.CalibrationInitialExposureCompensation = InitialCompensation;
	ExportSettings.CalibrationLuminanceP10 = Calibration.LuminanceP10;
	ExportSettings.CalibrationLuminanceMedian = Calibration.LuminanceMedian;
	ExportSettings.CalibrationLuminanceP90 = Calibration.LuminanceP90;
	ExportSettings.CalibrationViewsEvaluated = Calibration.SampleViewCount;

	if (FApp::IsUnattended())
	{
		return true;
	}

	const FString WarningText = Calibration.Warning.IsEmpty()
		? TEXT("")
		: TEXT("\n\nReview: ") + Calibration.Warning;
	const FString PreviewText = Calibration.PreviewImagePath.IsEmpty()
		? TEXT("")
		: TEXT("\nRepresentative preview: ") + Calibration.PreviewImagePath;
	const FString Summary = FString::Printf(
		TEXT("Global exposure calibration\n\nExposure compensation: %+.2f\nFrame mean luminance p10 / median / p90: %.1f / %.1f / %.1f\nRepresentative views: %d%s%s"),
		Calibration.EffectiveExposureCompensation,
		Calibration.LuminanceP10,
		Calibration.LuminanceMedian,
		Calibration.LuminanceP90,
		Calibration.SampleViewCount,
		*PreviewText,
		*WarningText);
	if (!bConfirmBeforeExport)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
		return true;
	}

	return FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(Summary + TEXT("\n\nUse this one exposure for the full export?"))) == EAppReturnType::Yes;
}

bool AUESplattingCaptureVolume::PrepareViewportMatchedPhotometrics(
	FUESplattingDatasetExportSettings& InOutSettings,
	bool bConfirmBeforeExport)
{
	FUESplattingCaptureView ViewportView;
	FString Error;
	if (!UUESplattingDatasetExporter::ResolveActiveViewportExposure(this, InOutSettings, ViewportView, Error))
	{
		LastPhotometricCalibration = FUESplattingPhotometricCalibrationResult();
		LastPhotometricCalibration.Warning = Error;
		if (!FApp::IsUnattended())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
		}
		return false;
	}

	TArray<FUESplattingCaptureView> PreviewViews;
	PreviewViews.Add(ViewportView);
	ApplyReferencePostProcess(PreviewViews);
	FUESplattingPhotometricCalibrationResult PreviewResult;
	if (!UUESplattingDatasetExporter::CaptureViewportMatchedPreview(this, PreviewViews[0], InOutSettings, PreviewResult))
	{
		LastPhotometricCalibration = PreviewResult;
		if (!FApp::IsUnattended())
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(PreviewResult.Warning.IsEmpty() ? TEXT("Viewport-match preview failed.") : PreviewResult.Warning));
		}
		return false;
	}

	LastPhotometricCalibration = PreviewResult;
	if (FApp::IsUnattended())
	{
		return true;
	}

	if (!PreviewResult.PreviewImagePath.IsEmpty())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*PreviewResult.PreviewImagePath);
	}
	const FString Summary = FString::Printf(
		TEXT("Viewport-matched capture preview\n\nThe preview uses the active viewport pose and the exact dataset SceneCapture path.\nGlobal exposure compensation: %+.3f\nMeasured viewport exposure scale: %.6f\nPreview frame mean luminance: %.1f\n\nCompare the opened image with the active viewport. The full export will use this one exposure for every image while preserving authored post process."),
		InOutSettings.ManualExposureCompensation,
		InOutSettings.ViewportExposureScale,
		PreviewResult.LuminanceMedian);
	if (!bConfirmBeforeExport)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
		return true;
	}

	return FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(Summary + TEXT("\n\nDoes the preview match the viewport closely enough to continue?"))) == EAppReturnType::Yes;
}

void AUESplattingCaptureVolume::RefreshPreviewStats()
{
	if (PreviewRefreshTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewRefreshTickerHandle);
		PreviewRefreshTickerHandle.Reset();
	}
	UpdatePreviewStats();
}

void AUESplattingCaptureVolume::RequestPreviewRefresh()
{
	if (PreviewRefreshTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewRefreshTickerHandle);
	}

	TWeakObjectPtr<AUESplattingCaptureVolume> WeakThis(this);
	PreviewRefreshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float)
		{
			if (AUESplattingCaptureVolume* CaptureVolume = WeakThis.Get())
			{
				CaptureVolume->PreviewRefreshTickerHandle.Reset();
				CaptureVolume->UpdatePreviewStats();
			}
			return false;
		}),
		0.15f);
}

void AUESplattingCaptureVolume::UpdatePreviewStats()
{
	LastPreviewStationCount = GenerateCaptureViews(CachedPreviewCaptureViews, &LastCoverageStats);
	LastPreviewViewCount = CachedPreviewCaptureViews.Num();

	if (ProbePreview)
	{
		ProbePreview->SetVisibility(bShowProbePreview);
		ProbePreview->MarkRenderStateDirty();
		ProbePreview->UpdateBounds();
	}
}

int32 AUESplattingCaptureVolume::GenerateCaptureViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats) const
{
	OutViews.Reset();
	if (OutCoverageStats)
	{
		*OutCoverageStats = FUESplattingCaptureCoverageStats();
	}

	if (!CaptureBounds)
	{
		return 0;
	}

	int32 StationCount = 0;
	switch (CapturePattern)
	{
	case EUESplattingCaptureVolumePattern::RoomCoverage:
		StationCount = GenerateRoomCoverageViews(OutViews, OutCoverageStats);
		break;
	case EUESplattingCaptureVolumePattern::FocusedDetail:
		StationCount = GenerateFocusedDetailViews(OutViews, OutCoverageStats);
		break;
	case EUESplattingCaptureVolumePattern::SimpleSweep:
	default:
		StationCount = GenerateDirectionalArrayViews(OutViews, OutCoverageStats);
		break;
	}

	const FString CaptureGroupId = GetCaptureGroupId();
	const FString CaptureGroupKind = GetCaptureGroupKind();
	for (FUESplattingCaptureView& View : OutViews)
	{
		View.CaptureGroupId = CaptureGroupId;
		View.CaptureGroupKind = CaptureGroupKind;
	}
	ApplyReferencePostProcess(OutViews);
	return StationCount;
}

void AUESplattingCaptureVolume::ApplyReferencePostProcessToViews(TArray<FUESplattingCaptureView>& InOutViews) const
{
	ApplyReferencePostProcess(InOutViews);
}

void AUESplattingCaptureVolume::ApplyCaptureScopeToExportSettings(FUESplattingDatasetExportSettings& InOutSettings) const
{
	InOutSettings.CaptureProfile = GetResolvedCaptureProfileName();
	InOutSettings.CaptureZoneId = ZoneId;
	InOutSettings.CaptureBlockId = BlockId;
	InOutSettings.CaptureOverlapMarginMeters = FMath::Max(0.0f, OverlapMarginMeters);
	InOutSettings.bHasCaptureWorldBounds = false;
	InOutSettings.CaptureWorldBoundsMinMeters = FVector::ZeroVector;
	InOutSettings.CaptureWorldBoundsMaxMeters = FVector::ZeroVector;

	const FBox WorldBoundsMeters = GetCaptureWorldBoundsMeters();
	if (WorldBoundsMeters.IsValid)
	{
		InOutSettings.bHasCaptureWorldBounds = true;
		InOutSettings.CaptureWorldBoundsMinMeters = WorldBoundsMeters.Min;
		InOutSettings.CaptureWorldBoundsMaxMeters = WorldBoundsMeters.Max;
	}
}

void AUESplattingCaptureVolume::ApplyCombinedCaptureScopeToExportSettings(
	const TArray<AUESplattingCaptureVolume*>& CaptureVolumes,
	FUESplattingDatasetExportSettings& InOutSettings)
{
	if (CaptureVolumes.IsEmpty())
	{
		return;
	}
	if (CaptureVolumes.Num() == 1 && CaptureVolumes[0])
	{
		CaptureVolumes[0]->ApplyCaptureScopeToExportSettings(InOutSettings);
		return;
	}

	FBox CombinedBounds(ForceInit);
	TSet<FString> Profiles;
	TSet<FString> ZoneIds;
	TSet<FString> BlockIds;
	float MaximumOverlapMarginMeters = 0.0f;
	for (const AUESplattingCaptureVolume* Volume : CaptureVolumes)
	{
		if (!Volume)
		{
			continue;
		}

		Profiles.Add(Volume->GetResolvedCaptureProfileName());
		if (!Volume->ZoneId.TrimStartAndEnd().IsEmpty())
		{
			ZoneIds.Add(Volume->ZoneId.TrimStartAndEnd());
		}
		if (!Volume->BlockId.TrimStartAndEnd().IsEmpty())
		{
			BlockIds.Add(Volume->BlockId.TrimStartAndEnd());
		}
		MaximumOverlapMarginMeters = FMath::Max(MaximumOverlapMarginMeters, Volume->OverlapMarginMeters);

		const FBox VolumeBounds = Volume->GetCaptureWorldBoundsMeters();
		if (VolumeBounds.IsValid)
		{
			CombinedBounds += VolumeBounds;
		}
	}

	InOutSettings.CaptureProfile = Profiles.Num() == 1 ? Profiles.Array()[0] : TEXT("MultiVolume");
	InOutSettings.CaptureZoneId = ZoneIds.Num() == 1 ? ZoneIds.Array()[0] : (ZoneIds.IsEmpty() ? TEXT("") : TEXT("multiple"));
	InOutSettings.CaptureBlockId = BlockIds.Num() == 1 ? BlockIds.Array()[0] : (BlockIds.IsEmpty() ? TEXT("") : TEXT("multiple"));
	InOutSettings.CaptureOverlapMarginMeters = FMath::Max(0.0f, MaximumOverlapMarginMeters);
	InOutSettings.bHasCaptureWorldBounds = CombinedBounds.IsValid != 0;
	InOutSettings.CaptureWorldBoundsMinMeters = CombinedBounds.IsValid ? CombinedBounds.Min : FVector::ZeroVector;
	InOutSettings.CaptureWorldBoundsMaxMeters = CombinedBounds.IsValid ? CombinedBounds.Max : FVector::ZeroVector;
}

AUESplattingCaptureVolume* AUESplattingCaptureVolume::FindPrimaryCaptureVolume(
	const TArray<AUESplattingCaptureVolume*>& CaptureVolumes)
{
	AUESplattingCaptureVolume* PrimaryRoomVolume = nullptr;
	for (AUESplattingCaptureVolume* Volume : CaptureVolumes)
	{
		if (Volume && Volume->CapturePattern == EUESplattingCaptureVolumePattern::RoomCoverage)
		{
			if (!PrimaryRoomVolume
				|| Volume->GetPathName().Compare(PrimaryRoomVolume->GetPathName(), ESearchCase::IgnoreCase) < 0)
			{
				PrimaryRoomVolume = Volume;
			}
		}
	}
	if (PrimaryRoomVolume)
	{
		return PrimaryRoomVolume;
	}

	AUESplattingCaptureVolume* PrimaryVolume = nullptr;
	for (AUESplattingCaptureVolume* Volume : CaptureVolumes)
	{
		if (Volume
			&& (!PrimaryVolume
				|| Volume->GetPathName().Compare(PrimaryVolume->GetPathName(), ESearchCase::IgnoreCase) < 0))
		{
			PrimaryVolume = Volume;
		}
	}
	return PrimaryVolume;
}

void AUESplattingCaptureVolume::ApplyReferencePostProcess(TArray<FUESplattingCaptureView>& InOutViews) const
{
	FPostProcessSettings ReferenceSettings;
	float ReferenceBlendWeight = 0.0f;
	bool bNeedsPerViewOverride = false;
	if (!ResolveReferencePostProcess(ReferencePostProcessCamera, ReferenceSettings, ReferenceBlendWeight, bNeedsPerViewOverride)
		|| !bNeedsPerViewOverride)
	{
		return;
	}

	for (FUESplattingCaptureView& View : InOutViews)
	{
		View.PostProcessSettings = ReferenceSettings;
		View.PostProcessBlendWeight = ReferenceBlendWeight;
	}
}

bool AUESplattingCaptureVolume::HasValidReferencePostProcessSource() const
{
	FPostProcessSettings UnusedSettings;
	float UnusedBlendWeight = 0.0f;
	bool bUnusedNeedsPerViewOverride = false;
	return ResolveReferencePostProcess(ReferencePostProcessCamera, UnusedSettings, UnusedBlendWeight, bUnusedNeedsPerViewOverride);
}

int32 AUESplattingCaptureVolume::GenerateRoomCoverageViews(TArray<FUESplattingCaptureView>& OutViews, FUESplattingCaptureCoverageStats* OutCoverageStats) const
{
	const FVector Extent = GetEffectiveLocalExtent();
	const FTransform BoundsTransform = CaptureBounds->GetComponentTransform();
	const int32 TargetStationCount = GetRequestedRoomCoverageProbeCount();
	const int32 HeightBands = GetResolvedRoomCoverageHeightBands();
	TArray<TArray<FRotator>> ViewRotationsByBand;
	ViewRotationsByBand.Reserve(HeightBands);
	for (int32 BandIndex = 0; BandIndex < HeightBands; ++BandIndex)
	{
		ViewRotationsByBand.Add(BuildRoomCoverageViewRotations(
			GetResolvedRoomCoverageViewSet(),
			GetRoomCoverageYawPhaseDegrees(BandIndex, HeightBands)));
	}
	const int32 ViewsPerStation = ViewRotationsByBand[0].Num();
	const int32 MaxCandidateStationCount = FMath::Max(TargetStationCount * 6, TargetStationCount + HeightBands * 32);
	const float PatchSizeMeters = FMath::Clamp(CoveragePatchSizeMeters, 0.25f, 5.0f);
	const float MinimumBaselineMeters = FMath::Clamp(CoverageMinimumBaselineMeters, 0.1f, 5.0f);
	const float MinimumBaselineCentimeters = MinimumBaselineMeters * 100.0f;
	const int32 SurfaceRequiredObservations = FMath::Clamp(CoverageRequiredObservations, 2, 8);
	const int32 FloorRequiredObservations = FMath::Clamp(FloorCoverageRequiredObservations, 2, 8);
	const double CaptureAspectRatio = static_cast<double>(FMath::Max(1, ExportSettings.ImageWidth))
		/ static_cast<double>(FMath::Max(1, ExportSettings.ImageHeight));
	TArray<TArray<FUESplattingCoverageTraceDirection>> CoverageDirectionsByBand;
	CoverageDirectionsByBand.Reserve(HeightBands);
	for (const TArray<FRotator>& BandViewRotations : ViewRotationsByBand)
	{
		CoverageDirectionsByBand.Add(BuildCoverageTraceDirections(
			BandViewRotations,
			GetResolvedHorizontalFieldOfView(),
			CaptureAspectRatio));
	}
	const float CloseDetailDistanceCm = FMath::Clamp(CloseDetailDistanceMeters, 0.25f, 5.0f) * 100.0f;
	const FVector BoxExtent = CaptureBounds->GetUnscaledBoxExtent();
	const FVector AbsoluteScale = CaptureBounds->GetComponentTransform().GetScale3D().GetAbs();
	const FVector VolumeUp = BoundsTransform.GetUnitAxis(EAxis::Z);
	const float CoverageTraceDistance = FMath::Max(CaptureBounds->Bounds.SphereRadius * 2.5f, 100.0f);
	UWorld* World = GetWorld();

	TArray<FUESplattingCoverageCandidate> Candidates;
	Candidates.Reserve(MaxCandidateStationCount);
	TMap<FUESplattingCoveragePatchKey, FVector> CoveragePatchWorldLocations;
	TSet<FUESplattingCoveragePatchKey> DiscoveredCloseDetailPatches;
	int32 ClearanceRejectedCount = 0;

	for (int32 CandidateIndex = 1; CandidateIndex <= MaxCandidateStationCount; ++CandidateIndex)
	{
		const int32 BandIndex = (CandidateIndex - 1) % HeightBands;
		const int32 XYIndex = ((CandidateIndex - 1) / HeightBands) + 1;
		const double TX = Halton(XYIndex, 2);
		const double TY = Halton(XYIndex, 3);
		// Interior bands avoid producing entire downward/upward frames only centimeters
		// from the floor or ceiling while retaining useful vertical parallax.
		const double TZ = static_cast<double>(BandIndex + 1) / static_cast<double>(HeightBands + 1);
		const FVector LocalStation(
			FMath::Lerp(-Extent.X, Extent.X, TX),
			FMath::Lerp(-Extent.Y, Extent.Y, TY),
			FMath::Lerp(-Extent.Z, Extent.Z, TZ));

		const FVector WorldLocation = BoundsTransform.TransformPosition(LocalStation);
		if (!IsStationClear(WorldLocation))
		{
			++ClearanceRejectedCount;
			continue;
		}

		FUESplattingCoverageCandidate Candidate;
		Candidate.LocalLocation = LocalStation;
		Candidate.WorldLocation = WorldLocation;
		Candidate.BandIndex = BandIndex;

		if (bUseSceneAwarePlacement && World)
		{
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UESplattingCaptureCoverage), true);
			QueryParams.AddIgnoredActor(this);
			TMap<FUESplattingCoveragePatchKey, FVector> CandidatePatchWorldLocations;

			for (const FUESplattingCoverageTraceDirection& TraceDirection : CoverageDirectionsByBand[BandIndex])
			{
				const FVector WorldDirection = BoundsTransform.TransformVectorNoScale(TraceDirection.Direction).GetSafeNormal();
				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(
					Hit,
					WorldLocation,
					WorldLocation + WorldDirection * CoverageTraceDistance,
					ClearanceChannel,
					QueryParams) || !Hit.bBlockingHit)
				{
					continue;
				}

				const FVector HitLocal = BoundsTransform.InverseTransformPosition(Hit.ImpactPoint);
				if (FMath::Abs(HitLocal.X) > BoxExtent.X + 1.0f
					|| FMath::Abs(HitLocal.Y) > BoxExtent.Y + 1.0f
					|| FMath::Abs(HitLocal.Z) > BoxExtent.Z + 1.0f)
				{
					continue;
				}

				const FVector HitLocalMeters(
					HitLocal.X * AbsoluteScale.X * 0.01f,
					HitLocal.Y * AbsoluteScale.Y * 0.01f,
					HitLocal.Z * AbsoluteScale.Z * 0.01f);
				const float UpDot = FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), VolumeUp);
				EUESplattingCoverageSurfaceType SurfaceType = EUESplattingCoverageSurfaceType::VerticalOrDetail;
				if (UpDot >= 0.65f)
				{
					SurfaceType = EUESplattingCoverageSurfaceType::Floor;
				}
				else if (UpDot <= -0.65f)
				{
					SurfaceType = EUESplattingCoverageSurfaceType::Ceiling;
				}

				const FUESplattingCoveragePatchKey PatchKey{
					FMath::FloorToInt32(HitLocalMeters.X / PatchSizeMeters),
					FMath::FloorToInt32(HitLocalMeters.Y / PatchSizeMeters),
					FMath::FloorToInt32(HitLocalMeters.Z / PatchSizeMeters),
					SurfaceType,
				};
				Candidate.VisiblePatches.Add(PatchKey);
				if (Hit.Distance <= CloseDetailDistanceCm)
				{
					Candidate.CloseDetailPatches.Add(PatchKey);
					DiscoveredCloseDetailPatches.Add(PatchKey);
				}
				CandidatePatchWorldLocations.FindOrAdd(PatchKey) = Hit.ImpactPoint;
			}
			for (const TPair<FUESplattingCoveragePatchKey, FVector>& PatchPair : CandidatePatchWorldLocations)
			{
				CoveragePatchWorldLocations.FindOrAdd(PatchPair.Key) = PatchPair.Value;
			}
		}

		Candidates.Add(MoveTemp(Candidate));
	}

	if (bUseSceneAwarePlacement && World)
	{
		FCollisionQueryParams FloorVisibilityQueryParams(SCENE_QUERY_STAT(UESplattingCaptureFloorCoverage), true);
		FloorVisibilityQueryParams.AddIgnoredActor(this);
		const float SamePatchToleranceSquared = FMath::Square(PatchSizeMeters * 100.0f * 0.75f);
		for (FUESplattingCoverageCandidate& Candidate : Candidates)
		{
			for (const TPair<FUESplattingCoveragePatchKey, FVector>& PatchPair : CoveragePatchWorldLocations)
			{
				if (PatchPair.Key.SurfaceType != EUESplattingCoverageSurfaceType::Floor)
				{
					continue;
				}

				const FVector ToPatch = PatchPair.Value - Candidate.WorldLocation;
				if (FVector::DotProduct(ToPatch, VolumeUp) >= -10.0f || ToPatch.IsNearlyZero())
				{
					continue;
				}

				const FVector Direction = ToPatch.GetSafeNormal();
				FHitResult FloorHit;
				if (World->LineTraceSingleByChannel(
					FloorHit,
					Candidate.WorldLocation,
					PatchPair.Value + Direction * 10.0f,
					ClearanceChannel,
					FloorVisibilityQueryParams)
					&& FloorHit.bBlockingHit
					&& FVector::DistSquared(FloorHit.ImpactPoint, PatchPair.Value) <= SamePatchToleranceSquared)
				{
					Candidate.VisiblePatches.Add(PatchPair.Key);
				}
			}
		}
	}

	TSet<FUESplattingCoveragePatchKey> DiscoveredPatches;
	for (const FUESplattingCoverageCandidate& Candidate : Candidates)
	{
		for (const FUESplattingCoveragePatchKey& Patch : Candidate.VisiblePatches)
		{
			DiscoveredPatches.Add(Patch);
		}
	}

	TArray<int32> SelectedCandidateIndices;
	SelectedCandidateIndices.Reserve(FMath::Min(TargetStationCount, Candidates.Num()));
	TArray<bool> bCandidateSelected;
	bCandidateSelected.Init(false, Candidates.Num());
	TArray<double> MinimumDistanceSquaredByCandidate;
	MinimumDistanceSquaredByCandidate.Init(DBL_MAX, Candidates.Num());
	TArray<int32> SelectedPerBand;
	SelectedPerBand.Init(0, HeightBands);
	TMap<FUESplattingCoveragePatchKey, FUESplattingCoverageObservers> PatchObservers;
	TMap<FUESplattingCoveragePatchKey, FUESplattingCoverageObservers> CloseDetailPatchObservers;
	const float NominalSpacingMeters = GetResolvedRoomCoverageProbeSpacingMeters();
	const float MinimumStationSpacingMeters = FMath::Max(0.25f, NominalSpacingMeters * 0.35f);
	float MinimumStationSpacingSquaredCm = FMath::Square(MinimumStationSpacingMeters * 100.0f);
	const int32 TargetPerBand = FMath::CeilToInt(static_cast<float>(TargetStationCount) / static_cast<float>(HeightBands));

	while (SelectedCandidateIndices.Num() < TargetStationCount && SelectedCandidateIndices.Num() < Candidates.Num())
	{
		int32 BestCandidateIndex = INDEX_NONE;
		double BestScore = -DBL_MAX;

		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (bCandidateSelected[CandidateIndex])
			{
				continue;
			}

			const FUESplattingCoverageCandidate& Candidate = Candidates[CandidateIndex];
			const double MinimumDistanceSquaredCm = MinimumDistanceSquaredByCandidate[CandidateIndex];
			if (!SelectedCandidateIndices.IsEmpty() && MinimumDistanceSquaredCm < MinimumStationSpacingSquaredCm)
			{
				continue;
			}

			double Score = 0.0;
			if (bUseSceneAwarePlacement)
			{
				for (const FUESplattingCoveragePatchKey& Patch : Candidate.VisiblePatches)
				{
					const FUESplattingCoverageObservers* ExistingObservers = PatchObservers.Find(Patch);
					const int32 ExistingObservationCount = ExistingObservers ? ExistingObservers->WorldLocations.Num() : 0;
					const int32 RequiredObservations = GetRequiredObservations(Patch, SurfaceRequiredObservations, FloorRequiredObservations);
					if (ExistingObservationCount < RequiredObservations
						&& IsBaselineSeparated(Candidate.WorldLocation, ExistingObservers, MinimumBaselineCentimeters))
					{
						Score += GetCoverageWeight(Patch.SurfaceType) * static_cast<double>(RequiredObservations - ExistingObservationCount);
					}
				}
				for (const FUESplattingCoveragePatchKey& Patch : Candidate.CloseDetailPatches)
				{
					const FUESplattingCoverageObservers* ExistingDetailObservers = CloseDetailPatchObservers.Find(Patch);
					const int32 ExistingDetailObservationCount = ExistingDetailObservers ? ExistingDetailObservers->WorldLocations.Num() : 0;
					const int32 RequiredObservations = GetRequiredObservations(Patch, SurfaceRequiredObservations, FloorRequiredObservations);
					if (ExistingDetailObservationCount < RequiredObservations
						&& IsBaselineSeparated(Candidate.WorldLocation, ExistingDetailObservers, MinimumBaselineCentimeters))
					{
						// Close, translated observations carry the texture detail that broad room
						// coverage alone cannot recover.
						Score += GetCoverageWeight(Patch.SurfaceType) * 2.0
							* static_cast<double>(RequiredObservations - ExistingDetailObservationCount);
					}
				}
			}

			const double MinimumDistanceMeters = SelectedCandidateIndices.IsEmpty()
				? NominalSpacingMeters
				: FMath::Sqrt(MinimumDistanceSquaredCm) * 0.01;
			Score += FMath::Clamp(MinimumDistanceMeters / FMath::Max(NominalSpacingMeters, 0.25f), 0.0, 2.0) * 3.0;
			Score += static_cast<double>(Candidate.VisiblePatches.Num()) * 0.02;
			Score += static_cast<double>(Candidate.CloseDetailPatches.Num()) * 0.05;
			if (SelectedPerBand.IsValidIndex(Candidate.BandIndex) && SelectedPerBand[Candidate.BandIndex] < TargetPerBand)
			{
				Score += 2.0;
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				BestCandidateIndex = CandidateIndex;
			}
		}

		if (BestCandidateIndex == INDEX_NONE)
		{
			// Clearance can leave fewer well-spaced candidates than requested. Fill the
			// remaining density budget with the best unused candidates rather than
			// silently reducing the capture.
			MinimumStationSpacingSquaredCm = 0.0f;
			continue;
		}

		bCandidateSelected[BestCandidateIndex] = true;
		SelectedCandidateIndices.Add(BestCandidateIndex);
		const FUESplattingCoverageCandidate& SelectedCandidate = Candidates[BestCandidateIndex];
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (!bCandidateSelected[CandidateIndex])
			{
				MinimumDistanceSquaredByCandidate[CandidateIndex] = FMath::Min(
					MinimumDistanceSquaredByCandidate[CandidateIndex],
					static_cast<double>(FVector::DistSquared(
						Candidates[CandidateIndex].WorldLocation,
						SelectedCandidate.WorldLocation)));
			}
		}
		if (SelectedPerBand.IsValidIndex(SelectedCandidate.BandIndex))
		{
			++SelectedPerBand[SelectedCandidate.BandIndex];
		}
		for (const FUESplattingCoveragePatchKey& Patch : SelectedCandidate.VisiblePatches)
		{
			FUESplattingCoverageObservers& Observers = PatchObservers.FindOrAdd(Patch);
			if (IsBaselineSeparated(SelectedCandidate.WorldLocation, &Observers, MinimumBaselineCentimeters))
			{
				Observers.WorldLocations.Add(SelectedCandidate.WorldLocation);
			}
		}
		for (const FUESplattingCoveragePatchKey& Patch : SelectedCandidate.CloseDetailPatches)
		{
			FUESplattingCoverageObservers& Observers = CloseDetailPatchObservers.FindOrAdd(Patch);
			if (IsBaselineSeparated(SelectedCandidate.WorldLocation, &Observers, MinimumBaselineCentimeters))
			{
				Observers.WorldLocations.Add(SelectedCandidate.WorldLocation);
			}
		}
	}

	for (int32 SelectedIndex = 0; SelectedIndex < SelectedCandidateIndices.Num(); ++SelectedIndex)
	{
		const FUESplattingCoverageCandidate& Candidate = Candidates[SelectedCandidateIndices[SelectedIndex]];
		const TArray<FRotator>& CandidateViewRotations = ViewRotationsByBand[Candidate.BandIndex];
		for (int32 ViewIndex = 0; ViewIndex < ViewsPerStation; ++ViewIndex)
		{
			const FQuat WorldRotation = BoundsTransform.GetRotation() * CandidateViewRotations[ViewIndex].Quaternion();
			FUESplattingCaptureView& View = OutViews.AddDefaulted_GetRef();
			View.Transform = FTransform(WorldRotation, Candidate.WorldLocation, FVector::OneVector);
			View.HorizontalFieldOfView = GetResolvedHorizontalFieldOfView();
			View.StationIndex = SelectedIndex;
			View.DebugName = FString::Printf(TEXT("%s_Room_%04d_%02d"), *GetName(), SelectedIndex + 1, ViewIndex);
		}
	}

	if (OutCoverageStats)
	{
		OutCoverageStats->CandidateStationCount = Candidates.Num();
		OutCoverageStats->ClearanceRejectedStationCount = ClearanceRejectedCount;
		OutCoverageStats->MinimumObservationBaselineMeters = MinimumBaselineMeters;
		OutCoverageStats->SurfacePatchCount = DiscoveredPatches.Num();
		for (const FUESplattingCoveragePatchKey& Patch : DiscoveredPatches)
		{
			const FUESplattingCoverageObservers* Observers = PatchObservers.Find(Patch);
			const int32 ObservationCount = Observers ? Observers->WorldLocations.Num() : 0;
			const int32 RequiredObservations = GetRequiredObservations(Patch, SurfaceRequiredObservations, FloorRequiredObservations);
			if (ObservationCount >= RequiredObservations)
			{
				++OutCoverageStats->RepeatedSurfacePatchCount;
			}
			if (Patch.SurfaceType == EUESplattingCoverageSurfaceType::Floor)
			{
				++OutCoverageStats->FloorPatchCount;
				if (ObservationCount >= FloorRequiredObservations)
				{
					++OutCoverageStats->RepeatedFloorPatchCount;
				}
			}
		}

		OutCoverageStats->bSceneAwareAssessmentAvailable = bUseSceneAwarePlacement && !DiscoveredPatches.IsEmpty();
		OutCoverageStats->RepeatedSurfaceCoveragePercent = OutCoverageStats->SurfacePatchCount > 0
			? 100.0f * static_cast<float>(OutCoverageStats->RepeatedSurfacePatchCount) / static_cast<float>(OutCoverageStats->SurfacePatchCount)
			: 0.0f;
		OutCoverageStats->RepeatedFloorCoveragePercent = OutCoverageStats->FloorPatchCount > 0
			? 100.0f * static_cast<float>(OutCoverageStats->RepeatedFloorPatchCount) / static_cast<float>(OutCoverageStats->FloorPatchCount)
			: 0.0f;
		OutCoverageStats->CloseDetailPatchCount = DiscoveredCloseDetailPatches.Num();
		for (const FUESplattingCoveragePatchKey& Patch : DiscoveredCloseDetailPatches)
		{
			const FUESplattingCoverageObservers* Observers = CloseDetailPatchObservers.Find(Patch);
			const int32 ObservationCount = Observers ? Observers->WorldLocations.Num() : 0;
			const int32 RequiredObservations = GetRequiredObservations(Patch, SurfaceRequiredObservations, FloorRequiredObservations);
			if (ObservationCount >= RequiredObservations)
			{
				++OutCoverageStats->RepeatedCloseDetailPatchCount;
			}
		}
		OutCoverageStats->RepeatedCloseDetailCoveragePercent = OutCoverageStats->CloseDetailPatchCount > 0
			? 100.0f * static_cast<float>(OutCoverageStats->RepeatedCloseDetailPatchCount) / static_cast<float>(OutCoverageStats->CloseDetailPatchCount)
			: 0.0f;

		if (Candidates.Num() < TargetStationCount)
		{
			OutCoverageStats->Warning = FString::Printf(
				TEXT("Only %d of %d requested probes survived camera-clearance checks (%d rejects). Resize or shift the volume and inspect collision."),
				Candidates.Num(),
				TargetStationCount,
				ClearanceRejectedCount);
		}
		else if (bUseSceneAwarePlacement && DiscoveredPatches.IsEmpty())
		{
			OutCoverageStats->Warning = TEXT("No collision-backed surface patches were found. Coverage is spatial only; inspect collision or the trace channel.");
		}
		else if (OutCoverageStats->FloorPatchCount == 0)
		{
			OutCoverageStats->Warning = TEXT("No floor patches were identified. Check floor collision, volume placement, and coverage trace settings.");
		}
		else if (OutCoverageStats->RepeatedFloorCoveragePercent < 70.0f
			|| OutCoverageStats->RepeatedSurfaceCoveragePercent < 60.0f
			|| (OutCoverageStats->CloseDetailPatchCount > 0 && OutCoverageStats->RepeatedCloseDetailCoveragePercent < 50.0f))
		{
			OutCoverageStats->Warning = TEXT("Shared broad or close-detail surface coverage is weak. Increase density, adjust the volume, or inspect collision before export.");
		}
	}

	return SelectedCandidateIndices.Num();
}

int32 AUESplattingCaptureVolume::GenerateFocusedDetailViews(
	TArray<FUESplattingCaptureView>& OutViews,
	FUESplattingCaptureCoverageStats* OutCoverageStats) const
{
	if (!CaptureBounds)
	{
		return 0;
	}

	const FTransform BoundsTransform = CaptureBounds->GetComponentTransform();
	const FVector TargetCenter = BoundsTransform.GetLocation();
	const FVector LocalExtent = CaptureBounds->GetUnscaledBoxExtent();
	const FVector AbsoluteScale = BoundsTransform.GetScale3D().GetAbs();
	const FVector WorldExtent(
		LocalExtent.X * AbsoluteScale.X,
		LocalExtent.Y * AbsoluteScale.Y,
		LocalExtent.Z * AbsoluteScale.Z);
	const FQuat BoundsRotation = BoundsTransform.GetRotation();
	const int32 AzimuthSamples = GetResolvedDetailAzimuthSamples();
	const int32 ElevationBands = GetResolvedDetailElevationBands();
	const int32 DistanceRings = GetResolvedDetailDistanceRings();
	const int32 ViewsPerStation = GetResolvedDetailViewsPerStation();
	const float NearStandoffCm = FMath::Clamp(DetailNearStandoffMeters, 0.1f, 10.0f) * 100.0f;
	const float FarStandoffCm = FMath::Max(
		NearStandoffCm,
		FMath::Clamp(DetailFarStandoffMeters, 0.1f, 20.0f) * 100.0f);
	const float MinimumElevation = FMath::Clamp(
		FMath::Min(DetailMinimumElevationDegrees, DetailMaximumElevationDegrees),
		-60.0f,
		85.0f);
	const float MaximumElevation = FMath::Clamp(
		FMath::Max(DetailMinimumElevationDegrees, DetailMaximumElevationDegrees),
		MinimumElevation,
		85.0f);
	const int32 TargetSampleGrid = FMath::Clamp(DetailTargetSampleGrid, 2, 5);
	const float TargetPatchSizeMeters = FMath::Clamp(DetailTargetPatchSizeMeters, 0.05f, 2.0f);
	const int32 RequiredObservations = FMath::Clamp(DetailRequiredObservations, 2, 8);
	const float MinimumBaselineMeters = FMath::Clamp(DetailMinimumBaselineMeters, 0.05f, 5.0f);
	const float MinimumBaselineCm = MinimumBaselineMeters * 100.0f;
	UWorld* World = GetWorld();

	TArray<FVector> TargetSamples;
	TargetSamples.Reserve(TargetSampleGrid * TargetSampleGrid * TargetSampleGrid);
	for (int32 X = 0; X < TargetSampleGrid; ++X)
	{
		const double TX = TargetSampleGrid > 1
			? FMath::Lerp(-0.8, 0.8, static_cast<double>(X) / static_cast<double>(TargetSampleGrid - 1))
			: 0.0;
		for (int32 Y = 0; Y < TargetSampleGrid; ++Y)
		{
			const double TY = TargetSampleGrid > 1
				? FMath::Lerp(-0.8, 0.8, static_cast<double>(Y) / static_cast<double>(TargetSampleGrid - 1))
				: 0.0;
			for (int32 Z = 0; Z < TargetSampleGrid; ++Z)
			{
				const double TZ = TargetSampleGrid > 1
					? FMath::Lerp(-0.8, 0.8, static_cast<double>(Z) / static_cast<double>(TargetSampleGrid - 1))
					: 0.0;
				TargetSamples.Add(BoundsTransform.TransformPosition(FVector(
					LocalExtent.X * TX,
					LocalExtent.Y * TY,
					LocalExtent.Z * TZ)));
			}
		}
	}

	TArray<FUESplattingDetailCandidate> Candidates;
	Candidates.Reserve(GetRequestedDetailCandidateCount());
	TSet<FIntVector> DiscoveredTargetPatches;
	int32 ClearanceRejectedCount = 0;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UESplattingFocusedDetailCoverage), true);
	QueryParams.AddIgnoredActor(this);
	for (int32 RingIndex = 0; RingIndex < DistanceRings; ++RingIndex)
	{
		const float RingAlpha = DistanceRings > 1
			? static_cast<float>(RingIndex) / static_cast<float>(DistanceRings - 1)
			: 0.0f;
		const float StandoffCm = FMath::Lerp(NearStandoffCm, FarStandoffCm, RingAlpha);
		for (int32 ElevationIndex = 0; ElevationIndex < ElevationBands; ++ElevationIndex)
		{
			const float ElevationAlpha = ElevationBands > 1
				? static_cast<float>(ElevationIndex) / static_cast<float>(ElevationBands - 1)
				: 0.5f;
			const float ElevationDegrees = FMath::Lerp(MinimumElevation, MaximumElevation, ElevationAlpha);
			const float ElevationRadians = FMath::DegreesToRadians(ElevationDegrees);
			const float HorizontalScale = FMath::Cos(ElevationRadians);
			const float VerticalScale = FMath::Sin(ElevationRadians);
			const float AzimuthOffset = (static_cast<float>(ElevationIndex) * 0.5f + static_cast<float>(RingIndex) * 0.25f)
				/ static_cast<float>(AzimuthSamples);

			for (int32 AzimuthIndex = 0; AzimuthIndex < AzimuthSamples; ++AzimuthIndex)
			{
				const float AzimuthTurns = static_cast<float>(AzimuthIndex) / static_cast<float>(AzimuthSamples) + AzimuthOffset;
				const float AzimuthRadians = AzimuthTurns * UE_TWO_PI;
				const FVector LocalDirection(
					HorizontalScale * FMath::Cos(AzimuthRadians),
					HorizontalScale * FMath::Sin(AzimuthRadians),
					VerticalScale);
				float TargetSurfaceDistance = MAX_flt;
				const auto IncludeAxisSurfaceDistance = [&TargetSurfaceDistance](double Direction, double Extent)
				{
					if (FMath::Abs(Direction) > UE_SMALL_NUMBER)
					{
						TargetSurfaceDistance = FMath::Min(
							TargetSurfaceDistance,
							static_cast<float>(Extent / FMath::Abs(Direction)));
					}
				};
				IncludeAxisSurfaceDistance(LocalDirection.X, WorldExtent.X);
				IncludeAxisSurfaceDistance(LocalDirection.Y, WorldExtent.Y);
				IncludeAxisSurfaceDistance(LocalDirection.Z, WorldExtent.Z);
				const FVector WorldDirection = BoundsRotation.RotateVector(LocalDirection).GetSafeNormal();
				const FVector WorldLocation = TargetCenter + WorldDirection * (TargetSurfaceDistance + StandoffCm);
				if (!IsStationClear(WorldLocation))
				{
					++ClearanceRejectedCount;
					continue;
				}

				FUESplattingDetailCandidate Candidate;
				Candidate.WorldLocation = WorldLocation;
				Candidate.ElevationBand = ElevationIndex;
				Candidate.DistanceRing = RingIndex;
				if (World)
				{
					for (const FVector& TargetSample : TargetSamples)
					{
						const FVector ToTarget = TargetSample - WorldLocation;
						if (ToTarget.IsNearlyZero())
						{
							continue;
						}

						FHitResult Hit;
						const FVector TraceEnd = TargetSample + ToTarget.GetSafeNormal() * 10.0f;
						if (!World->LineTraceSingleByChannel(
							Hit,
							WorldLocation,
							TraceEnd,
							ClearanceChannel,
							QueryParams) || !Hit.bBlockingHit)
						{
							continue;
						}

						const FVector LocalHit = BoundsTransform.InverseTransformPosition(Hit.ImpactPoint);
						if (FMath::Abs(LocalHit.X) > LocalExtent.X * 1.05f
							|| FMath::Abs(LocalHit.Y) > LocalExtent.Y * 1.05f
							|| FMath::Abs(LocalHit.Z) > LocalExtent.Z * 1.05f)
						{
							continue;
						}

						const FVector LocalHitMeters(
							LocalHit.X * AbsoluteScale.X * 0.01f,
							LocalHit.Y * AbsoluteScale.Y * 0.01f,
							LocalHit.Z * AbsoluteScale.Z * 0.01f);
						const FIntVector PatchKey(
							FMath::FloorToInt32(LocalHitMeters.X / TargetPatchSizeMeters),
							FMath::FloorToInt32(LocalHitMeters.Y / TargetPatchSizeMeters),
							FMath::FloorToInt32(LocalHitMeters.Z / TargetPatchSizeMeters));
						Candidate.VisiblePatchTargets.FindOrAdd(PatchKey) = Hit.ImpactPoint;
						DiscoveredTargetPatches.Add(PatchKey);
					}
				}
				Candidates.Add(MoveTemp(Candidate));
			}
		}
	}

	const bool bHasCollisionBackedTargets = !DiscoveredTargetPatches.IsEmpty();
	TMap<FIntVector, FUESplattingCoverageObservers> PatchObservers;
	int32 StationIndex = 0;
	for (const FUESplattingDetailCandidate& Candidate : Candidates)
	{
		if (bHasCollisionBackedTargets && Candidate.VisiblePatchTargets.IsEmpty())
		{
			continue;
		}

		TArray<FVector> AvailableTargets;
		if (bHasCollisionBackedTargets)
		{
			TArray<FIntVector> VisiblePatchKeys;
			Candidate.VisiblePatchTargets.GenerateKeyArray(VisiblePatchKeys);
			VisiblePatchKeys.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.X != Right.X)
				{
					return Left.X < Right.X;
				}
				if (Left.Y != Right.Y)
				{
					return Left.Y < Right.Y;
				}
				return Left.Z < Right.Z;
			});
			AvailableTargets.Reserve(VisiblePatchKeys.Num());
			for (const FIntVector& PatchKey : VisiblePatchKeys)
			{
				AvailableTargets.Add(Candidate.VisiblePatchTargets.FindChecked(PatchKey));
			}
		}
		else
		{
			AvailableTargets = TargetSamples;
		}
		if (AvailableTargets.IsEmpty())
		{
			AvailableTargets.Add(TargetCenter);
		}

		TArray<FVector> SelectedTargets;
		SelectedTargets.Reserve(ViewsPerStation);
		while (!AvailableTargets.IsEmpty() && SelectedTargets.Num() < ViewsPerStation)
		{
			int32 BestTargetIndex = 0;
			double BestTargetScore = SelectedTargets.IsEmpty() ? DBL_MAX : -DBL_MAX;
			for (int32 TargetIndex = 0; TargetIndex < AvailableTargets.Num(); ++TargetIndex)
			{
				double Score = FVector::DistSquared(AvailableTargets[TargetIndex], TargetCenter);
				if (SelectedTargets.IsEmpty())
				{
					if (Score < BestTargetScore)
					{
						BestTargetScore = Score;
						BestTargetIndex = TargetIndex;
					}
					continue;
				}

				Score = DBL_MAX;
				for (const FVector& SelectedTarget : SelectedTargets)
				{
					Score = FMath::Min(Score, static_cast<double>(FVector::DistSquared(AvailableTargets[TargetIndex], SelectedTarget)));
				}
				if (Score > BestTargetScore)
				{
					BestTargetScore = Score;
					BestTargetIndex = TargetIndex;
				}
			}
			SelectedTargets.Add(AvailableTargets[BestTargetIndex]);
			AvailableTargets.RemoveAtSwap(BestTargetIndex, 1, EAllowShrinking::No);
		}

		for (int32 TargetIndex = 0; TargetIndex < SelectedTargets.Num(); ++TargetIndex)
		{
			const FVector ViewDirection = (SelectedTargets[TargetIndex] - Candidate.WorldLocation).GetSafeNormal();
			if (ViewDirection.IsNearlyZero())
			{
				continue;
			}
			FUESplattingCaptureView& View = OutViews.AddDefaulted_GetRef();
			View.Transform = FTransform(FRotationMatrix::MakeFromX(ViewDirection).ToQuat(), Candidate.WorldLocation);
			View.HorizontalFieldOfView = GetResolvedHorizontalFieldOfView();
			View.StationIndex = StationIndex;
			View.DebugName = FString::Printf(TEXT("%s_Detail_%04d_%02d"), *GetName(), StationIndex + 1, TargetIndex);
		}

		for (const TPair<FIntVector, FVector>& Patch : Candidate.VisiblePatchTargets)
		{
			FUESplattingCoverageObservers& Observers = PatchObservers.FindOrAdd(Patch.Key);
			if (IsBaselineSeparated(Candidate.WorldLocation, &Observers, MinimumBaselineCm))
			{
				Observers.WorldLocations.Add(Candidate.WorldLocation);
			}
		}
		++StationIndex;
	}

	if (OutCoverageStats)
	{
		OutCoverageStats->CandidateStationCount = Candidates.Num();
		OutCoverageStats->ClearanceRejectedStationCount = ClearanceRejectedCount;
		OutCoverageStats->MinimumObservationBaselineMeters = MinimumBaselineMeters;
		OutCoverageStats->SurfacePatchCount = DiscoveredTargetPatches.Num();
		OutCoverageStats->CloseDetailPatchCount = DiscoveredTargetPatches.Num();
		for (const FIntVector& Patch : DiscoveredTargetPatches)
		{
			const FUESplattingCoverageObservers* Observers = PatchObservers.Find(Patch);
			if (Observers && Observers->WorldLocations.Num() >= RequiredObservations)
			{
				++OutCoverageStats->RepeatedSurfacePatchCount;
				++OutCoverageStats->RepeatedCloseDetailPatchCount;
			}
		}
		OutCoverageStats->bSceneAwareAssessmentAvailable = bHasCollisionBackedTargets;
		OutCoverageStats->RepeatedSurfaceCoveragePercent = OutCoverageStats->SurfacePatchCount > 0
			? 100.0f * static_cast<float>(OutCoverageStats->RepeatedSurfacePatchCount) / static_cast<float>(OutCoverageStats->SurfacePatchCount)
			: 0.0f;
		OutCoverageStats->RepeatedCloseDetailCoveragePercent = OutCoverageStats->RepeatedSurfaceCoveragePercent;

		if (StationIndex == 0)
		{
			OutCoverageStats->Warning = TEXT("No focused-detail camera origins survived clearance and target-visibility checks. Resize or shift the target box and inspect collision.");
		}
		else if (!bHasCollisionBackedTargets)
		{
			OutCoverageStats->Warning = TEXT("No collision-backed target patches were found. The full spatial camera shell is retained; inspect the live view directions before export.");
		}
		else if (OutCoverageStats->RepeatedSurfaceCoveragePercent < 60.0f)
		{
			OutCoverageStats->Warning = TEXT("Repeated target coverage is weak. Increase detail quality, widen the target box, or adjust camera distances.");
		}
	}

	return StationIndex;
}

int32 AUESplattingCaptureVolume::GenerateDirectionalArrayViews(
	TArray<FUESplattingCaptureView>& OutViews,
	FUESplattingCaptureCoverageStats* OutCoverageStats) const
{
	const FVector Extent = GetEffectiveLocalExtent();
	const FTransform BoundsTransform = CaptureBounds->GetComponentTransform();
	const FVector AbsoluteScale = BoundsTransform.GetScale3D().GetAbs();
	const float SpacingCm = FMath::Clamp(DirectionalArraySpacingMeters, 0.1f, 1000.0f) * 100.0f;
	const float WidthCm = Extent.Y * AbsoluteScale.Y * 2.0f;
	const float HeightCm = Extent.Z * AbsoluteScale.Z * 2.0f;
	const int32 CountY = GetDirectionalArrayAxisCount(WidthCm, SpacingCm);
	const int32 CountZ = GetDirectionalArrayAxisCount(HeightCm, SpacingCm);
	const int32 CandidateStationCount = CountY * CountZ;
	const FQuat WorldRotation = BoundsTransform.GetRotation();
	int32 AcceptedStationCount = 0;
	int32 ClearanceRejectedCount = 0;

	for (int32 Z = 0; Z < CountZ; ++Z)
	{
		const double TZ = CountZ == 1 ? 0.5 : static_cast<double>(Z) / static_cast<double>(CountZ - 1);
		for (int32 Y = 0; Y < CountY; ++Y)
		{
			const double TY = CountY == 1 ? 0.5 : static_cast<double>(Y) / static_cast<double>(CountY - 1);
			const FVector LocalStation(
				0.0,
				FMath::Lerp(-Extent.Y, Extent.Y, TY),
				FMath::Lerp(-Extent.Z, Extent.Z, TZ));
			const FVector WorldLocation = BoundsTransform.TransformPosition(LocalStation);
			if (!IsStationClear(WorldLocation))
			{
				++ClearanceRejectedCount;
				continue;
			}

			FUESplattingCaptureView& View = OutViews.AddDefaulted_GetRef();
			View.Transform = FTransform(WorldRotation, WorldLocation, FVector::OneVector);
			View.HorizontalFieldOfView = GetResolvedHorizontalFieldOfView();
			View.StationIndex = AcceptedStationCount;
			View.DebugName = FString::Printf(TEXT("%s_Directional_%04d"), *GetName(), AcceptedStationCount + 1);
			++AcceptedStationCount;
		}
	}

	if (OutCoverageStats)
	{
		OutCoverageStats->CandidateStationCount = CandidateStationCount;
		OutCoverageStats->ClearanceRejectedStationCount = ClearanceRejectedCount;
		if (AcceptedStationCount == 0)
		{
			OutCoverageStats->Warning = TEXT("No directional-array camera origins survived clearance filtering. Move the array or disable clearance filtering after inspecting the preview.");
		}
		else if (ClearanceRejectedCount > 0)
		{
			OutCoverageStats->Warning = FString::Printf(
				TEXT("%d of %d directional-array camera origins were removed by clearance filtering."),
				ClearanceRejectedCount,
				CandidateStationCount);
		}
	}

	return AcceptedStationCount;
}

FVector AUESplattingCaptureVolume::GetEffectiveLocalExtent() const
{
	const FVector BoxExtent = CaptureBounds ? CaptureBounds->GetUnscaledBoxExtent() : FVector(1.0);
	if (CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail
		|| CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep)
	{
		// Focused Detail uses the bounds as a target. Directional Array uses its
		// unmodified Y/Z face as the camera plane. Clearance rejects camera origins
		// instead of silently changing either user-authored shape.
		return BoxExtent;
	}
	const FVector Scale = CaptureBounds ? CaptureBounds->GetComponentTransform().GetScale3D() : FVector::OneVector;
	const float Inset = bFilterByClearance ? FMath::Max(CameraClearanceRadius, 0.0f) : 0.0f;
	const auto SafeScale = [](double Value)
	{
		return FMath::Max(FMath::Abs(Value), static_cast<double>(KINDA_SMALL_NUMBER));
	};
	return FVector(
		FMath::Max(0.0, BoxExtent.X - Inset / SafeScale(Scale.X)),
		FMath::Max(0.0, BoxExtent.Y - Inset / SafeScale(Scale.Y)),
		FMath::Max(0.0, BoxExtent.Z - Inset / SafeScale(Scale.Z)));
}

FVector AUESplattingCaptureVolume::GetCaptureDimensionsMeters() const
{
	const FVector LocalExtent = GetEffectiveLocalExtent();
	const FVector Scale = CaptureBounds ? CaptureBounds->GetComponentTransform().GetScale3D() : FVector::OneVector;
	const FVector WorldExtent(
		LocalExtent.X * FMath::Abs(Scale.X),
		LocalExtent.Y * FMath::Abs(Scale.Y),
		LocalExtent.Z * FMath::Abs(Scale.Z));
	return WorldExtent * 2.0f * 0.01f;
}

FBox AUESplattingCaptureVolume::GetCaptureWorldBoundsMeters() const
{
	if (!CaptureBounds)
	{
		return FBox(ForceInit);
	}

	const FVector LocalExtent = GetEffectiveLocalExtent();
	const FTransform BoundsTransform = CaptureBounds->GetComponentTransform();
	FBox WorldBounds(ForceInit);
	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 Z = -1; Z <= 1; Z += 2)
			{
				WorldBounds += BoundsTransform.TransformPosition(FVector(LocalExtent.X * X, LocalExtent.Y * Y, LocalExtent.Z * Z)) * 0.01f;
			}
		}
	}
	return WorldBounds;
}

FString AUESplattingCaptureVolume::GetResolvedCaptureProfileName() const
{
	if (CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
	{
		return FString::Printf(TEXT("FocusedDetail%s"), *GetDetailCaptureProfileName(DetailCaptureProfile));
	}
	if (CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep)
	{
		return TEXT("DirectionalArray");
	}
	return GetCaptureProfileName(CaptureProfile);
}

double AUESplattingCaptureVolume::GetEstimatedFloorAreaSquareMeters() const
{
	const FVector DimensionsMeters = GetCaptureDimensionsMeters();
	return FMath::Max(0.0, static_cast<double>(DimensionsMeters.X) * static_cast<double>(DimensionsMeters.Y));
}

float AUESplattingCaptureVolume::GetResolvedRoomCoverageProbeSpacingMeters() const
{
	if (CaptureProfile == EUESplattingCaptureProfile::Custom)
	{
		return FMath::Clamp(RoomCoverageCustomProbeSpacingMeters, 0.25f, 10.0f);
	}

	switch (CaptureProfile)
	{
	case EUESplattingCaptureProfile::Low:
		return 1.5f;
	case EUESplattingCaptureProfile::High:
		return 0.75f;
	case EUESplattingCaptureProfile::Ultra:
		return 0.5f;
	case EUESplattingCaptureProfile::Medium:
	default:
		return 1.0f;
	}
}

EUESplattingRoomCoverageViewSet AUESplattingCaptureVolume::GetResolvedRoomCoverageViewSet() const
{
	if (CaptureProfile == EUESplattingCaptureProfile::Custom)
	{
		return RoomCoverageViewSet;
	}
	return EUESplattingRoomCoverageViewSet::Standard8;
}

int32 AUESplattingCaptureVolume::GetResolvedRoomCoverageHeightBands() const
{
	if (CaptureProfile == EUESplattingCaptureProfile::Custom)
	{
		return FMath::Clamp(RoomCoverageHeightBands, 1, 5);
	}
	return 3;
}

float AUESplattingCaptureVolume::GetResolvedHorizontalFieldOfView() const
{
	if (CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail)
	{
		if (const AUESplattingCaptureVolume* LinkedRoom = GetLinkedRoomCoverageVolume())
		{
			return LinkedRoom->GetResolvedHorizontalFieldOfView();
		}
	}
	return FMath::Clamp(HorizontalFieldOfView, 1.0f, 179.0f);
}

int32 AUESplattingCaptureVolume::GetResolvedDetailAzimuthSamples() const
{
	if (DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom)
	{
		return FMath::Clamp(DetailCustomAzimuthSamples, 4, 96);
	}
	switch (DetailCaptureProfile)
	{
	case EUESplattingDetailCaptureProfile::Low:
		return 16;
	case EUESplattingDetailCaptureProfile::High:
		return 32;
	case EUESplattingDetailCaptureProfile::Ultra:
		return 48;
	case EUESplattingDetailCaptureProfile::Medium:
	default:
		return 24;
	}
}

int32 AUESplattingCaptureVolume::GetResolvedDetailElevationBands() const
{
	if (DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom)
	{
		return FMath::Clamp(DetailCustomElevationBands, 1, 8);
	}
	switch (DetailCaptureProfile)
	{
	case EUESplattingDetailCaptureProfile::High:
		return 4;
	case EUESplattingDetailCaptureProfile::Ultra:
		return 5;
	case EUESplattingDetailCaptureProfile::Low:
	case EUESplattingDetailCaptureProfile::Medium:
	default:
		return 3;
	}
}

int32 AUESplattingCaptureVolume::GetResolvedDetailDistanceRings() const
{
	if (DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom)
	{
		return FMath::Clamp(DetailCustomDistanceRings, 1, 3);
	}
	return DetailCaptureProfile == EUESplattingDetailCaptureProfile::Low ? 1 : 2;
}

int32 AUESplattingCaptureVolume::GetResolvedDetailViewsPerStation() const
{
	if (DetailCaptureProfile == EUESplattingDetailCaptureProfile::Custom)
	{
		return FMath::Clamp(DetailCustomViewsPerStation, 1, 5);
	}
	return DetailCaptureProfile == EUESplattingDetailCaptureProfile::Ultra ? 3
		: (DetailCaptureProfile == EUESplattingDetailCaptureProfile::Low ? 1 : 2);
}

int32 AUESplattingCaptureVolume::GetRequestedDetailCandidateCount() const
{
	return GetResolvedDetailAzimuthSamples()
		* GetResolvedDetailElevationBands()
		* GetResolvedDetailDistanceRings();
}

int32 AUESplattingCaptureVolume::GetRequestedDirectionalArrayStationCount() const
{
	if (!CaptureBounds)
	{
		return 0;
	}

	const FVector Extent = GetEffectiveLocalExtent();
	const FVector AbsoluteScale = CaptureBounds->GetComponentTransform().GetScale3D().GetAbs();
	const float SpacingCm = FMath::Clamp(DirectionalArraySpacingMeters, 0.1f, 1000.0f) * 100.0f;
	const float WidthCm = Extent.Y * AbsoluteScale.Y * 2.0f;
	const float HeightCm = Extent.Z * AbsoluteScale.Z * 2.0f;
	const int32 CountY = GetDirectionalArrayAxisCount(WidthCm, SpacingCm);
	const int32 CountZ = GetDirectionalArrayAxisCount(HeightCm, SpacingCm);
	return CountY * CountZ;
}

int32 AUESplattingCaptureVolume::GetRequestedCaptureStationCount() const
{
	switch (CapturePattern)
	{
	case EUESplattingCaptureVolumePattern::RoomCoverage:
		return GetRequestedRoomCoverageProbeCount();
	case EUESplattingCaptureVolumePattern::FocusedDetail:
		return GetRequestedDetailCandidateCount();
	case EUESplattingCaptureVolumePattern::SimpleSweep:
	default:
		return GetRequestedDirectionalArrayStationCount();
	}
}

FString AUESplattingCaptureVolume::GetCaptureGroupId() const
{
	return GetPathName();
}

FString AUESplattingCaptureVolume::GetCaptureGroupKind() const
{
	switch (CapturePattern)
	{
	case EUESplattingCaptureVolumePattern::RoomCoverage:
		return TEXT("room");
	case EUESplattingCaptureVolumePattern::FocusedDetail:
		return TEXT("focused_detail");
	case EUESplattingCaptureVolumePattern::SimpleSweep:
	default:
		return TEXT("directional_array");
	}
}

int32 AUESplattingCaptureVolume::GetResolvedRoomCoverageViewsPerStation() const
{
	return BuildRoomCoverageViewRotations(GetResolvedRoomCoverageViewSet()).Num();
}

int32 AUESplattingCaptureVolume::GetRequestedRoomCoverageProbeCount() const
{
	const double FloorAreaSquareMeters = GetEstimatedFloorAreaSquareMeters();
	const double ProbeSpacingMeters = FMath::Max(static_cast<double>(GetResolvedRoomCoverageProbeSpacingMeters()), 0.25);
	const int32 HeightBands = GetResolvedRoomCoverageHeightBands();
	return FMath::Max(1, FMath::RoundToInt(FloorAreaSquareMeters / (ProbeSpacingMeters * ProbeSpacingMeters) * static_cast<double>(HeightBands)));
}

float AUESplattingCaptureVolume::GetAchievedRoomCoverageProbeSpacingMeters(int32 AcceptedStationCount) const
{
	if (AcceptedStationCount <= 0)
	{
		return 0.0f;
	}

	const double FloorAreaSquareMeters = GetEstimatedFloorAreaSquareMeters();
	const int32 HeightBands = GetResolvedRoomCoverageHeightBands();
	const double ProbesPerHeightBand = static_cast<double>(AcceptedStationCount) / static_cast<double>(HeightBands);
	if (FloorAreaSquareMeters <= 0.0 || ProbesPerHeightBand <= 0.0)
	{
		return 0.0f;
	}

	return static_cast<float>(FMath::Sqrt(FloorAreaSquareMeters / ProbesPerHeightBand));
}

int64 AUESplattingCaptureVolume::GetEstimatedImageStorageBytes(int32 ImageCount) const
{
	if (ImageCount <= 0)
	{
		return 0;
	}

	const double PixelCount = static_cast<double>(FMath::Max(1, ExportSettings.ImageWidth))
		* static_cast<double>(FMath::Max(1, ExportSettings.ImageHeight));
	double EstimatedBytesPerPixel = 2.2;
	if (ExportSettings.ImageFormat == EUESplattingSceneCaptureImageFormat::JPEG)
	{
		if (ExportSettings.Renderer == EUESplattingSceneCaptureRenderer::MovieRenderQueue)
		{
			// Calibrated from the viewport-faithful 1920x1080 alley capture. High-detail
			// authored scenes compress less than the legacy render-target estimate.
			EstimatedBytesPerPixel = 0.68;
		}
		else
		{
			const double QualityAlpha = static_cast<double>(FMath::Clamp(ExportSettings.JpegQuality, 50, 100) - 50) / 50.0;
			EstimatedBytesPerPixel = FMath::Lerp(0.18, 0.42, QualityAlpha);
		}
	}
	return FMath::RoundToInt64(PixelCount * EstimatedBytesPerPixel * static_cast<double>(ImageCount));
}

bool AUESplattingCaptureVolume::IsStationClear(const FVector& WorldLocation) const
{
	if (!bFilterByClearance || CameraClearanceRadius <= 0.0f)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UESplattingCaptureVolumeClearance), false);
	QueryParams.AddIgnoredActor(this);
	return !World->OverlapBlockingTestByChannel(
		WorldLocation,
		FQuat::Identity,
		ClearanceChannel,
		FCollisionShape::MakeSphere(CameraClearanceRadius),
		QueryParams);
}

double AUESplattingCaptureVolume::Halton(int32 Index, int32 Base)
{
	double Result = 0.0;
	double Fraction = 1.0 / static_cast<double>(Base);

	while (Index > 0)
	{
		Result += Fraction * static_cast<double>(Index % Base);
		Index = FMath::FloorToInt(static_cast<double>(Index) / static_cast<double>(Base));
		Fraction /= static_cast<double>(Base);
	}

	return Result;
}
