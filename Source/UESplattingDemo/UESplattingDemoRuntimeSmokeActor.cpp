// SPDX-License-Identifier: MIT

#include "UESplattingDemoRuntimeSmokeActor.h"

#include "GaussianSplatComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogUESplattingDemo, Log, All);

void AUESplattingDemoRuntimeSmokeActor::BeginPlay()
{
	Super::BeginPlay();

	if (!FParse::Param(FCommandLine::Get(), TEXT("UESplattingRuntimeSmoke")))
	{
		return;
	}

	if (!GaussianSplatComponent)
	{
		UE_LOG(LogUESplattingDemo, Error, TEXT("UESPLATTING_RUNTIME_SMOKE_FAIL: actor has no splat component"));
		FPlatformMisc::RequestExitWithStatus(false, 1);
		return;
	}

	GaussianSplatComponent->OnRuntimeSplatLoadSucceeded.AddDynamic(
		this,
		&AUESplattingDemoRuntimeSmokeActor::HandleRuntimeLoadSucceeded
	);
	GaussianSplatComponent->OnRuntimeSplatLoadFailed.AddDynamic(
		this,
		&AUESplattingDemoRuntimeSmokeActor::HandleRuntimeLoadFailed
	);

	const FString FixturePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("samples/Data/UESplatting_Demo.ply"))
	);
	UE_LOG(LogUESplattingDemo, Display, TEXT("UESplatting runtime smoke loading '%s'"), *FixturePath);
	LoadSplatFromFileAsync(FixturePath);
}

void AUESplattingDemoRuntimeSmokeActor::HandleRuntimeLoadSucceeded(
	UGaussianSplatAsset* Asset,
	int32 SplatCount
)
{
	if (!Asset || SplatCount <= 0)
	{
		HandleRuntimeLoadFailed(TEXT("loader reported success without a valid asset"));
		return;
	}

	UE_LOG(
		LogUESplattingDemo,
		Display,
		TEXT("UESPLATTING_RUNTIME_SMOKE_PASS: loaded %d splats"),
		SplatCount
	);
	FPlatformMisc::RequestExitWithStatus(false, 0);
}

void AUESplattingDemoRuntimeSmokeActor::HandleRuntimeLoadFailed(const FString& ErrorMessage)
{
	UE_LOG(LogUESplattingDemo, Error, TEXT("UESPLATTING_RUNTIME_SMOKE_FAIL: %s"), *ErrorMessage);
	FPlatformMisc::RequestExitWithStatus(false, 1);
}
