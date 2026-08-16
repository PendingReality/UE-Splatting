// SPDX-License-Identifier: MIT

#include "UESplattingCaptureTimeStep.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Graph/MovieGraphPipeline.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/CoreDelegates.h"
#endif
#include "MoviePipelineQueue.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogUESplattingCaptureTimeStep, Log, All);

namespace
{
	bool GUESplattingSceneFreezeRequested = false;
	const FName FullTickWhenPausedPropertyName(TEXT("bShouldPerformFullTickWhenPaused"));

	FBoolProperty* FindFullTickWhenPausedProperty(const APlayerController* PlayerController)
	{
		return PlayerController
			? FindFProperty<FBoolProperty>(PlayerController->GetClass(), FullTickWhenPausedPropertyName)
			: nullptr;
	}

	bool ReadFullTickWhenPaused(const APlayerController* PlayerController)
	{
		const FBoolProperty* Property = FindFullTickWhenPausedProperty(PlayerController);
		return Property && Property->GetPropertyValue_InContainer(PlayerController);
	}

	bool WriteFullTickWhenPaused(APlayerController* PlayerController, bool bValue)
	{
		FBoolProperty* Property = FindFullTickWhenPausedProperty(PlayerController);
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(PlayerController, bValue);
		return true;
	}

#if WITH_DEV_AUTOMATION_TESTS
	FUESplattingCaptureFrameObserved GUESplattingCaptureFrameObserved;
	FUESplattingSceneFreezeObserved GUESplattingSceneFreezeApplied;
	FUESplattingSceneFreezeObserved GUESplattingSceneFreezeRestored;
#endif
}

void UUESplattingCaptureTimeStep::SetSceneFreezeRequested(bool bRequested)
{
	GUESplattingSceneFreezeRequested = bRequested;
}

#if WITH_DEV_AUTOMATION_TESTS

FUESplattingCaptureFrameObserved& UUESplattingCaptureTimeStep::OnFrameObservedForTesting()
{
	return GUESplattingCaptureFrameObserved;
}

FUESplattingSceneFreezeObserved& UUESplattingCaptureTimeStep::OnSceneFreezeAppliedForTesting()
{
	return GUESplattingSceneFreezeApplied;
}

FUESplattingSceneFreezeObserved& UUESplattingCaptureTimeStep::OnSceneFreezeRestoredForTesting()
{
	return GUESplattingSceneFreezeRestored;
}
#endif

void UUESplattingCaptureTimeStep::Initialize()
{
	Super::Initialize();
	bSceneFreezeRequested = GUESplattingSceneFreezeRequested;
#if WITH_DEV_AUTOMATION_TESTS
	EndFrameDelegateHandle = FCoreDelegates::OnEndFrame.AddUObject(this, &UUESplattingCaptureTimeStep::ObserveCameraAtEndFrame);
#endif
}

void UUESplattingCaptureTimeStep::Shutdown()
{
	RestoreSceneFreeze();
#if WITH_DEV_AUTOMATION_TESTS
	FCoreDelegates::OnEndFrame.Remove(EndFrameDelegateHandle);
	EndFrameDelegateHandle.Reset();
#endif
	Super::Shutdown();
}

bool UUESplattingCaptureTimeStep::ApplySceneFreeze()
{
	if (!bSceneFreezeRequested)
	{
		return true;
	}
	if (bSceneFreezeAttempted)
	{
		return bSceneFreezeApplied;
	}
	bSceneFreezeAttempted = true;

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	AWorldSettings* WorldSettings = World ? World->GetWorldSettings() : nullptr;
	APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	if (!World || !PlayerController || !WorldSettings || !PlayerState || !FindFullTickWhenPausedProperty(PlayerController))
	{
		UE_LOG(LogUESplattingCaptureTimeStep, Error, TEXT("Cannot freeze the MRQ scene because its render world has no complete local player pause state."));
		return false;
	}

	SceneFreezeWorld = World;
	SceneFreezePlayerController = PlayerController;
	PreviousPauserPlayerState = WorldSettings->GetPauserPlayerState();
	bWorldWasPaused = World->IsPaused();
	bPreviousFullCameraTick = ReadFullTickWhenPaused(PlayerController);
	WriteFullTickWhenPaused(PlayerController, true);

	if (!WorldSettings->GetPauserPlayerState())
	{
		WorldSettings->SetPauserPlayerState(PlayerState);
		AppliedPauserPlayerState = PlayerState;
		bModifiedPauseState = true;
	}

	bSceneFreezeApplied = World->IsPaused();
	if (!bSceneFreezeApplied)
	{
		RestoreSceneFreeze();
		UE_LOG(LogUESplattingCaptureTimeStep, Error, TEXT("Unreal did not enter its paused world tick after UESplatting requested scene freeze."));
		return false;
	}

#if WITH_DEV_AUTOMATION_TESTS
	FUESplattingSceneFreezeObservation Observation;
	Observation.bWorldWasPaused = bWorldWasPaused;
	Observation.bWorldIsPaused = true;
	Observation.bFullCameraTickWasEnabled = bPreviousFullCameraTick;
	Observation.bFullCameraTickIsEnabled = ReadFullTickWhenPaused(PlayerController);
	GUESplattingSceneFreezeApplied.Broadcast(World, Observation);
#endif
	return true;
}

