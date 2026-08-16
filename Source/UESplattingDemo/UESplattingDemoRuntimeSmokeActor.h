// SPDX-License-Identifier: MIT

#pragma once

#include "GaussianSplatActor.h"
#include "UESplattingDemoRuntimeSmokeActor.generated.h"

class UGaussianSplatAsset;

/** Opt-in packaged-build validation for UESplatting's asynchronous file loader. */
UCLASS()
class UESPLATTINGDEMO_API AUESplattingDemoRuntimeSmokeActor : public AGaussianSplatActor
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleRuntimeLoadSucceeded(UGaussianSplatAsset* Asset, int32 SplatCount);

	UFUNCTION()
	void HandleRuntimeLoadFailed(const FString& ErrorMessage);
};
