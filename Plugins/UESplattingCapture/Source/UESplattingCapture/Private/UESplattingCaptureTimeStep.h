// SPDX-License-Identifier: MIT

#pragma once

#include "Graph/MovieGraphLinearTimeStep.h"
#include "UESplattingCaptureTimeStep.generated.h"

class APlayerController;
class APlayerState;
class UWorld;

#if WITH_DEV_AUTOMATION_TESTS
struct FUESplattingCaptureFrameObservation
{
	int32 OutputFrameNumber = INDEX_NONE;
	FTransform CameraTransform = FTransform::Identity;
	float HorizontalFieldOfView = 0.0f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUESplattingCaptureFrameObserved, const FUESplattingCaptureFrameObservation&);

struct FUESplattingSceneFreezeObservation
{
	bool bWorldWasPaused = false;
	bool bWorldIsPaused = false;
	bool bFullCameraTickWasEnabled = false;
	bool bFullCameraTickIsEnabled = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FUESplattingSceneFreezeObserved, UWorld*, const FUESplattingSceneFreezeObservation&);
#endif

/** Linear MRQ time step for known-pose captures where the camera teleports every output frame. */
UCLASS()
class UUESplattingCaptureTimeStep : public UMovieGraphLinearTimeStep
{
	GENERATED_BODY()

public:
	/** Configures the single active UESplatting MRQ export before its time-step instance is created. */
	static void SetSceneFreezeRequested(bool bRequested);

	virtual void Initialize() override;
	virtual void Shutdown() override;
	virtual void TickProducingFrames() override;
	virtual FMovieGraphTimeStepData GetCalculatedTimeData() const override;

#if WITH_DEV_AUTOMATION_TESTS
	static FUESplattingCaptureFrameObserved& OnFrameObservedForTesting();
	static FUESplattingSceneFreezeObserved& OnSceneFreezeAppliedForTesting();
	static FUESplattingSceneFreezeObserved& OnSceneFreezeRestoredForTesting();
#endif

private:
	bool ApplySceneFreeze();
	void RestoreSceneFreeze();

#if WITH_DEV_AUTOMATION_TESTS
	void ObserveCameraAtEndFrame();
	FDelegateHandle EndFrameDelegateHandle;
#endif

	TWeakObjectPtr<UWorld> SceneFreezeWorld;
	TWeakObjectPtr<APlayerController> SceneFreezePlayerController;
	TWeakObjectPtr<APlayerState> PreviousPauserPlayerState;
	TWeakObjectPtr<APlayerState> AppliedPauserPlayerState;
	bool bSceneFreezeRequested = false;
	bool bSceneFreezeAttempted = false;
	bool bSceneFreezeApplied = false;
	bool bModifiedPauseState = false;
	bool bWorldWasPaused = false;
	bool bPreviousFullCameraTick = false;
};
