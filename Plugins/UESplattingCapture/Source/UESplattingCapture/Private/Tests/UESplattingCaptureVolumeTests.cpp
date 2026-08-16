// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "UESplattingCaptureVolume.h"
#include "UESplattingDatasetExporter.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureVolumeDensityPresetsTest,
	"UESplatting.Capture.Volume.DensityPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureVolumeDensityPresetsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AUESplattingCaptureVolume* CaptureVolume = World->SpawnActor<AUESplattingCaptureVolume>(
		AUESplattingCaptureVolume::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Capture volume can be spawned"), CaptureVolume))
	{
		return false;
	}

	TestTrue(
		TEXT("New capture volumes default to Medium density"),
		CaptureVolume->CaptureProfile == EUESplattingCaptureProfile::Medium);

	// The default box is 10 x 10 x 4 m. This scale reproduces the validated
	// 9.2 x 9.2 x 3.2 m alley volume without depending on collision in the map.
	CaptureVolume->SetActorScale3D(FVector(0.92, 0.92, 0.8));
	CaptureVolume->bFilterByClearance = false;
	CaptureVolume->bUseSceneAwarePlacement = false;
	CaptureVolume->bShowProbePreview = false;

	const FVector DimensionsMeters = CaptureVolume->GetCaptureDimensionsMeters();
	TestTrue(TEXT("Capture width is 9.2 m"), FMath::IsNearlyEqual(DimensionsMeters.X, 9.2f, 0.001f));
	TestTrue(TEXT("Capture depth is 9.2 m"), FMath::IsNearlyEqual(DimensionsMeters.Y, 9.2f, 0.001f));
	TestTrue(TEXT("Capture height is 3.2 m"), FMath::IsNearlyEqual(DimensionsMeters.Z, 3.2f, 0.001f));

	const auto TestPreset = [this, CaptureVolume](
		EUESplattingCaptureProfile Preset,
		float ExpectedSpacingMeters,
		int32 ExpectedProbeCount)
	{
		CaptureVolume->CaptureProfile = Preset;
		TestTrue(
			*FString::Printf(TEXT("%s uses %.2f m spacing"), *CaptureVolume->GetResolvedCaptureProfileName(), ExpectedSpacingMeters),
			FMath::IsNearlyEqual(CaptureVolume->GetResolvedRoomCoverageProbeSpacingMeters(), ExpectedSpacingMeters));
		TestEqual(
			*FString::Printf(TEXT("%s keeps three height bands"), *CaptureVolume->GetResolvedCaptureProfileName()),
			CaptureVolume->GetResolvedRoomCoverageHeightBands(),
			3);
		TestTrue(
			*FString::Printf(TEXT("%s keeps Standard8 angular coverage"), *CaptureVolume->GetResolvedCaptureProfileName()),
			CaptureVolume->GetResolvedRoomCoverageViewSet() == EUESplattingRoomCoverageViewSet::Standard8);
		TestEqual(
			*FString::Printf(TEXT("%s keeps eight views per probe"), *CaptureVolume->GetResolvedCaptureProfileName()),
			CaptureVolume->GetResolvedRoomCoverageViewsPerStation(),
			8);
		TestEqual(
			*FString::Printf(TEXT("%s derives the expected translated-probe budget"), *CaptureVolume->GetResolvedCaptureProfileName()),
			CaptureVolume->GetRequestedRoomCoverageProbeCount(),
			ExpectedProbeCount);
	};

	TestPreset(EUESplattingCaptureProfile::Low, 1.5f, 113);
	TestPreset(EUESplattingCaptureProfile::Medium, 1.0f, 254);
	TestPreset(EUESplattingCaptureProfile::High, 0.75f, 451);
	TestPreset(EUESplattingCaptureProfile::Ultra, 0.5f, 1016);

	CaptureVolume->CaptureProfile = EUESplattingCaptureProfile::Custom;
	CaptureVolume->RoomCoverageCustomProbeSpacingMeters = 1.25f;
	CaptureVolume->RoomCoverageHeightBands = 3;
	CaptureVolume->RoomCoverageViewSet = EUESplattingRoomCoverageViewSet::Standard8;
	TestTrue(
		TEXT("Custom exposes its exact requested spacing"),
		FMath::IsNearlyEqual(CaptureVolume->GetResolvedRoomCoverageProbeSpacingMeters(), 1.25f));
	TestEqual(
		TEXT("Custom derives its probe budget from area, spacing, and height bands"),
		CaptureVolume->GetRequestedRoomCoverageProbeCount(),
		163);

	CaptureVolume->CaptureProfile = EUESplattingCaptureProfile::Ultra;
	TArray<FUESplattingCaptureView> Views;
	FUESplattingCaptureCoverageStats CoverageStats;
	const double StartTime = FPlatformTime::Seconds();
	const int32 GeneratedProbeCount = CaptureVolume->GenerateCaptureViews(Views, &CoverageStats);
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	TestEqual(TEXT("Ultra generates every requested translated probe"), GeneratedProbeCount, 1016);
	TestEqual(TEXT("Ultra generates eight perspective frames per probe"), Views.Num(), 8128);
	TestEqual(TEXT("Ultra evaluates a bounded six-times candidate pool"), CoverageStats.CandidateStationCount, 6096);

	TMap<int32, TArray<FQuat>> RotationsByYawPhase;
	for (int32 StationStart = 0; StationStart < Views.Num(); StationStart += 8)
	{
		const int32 YawPhase = FMath::RoundToInt(FRotator::NormalizeAxis(
			Views[StationStart].Transform.Rotator().Yaw));
		if (!RotationsByYawPhase.Contains(YawPhase))
		{
			TArray<FQuat>& PhaseRotations = RotationsByYawPhase.Add(YawPhase);
			PhaseRotations.Reserve(8);
			for (int32 ViewIndex = 0; ViewIndex < 8; ++ViewIndex)
			{
				PhaseRotations.Add(Views[StationStart + ViewIndex].Transform.GetRotation());
			}
		}
	}
	TestEqual(TEXT("Standard8 uses one world-yaw phase per height band"), RotationsByYawPhase.Num(), 3);
	TestTrue(TEXT("Lower height band uses the 0-degree phase"), RotationsByYawPhase.Contains(0));
	TestTrue(TEXT("Middle height band uses the 30-degree phase"), RotationsByYawPhase.Contains(30));
	TestTrue(TEXT("Upper height band uses the 60-degree phase"), RotationsByYawPhase.Contains(60));

	const double AspectRatio = static_cast<double>(CaptureVolume->ExportSettings.ImageWidth)
		/ static_cast<double>(CaptureVolume->ExportSettings.ImageHeight);
	const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(
		static_cast<double>(CaptureVolume->GetResolvedHorizontalFieldOfView())) * 0.5);
	const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
	constexpr int32 DirectionSampleCount = 20000;
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
	int32 UnseenDirectionCount = 0;
	for (int32 SampleIndex = 0; SampleIndex < DirectionSampleCount; ++SampleIndex)
	{
		const double Z = 1.0 - 2.0 * (static_cast<double>(SampleIndex) + 0.5) / DirectionSampleCount;
		const double Radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Azimuth = GoldenAngle * static_cast<double>(SampleIndex);
		const FVector WorldDirection(Radius * FMath::Cos(Azimuth), Radius * FMath::Sin(Azimuth), Z);
		bool bDirectionSeen = false;
		for (const TPair<int32, TArray<FQuat>>& PhasePair : RotationsByYawPhase)
		{
			for (const FQuat& ViewRotation : PhasePair.Value)
			{
				const FVector CameraDirection = ViewRotation.UnrotateVector(WorldDirection);
				if (CameraDirection.X > UE_SMALL_NUMBER
					&& FMath::Abs(CameraDirection.Y) <= CameraDirection.X * TanHalfHorizontal
					&& FMath::Abs(CameraDirection.Z) <= CameraDirection.X * TanHalfVertical)
				{
					bDirectionSeen = true;
					break;
				}
			}
			if (bDirectionSeen)
			{
				break;
			}
		}
		UnseenDirectionCount += bDirectionSeen ? 0 : 1;
	}
	const double UnseenDirectionPercent = 100.0 * static_cast<double>(UnseenDirectionCount) / DirectionSampleCount;
	AddInfo(FString::Printf(
		TEXT("Three-band phased Standard8 unseen world-direction estimate: %.3f%%."),
		UnseenDirectionPercent));
	TestTrue(
		*FString::Printf(TEXT("Phased Standard8 leaves less than one percent of world directions unseen (%.3f%%)"), UnseenDirectionPercent),
		UnseenDirectionPercent < 1.0);
	AddInfo(FString::Printf(TEXT("Generated the 1,016-probe Ultra layout in %.3f seconds."), ElapsedSeconds));

	World->DestroyActor(CaptureVolume);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureVolumeDirectionalArrayTest,
	"UESplatting.Capture.Volume.DirectionalArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureVolumeDirectionalArrayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AUESplattingCaptureVolume* DirectionalArray = World->SpawnActor<AUESplattingCaptureVolume>(
		AUESplattingCaptureVolume::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Directional array can be spawned"), DirectionalArray))
	{
		return false;
	}

	DirectionalArray->ConfigureAsDirectionalArray();
	DirectionalArray->SetActorTransform(FTransform(
		FRotator(-12.0f, 37.0f, 0.0f),
		FVector(1200.0f, -800.0f, 450.0f),
		FVector(1.0f, 2.0f, 3.0f)));
	DirectionalArray->CaptureBounds->SetBoxExtent(FVector(10.0f, 100.0f, 50.0f));
	DirectionalArray->DirectionalArraySpacingMeters = 1.0f;
	DirectionalArray->bFilterByClearance = false;
	DirectionalArray->bShowProbePreview = false;

	TestTrue(
		TEXT("Directional Array reuses the serialized SimpleSweep enum value"),
		DirectionalArray->CapturePattern == EUESplattingCaptureVolumePattern::SimpleSweep);
	TestEqual(
		TEXT("Actor scale produces a 5 by 4 camera wall at one-meter spacing"),
		DirectionalArray->GetRequestedDirectionalArrayStationCount(),
		20);

	TArray<FUESplattingCaptureView> Views;
	FUESplattingCaptureCoverageStats CoverageStats;
	const int32 StationCount = DirectionalArray->GenerateCaptureViews(Views, &CoverageStats);
	TestEqual(TEXT("Every directional-array candidate is accepted without clearance filtering"), StationCount, 20);
	TestEqual(TEXT("Directional Array exports exactly one image per camera origin"), Views.Num(), 20);
	TestEqual(TEXT("Coverage reports every planar candidate"), CoverageStats.CandidateStationCount, 20);
	TestEqual(TEXT("No camera is rejected when clearance filtering is disabled"), CoverageStats.ClearanceRejectedStationCount, 0);

	const FTransform ArrayTransform = DirectionalArray->CaptureBounds->GetComponentTransform();
	const FVector ExpectedForward = ArrayTransform.GetRotation().GetForwardVector();
	TSet<int32> StationIndices;
	double MinimumLocalY = DBL_MAX;
	double MaximumLocalY = -DBL_MAX;
	double MinimumLocalZ = DBL_MAX;
	double MaximumLocalZ = -DBL_MAX;
	for (const FUESplattingCaptureView& View : Views)
	{
		const FVector LocalCamera = ArrayTransform.InverseTransformPosition(View.Transform.GetLocation());
		if (!FMath::IsNearlyZero(LocalCamera.X, 0.001f))
		{
			AddError(FString::Printf(TEXT("Directional camera %s left the local Y/Z plane."), *View.DebugName));
			break;
		}
		if (FVector::DotProduct(View.Transform.GetRotation().GetForwardVector(), ExpectedForward) < 0.9999f)
		{
			AddError(FString::Printf(TEXT("Directional camera %s does not share the actor's +X direction."), *View.DebugName));
			break;
		}
		if (!FMath::IsNearlyEqual(View.HorizontalFieldOfView, 90.0f))
		{
			AddError(FString::Printf(TEXT("Directional camera %s did not retain the configured FOV."), *View.DebugName));
			break;
		}
		if (View.CaptureGroupKind != TEXT("directional_array"))
		{
			AddError(FString::Printf(TEXT("Directional camera %s is missing directional-array metadata."), *View.DebugName));
			break;
		}

		StationIndices.Add(View.StationIndex);
		MinimumLocalY = FMath::Min(MinimumLocalY, LocalCamera.Y);
		MaximumLocalY = FMath::Max(MaximumLocalY, LocalCamera.Y);
		MinimumLocalZ = FMath::Min(MinimumLocalZ, LocalCamera.Z);
		MaximumLocalZ = FMath::Max(MaximumLocalZ, LocalCamera.Z);
	}
	TestEqual(TEXT("Each directional camera has a distinct station identity"), StationIndices.Num(), 20);
	TestTrue(TEXT("Camera wall spans the authored local width"), FMath::IsNearlyEqual(MinimumLocalY, -100.0, 0.001) && FMath::IsNearlyEqual(MaximumLocalY, 100.0, 0.001));
	TestTrue(TEXT("Camera wall spans the authored local height"), FMath::IsNearlyEqual(MinimumLocalZ, -50.0, 0.001) && FMath::IsNearlyEqual(MaximumLocalZ, 50.0, 0.001));

	DirectionalArray->CaptureBounds->SetBoxExtent(FVector(10.0f, 115.0f, 60.0f));
	TestEqual(
		TEXT("Non-divisible dimensions add stations instead of exceeding requested spacing"),
		DirectionalArray->GetRequestedDirectionalArrayStationCount(),
		30);

	World->DestroyActor(DirectionalArray);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureVolumeFocusedDetailTest,
	"UESplatting.Capture.Volume.FocusedDetail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureVolumeFocusedDetailTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AUESplattingCaptureVolume* RoomCoverage = World->SpawnActor<AUESplattingCaptureVolume>(
		AUESplattingCaptureVolume::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	AUESplattingCaptureVolume* DetailRegion = World->SpawnActor<AUESplattingCaptureVolume>(
		AUESplattingCaptureVolume::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!TestNotNull(TEXT("Room Coverage can be spawned"), RoomCoverage)
		|| !TestNotNull(TEXT("Focused detail region can be spawned"), DetailRegion))
	{
		return false;
	}

	DetailRegion->ConfigureAsFocusedDetailRegion();
	DetailRegion->RoomCoverageVolume = RoomCoverage;
	// 90 degrees of field of view is not 90 degrees of overlap. Adjacent Standard8
	// horizon views sit 90 degrees apart in yaw, so their frusta meet at a boundary
	// and share no area. Same-station overlap is incidental, not designed.
	TestTrue(TEXT("Room Coverage defaults to a 90-degree horizontal field of view"), FMath::IsNearlyEqual(RoomCoverage->GetResolvedHorizontalFieldOfView(), 90.0f));
	TestTrue(TEXT("Focused detail inherits the Room Coverage FOV"), FMath::IsNearlyEqual(DetailRegion->GetResolvedHorizontalFieldOfView(), 90.0f));
	RoomCoverage->HorizontalFieldOfView = 82.0f;
	TestTrue(TEXT("Focused detail tracks an intentional Room Coverage FOV change"), FMath::IsNearlyEqual(DetailRegion->GetResolvedHorizontalFieldOfView(), 82.0f));
	RoomCoverage->HorizontalFieldOfView = 90.0f;
	// Keep this fallback test isolated from whatever collision happens to be in
	// the project's current editor level.
	DetailRegion->SetActorLocation(FVector(10000000.0, 10000000.0, 10000000.0));
	DetailRegion->bFilterByClearance = false;
	DetailRegion->bShowProbePreview = false;
	TestTrue(
		TEXT("Focused detail is an explicit capture mode"),
		DetailRegion->CapturePattern == EUESplattingCaptureVolumePattern::FocusedDetail);
	DetailRegion->DetailCaptureProfile = EUESplattingDetailCaptureProfile::Low;
	TestEqual(TEXT("Low requests 48 translated camera origins"), DetailRegion->GetRequestedDetailCandidateCount(), 48);
	DetailRegion->DetailCaptureProfile = EUESplattingDetailCaptureProfile::Medium;
	TestEqual(TEXT("Medium requests 144 translated camera origins"), DetailRegion->GetRequestedDetailCandidateCount(), 144);
	DetailRegion->DetailCaptureProfile = EUESplattingDetailCaptureProfile::High;
	TestEqual(TEXT("High requests 256 translated camera origins"), DetailRegion->GetRequestedDetailCandidateCount(), 256);
	DetailRegion->DetailCaptureProfile = EUESplattingDetailCaptureProfile::Ultra;
	TestEqual(TEXT("Ultra requests 480 translated camera origins"), DetailRegion->GetRequestedDetailCandidateCount(), 480);

	DetailRegion->DetailCaptureProfile = EUESplattingDetailCaptureProfile::Medium;
	TArray<FUESplattingCaptureView> Views;
	FUESplattingCaptureCoverageStats CoverageStats;
	const int32 StationCount = DetailRegion->GenerateCaptureViews(Views, &CoverageStats);
	TestEqual(TEXT("Medium retains the complete shell when collision evidence is unavailable"), StationCount, 144);
	TestEqual(TEXT("Medium emits two target-facing views per origin"), Views.Num(), 288);
	TestEqual(TEXT("Coverage reports every shell candidate"), CoverageStats.CandidateStationCount, 144);
	TestFalse(TEXT("An empty test scene is reported as spatial fallback"), CoverageStats.bSceneAwareAssessmentAvailable);

	const FTransform TargetTransform = DetailRegion->CaptureBounds->GetComponentTransform();
	const FVector TargetExtent = DetailRegion->CaptureBounds->GetUnscaledBoxExtent();
	const FVector TargetCenter = TargetTransform.GetLocation();
	for (const FUESplattingCaptureView& View : Views)
	{
		if (!FMath::IsNearlyEqual(View.HorizontalFieldOfView, 90.0f))
		{
			AddError(FString::Printf(TEXT("Focused camera %s did not inherit the room's 90-degree FOV."), *View.DebugName));
			break;
		}
		const FVector LocalCamera = TargetTransform.InverseTransformPosition(View.Transform.GetLocation());
		const bool bOutsideTarget = FMath::Abs(LocalCamera.X) > TargetExtent.X
			|| FMath::Abs(LocalCamera.Y) > TargetExtent.Y
			|| FMath::Abs(LocalCamera.Z) > TargetExtent.Z;
		if (!bOutsideTarget)
		{
			AddError(FString::Printf(TEXT("Focused camera %s was generated inside the target box."), *View.DebugName));
			break;
		}

		const FVector TowardCenter = (TargetCenter - View.Transform.GetLocation()).GetSafeNormal();
		const FVector Forward = View.Transform.GetRotation().GetForwardVector();
		if (FVector::DotProduct(Forward, TowardCenter) <= 0.0f)
		{
			AddError(FString::Printf(TEXT("Focused camera %s does not face the target."), *View.DebugName));
			break;
		}
		if (View.CaptureGroupId != DetailRegion->GetCaptureGroupId()
			|| View.CaptureGroupKind != TEXT("focused_detail"))
		{
			AddError(FString::Printf(TEXT("Focused camera %s is missing capture-group metadata."), *View.DebugName));
			break;
		}
	}

	World->DestroyActor(DetailRegion);
	World->DestroyActor(RoomCoverage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureLinkedVolumeSetTest,
	"UESplatting.Capture.Volume.LinkedCaptureSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureLinkedVolumeSetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	const auto SpawnVolume = [World, &SpawnParameters]()
	{
		return World->SpawnActor<AUESplattingCaptureVolume>(
			AUESplattingCaptureVolume::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	};

	AUESplattingCaptureVolume* Room = SpawnVolume();
	AUESplattingCaptureVolume* OtherRoom = SpawnVolume();
	AUESplattingCaptureVolume* DetailA = SpawnVolume();
	AUESplattingCaptureVolume* DetailB = SpawnVolume();
	AUESplattingCaptureVolume* OtherDetail = SpawnVolume();
	AUESplattingCaptureVolume* UnlinkedDetail = SpawnVolume();
	const bool bSpawnedAll = Room && OtherRoom && DetailA && DetailB && OtherDetail && UnlinkedDetail;
	if (!TestTrue(TEXT("Capture-set test actors can be spawned"), bSpawnedAll))
	{
		return false;
	}

	DetailA->ConfigureAsFocusedDetailRegion();
	DetailB->ConfigureAsFocusedDetailRegion();
	OtherDetail->ConfigureAsFocusedDetailRegion();
	UnlinkedDetail->ConfigureAsFocusedDetailRegion();
	DetailA->RoomCoverageVolume = Room;
	DetailB->RoomCoverageVolume = Room;
	OtherDetail->RoomCoverageVolume = OtherRoom;

	TArray<AUESplattingCaptureVolume*> LinkedDetails;
	Room->GetLinkedFocusedDetailRegions(LinkedDetails);
	TestEqual(TEXT("Room discovers exactly its two linked detail regions"), LinkedDetails.Num(), 2);
	TestTrue(TEXT("Room discovers detail A"), LinkedDetails.Contains(DetailA));
	TestTrue(TEXT("Room discovers detail B"), LinkedDetails.Contains(DetailB));

	TArray<AUESplattingCaptureVolume*> CaptureSet;
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet({Room}, CaptureSet);
	TestEqual(TEXT("Exporting the room resolves one room plus two details"), CaptureSet.Num(), 3);
	TestTrue(TEXT("Room export includes the room"), CaptureSet.Contains(Room));
	TestTrue(TEXT("Room export includes detail A"), CaptureSet.Contains(DetailA));
	TestTrue(TEXT("Room export includes detail B"), CaptureSet.Contains(DetailB));
	TestFalse(TEXT("Room export excludes another room"), CaptureSet.Contains(OtherRoom));
	TestFalse(TEXT("Room export excludes details linked elsewhere"), CaptureSet.Contains(OtherDetail));

	AUESplattingCaptureVolume::ExpandCaptureVolumeSet({DetailA}, CaptureSet);
	TestEqual(TEXT("Exporting one linked detail resolves its complete capture set"), CaptureSet.Num(), 3);
	TestTrue(TEXT("Linked detail export resolves its room as primary"),
		AUESplattingCaptureVolume::FindPrimaryCaptureVolume(CaptureSet) == Room);

	AUESplattingCaptureVolume::ExpandCaptureVolumeSet({Room, DetailA, DetailB}, CaptureSet);
	TestEqual(TEXT("Explicitly selecting linked actors does not duplicate them"), CaptureSet.Num(), 3);

	AUESplattingCaptureVolume::ExpandCaptureVolumeSet({UnlinkedDetail}, CaptureSet);
	TestEqual(TEXT("An unlinked detail region remains a valid standalone capture"), CaptureSet.Num(), 1);
	TestTrue(TEXT("Standalone detail export retains that actor"), CaptureSet.Contains(UnlinkedDetail));

	UnlinkedDetail->RoomCoverageVolume = DetailA;
	TestNull(TEXT("A focused-detail actor is not accepted as a room link"), UnlinkedDetail->GetLinkedRoomCoverageVolume());
	AUESplattingCaptureVolume::ExpandCaptureVolumeSet({UnlinkedDetail}, CaptureSet);
	TestEqual(TEXT("An invalid room link cannot pull an unrelated capture set"), CaptureSet.Num(), 1);

	World->DestroyActor(UnlinkedDetail);
	World->DestroyActor(OtherDetail);
	World->DestroyActor(DetailB);
	World->DestroyActor(DetailA);
	World->DestroyActor(OtherRoom);
	World->DestroyActor(Room);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureGroupedStationIdentityTest,
	"UESplatting.Capture.Export.GroupedStationIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureGroupedStationIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FUESplattingCaptureView> Views;
	const auto AddView = [&Views](const TCHAR* GroupId, int32 LocalStationIndex, const FVector& Location)
	{
		FUESplattingCaptureView& View = Views.AddDefaulted_GetRef();
		View.CaptureGroupId = GroupId;
		View.StationIndex = LocalStationIndex;
		View.Transform.SetLocation(Location);
	};
	AddView(TEXT("Room"), 0, FVector(0.0, 0.0, 0.0));
	AddView(TEXT("Room"), 0, FVector(0.0, 0.0, 0.0));
	AddView(TEXT("DeskDetail"), 0, FVector(100.0, 0.0, 0.0));
	AddView(TEXT("DeskDetail"), 0, FVector(100.0, 0.0, 0.0));
	AddView(TEXT("DeskDetail"), 1, FVector(200.0, 0.0, 0.0));

	const int32 StationCount = UUESplattingDatasetExporter::NormalizeCaptureStationIndices(Views);
	TestEqual(TEXT("Group-local ids normalize into three distinct stations"), StationCount, 3);
	TestEqual(TEXT("Room view one keeps station zero"), Views[0].StationIndex, 0);
	TestEqual(TEXT("Room view two shares station zero"), Views[1].StationIndex, 0);
	TestEqual(TEXT("Detail local zero becomes a distinct global station"), Views[2].StationIndex, 1);
	TestEqual(TEXT("Detail views at local zero stay grouped"), Views[3].StationIndex, 1);
	TestEqual(TEXT("Detail local one becomes global station two"), Views[4].StationIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingCaptureTransformsIntrinsicsContractTest,
	"UESplatting.Capture.Export.IntrinsicsContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUESplattingCaptureTransformsIntrinsicsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FUESplattingDatasetExportSettings Settings;
	Settings.ImageWidth = 1920;
	Settings.ImageHeight = 1080;
	TestFalse(TEXT("Collision seed generation is opt-in by default"), Settings.bGenerateTracePointCloud);

	const auto ParseTransforms = [this](const FString& JsonText, TSharedPtr<FJsonObject>& OutRoot)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return TestTrue(TEXT("Generated transforms JSON parses"), FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid());
	};

	TArray<FUESplattingCaptureView> UniformViews;
	UniformViews.AddDefaulted(2);
	UniformViews[0].HorizontalFieldOfView = 90.0f;
	UniformViews[1].HorizontalFieldOfView = 90.0f;
	TSharedPtr<FJsonObject> UniformRoot;
	if (!ParseTransforms(UUESplattingDatasetExporter::BuildTransformsJsonForAutomationTests(UniformViews, Settings), UniformRoot))
	{
		return false;
	}
	TestTrue(TEXT("Uniform lenses publish top-level fl_x"), UniformRoot->HasField(TEXT("fl_x")));
	TestTrue(TEXT("Uniform lenses publish top-level camera_angle_x"), UniformRoot->HasField(TEXT("camera_angle_x")));
	TestTrue(TEXT("A 90-degree 1920-wide image has 960px focal length"), FMath::IsNearlyEqual(UniformRoot->GetNumberField(TEXT("fl_x")), 960.0, 1.e-6));
	TestFalse(TEXT("Camera-only transforms omit ply_file_path"), UniformRoot->HasField(TEXT("ply_file_path")));

	Settings.bGenerateTracePointCloud = true;
	TSharedPtr<FJsonObject> SeededRoot;
	if (!ParseTransforms(UUESplattingDatasetExporter::BuildTransformsJsonForAutomationTests(UniformViews, Settings, true), SeededRoot))
	{
		return false;
	}
	TestEqual(TEXT("A generated optional seed is referenced by transforms.json"), SeededRoot->GetStringField(TEXT("ply_file_path")), FString(TEXT("sparse_pc.ply")));
	Settings.bGenerateTracePointCloud = false;

	TArray<FUESplattingCaptureView> MixedViews = UniformViews;
	MixedViews[1].HorizontalFieldOfView = 60.0f;
	TSharedPtr<FJsonObject> MixedRoot;
	if (!ParseTransforms(UUESplattingDatasetExporter::BuildTransformsJsonForAutomationTests(MixedViews, Settings), MixedRoot))
	{
		return false;
	}
	TestFalse(TEXT("Mixed lenses omit top-level fl_x so Nerfstudio reads each frame"), MixedRoot->HasField(TEXT("fl_x")));
	TestFalse(TEXT("Mixed lenses omit top-level fl_y so Nerfstudio reads each frame"), MixedRoot->HasField(TEXT("fl_y")));
	TestFalse(TEXT("Mixed lenses omit a misleading global camera_angle_x"), MixedRoot->HasField(TEXT("camera_angle_x")));
	const TArray<TSharedPtr<FJsonValue>>& MixedFrames = MixedRoot->GetArrayField(TEXT("frames"));
	TestEqual(TEXT("Both mixed-lens frames are serialized"), MixedFrames.Num(), 2);
	if (MixedFrames.Num() == 2)
	{
		const double RoomFocal = MixedFrames[0]->AsObject()->GetNumberField(TEXT("fl_x"));
		const double DetailFocal = MixedFrames[1]->AsObject()->GetNumberField(TEXT("fl_x"));
		TestTrue(TEXT("Room frame retains its 90-degree focal length"), FMath::IsNearlyEqual(RoomFocal, 960.0, 1.e-6));
		TestTrue(TEXT("60-degree control frame retains its own focal length"), FMath::IsNearlyEqual(DetailFocal, 1662.7687752661222, 1.e-6));
		TestNotEqual(TEXT("Mixed frames remain distinct intrinsics"), RoomFocal, DetailFocal);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