void UUESplattingCaptureTimeStep::RestoreSceneFreeze()
{
	if (!bSceneFreezeAttempted)
	{
		return;
	}

	UWorld* World = SceneFreezeWorld.Get();
	APlayerController* PlayerController = SceneFreezePlayerController.Get();
	if (PlayerController)
	{
		WriteFullTickWhenPaused(PlayerController, bPreviousFullCameraTick);
	}

	if (World && bModifiedPauseState)
	{
		if (AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			APlayerState* CurrentPauser = WorldSettings->GetPauserPlayerState();
			if (CurrentPauser == AppliedPauserPlayerState.Get())
			{
				WorldSettings->SetPauserPlayerState(PreviousPauserPlayerState.Get());
			}
			else
			{
				UE_LOG(LogUESplattingCaptureTimeStep, Warning, TEXT("The MRQ world's pauser changed during capture; UESplatting preserved the newer external pause owner."));
			}
		}
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (World)
	{
		FUESplattingSceneFreezeObservation Observation;
		Observation.bWorldWasPaused = bWorldWasPaused;
		Observation.bWorldIsPaused = World->IsPaused();
		Observation.bFullCameraTickWasEnabled = bPreviousFullCameraTick;
		Observation.bFullCameraTickIsEnabled = ReadFullTickWhenPaused(PlayerController);
		GUESplattingSceneFreezeRestored.Broadcast(World, Observation);
	}
#endif

	SceneFreezeWorld.Reset();
	SceneFreezePlayerController.Reset();
	PreviousPauserPlayerState.Reset();
	AppliedPauserPlayerState.Reset();
	bSceneFreezeAttempted = false;
	bSceneFreezeApplied = false;
	bModifiedPauseState = false;
}

void UUESplattingCaptureTimeStep::TickProducingFrames()
{
	if (!ApplySceneFreeze())
	{
		if (UMovieGraphPipeline* Pipeline = GetOwningGraph())
		{
			Pipeline->RequestShutdown(true);
		}
		return;
	}

	Super::TickProducingFrames();

	UMovieGraphPipeline* Pipeline = GetOwningGraph();
	if (!Pipeline || CurrentTimeStepData.bDiscardOutput || !CurrentTimeStepData.bIsFirstTemporalSampleForFrame)
	{
		return;
	}

	const int32 ShotIndex = Pipeline->GetCurrentShotIndex();
	const TArray<TObjectPtr<UMoviePipelineExecutorShot>>& ActiveShots = Pipeline->GetActiveShotList();
	if (ActiveShots.IsValidIndex(ShotIndex) && ActiveShots[ShotIndex])
	{
		// The core time step only updates the sequence time controller. Evaluate this
		// known-pose frame immediately, then hold it so the world tick cannot restore
		// the previous playback position before MRQ renders at end-of-frame.
		Pipeline->GetDataSourceInstance()->JumpDataSource(ActiveShots[ShotIndex]->ShotInfo.CurrentTimeInRoot);
		Pipeline->GetDataSourceInstance()->PauseDataSource();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UUESplattingCaptureTimeStep::ObserveCameraAtEndFrame()
{
	if (CurrentTimeStepData.bDiscardOutput || !CurrentTimeStepData.bIsFirstTemporalSampleForFrame)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		return;
	}

	const FMinimalViewInfo CachedView = CameraManager->GetCameraCacheView();
	FUESplattingCaptureFrameObservation Observation;
	Observation.OutputFrameNumber = CurrentTimeStepData.OutputFrameNumber;
	Observation.CameraTransform = FTransform(CachedView.Rotation.Quaternion(), CachedView.Location);
	Observation.HorizontalFieldOfView = CachedView.FOV;
	GUESplattingCaptureFrameObserved.Broadcast(Observation);
}
#endif

FMovieGraphTimeStepData UUESplattingCaptureTimeStep::GetCalculatedTimeData() const
{
	FMovieGraphTimeStepData TimeData = Super::GetCalculatedTimeData();
	TimeData.bIsCameraCut = TimeData.bIsFirstTemporalSampleForFrame;
	return TimeData;
}
