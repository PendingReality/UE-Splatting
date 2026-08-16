// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GaussianDataTypes.h"
#include "GameFramework/Actor.h"
#include "GaussianSplatActor.generated.h"

class UGaussianSplatAsset;
class UGaussianSplatComponent;

/**
 * Simple actor for placing Gaussian splats in a level or loading one from disk at runtime.
 */
UCLASS(BlueprintType, Blueprintable)
class UESPLATTING_API AGaussianSplatActor : public AActor
{
	GENERATED_BODY()

public:
	AGaussianSplatActor();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	//~ End AActor Interface

	/** Set the Gaussian splat asset rendered by this actor's component. */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting")
	void SetSplatAsset(UGaussianSplatAsset* NewAsset);

	/** Get the Gaussian splat asset rendered by this actor's component. */
	UFUNCTION(BlueprintPure, Category = "Gaussian Splatting")
	UGaussianSplatAsset* GetSplatAsset() const;

	/** Get the number of splats currently assigned to this actor's component. */
	UFUNCTION(BlueprintPure, Category = "Gaussian Splatting")
	int32 GetSplatCount() const;

	/** Load RuntimeSplatFile. */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting|Runtime Import")
	void LoadRuntimeSplat();

	/** Load a supported splat file into this actor's component. */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting|Runtime Import")
	void LoadSplatFromFileAsync(const FString& FilePath);

	/** Cancel the current runtime splat load request. */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting|Runtime Import")
	void CancelRuntimeSplatLoad();

	/** True while this actor's component is waiting on a runtime splat load. */
	UFUNCTION(BlueprintPure, Category = "Gaussian Splatting|Runtime Import")
	bool IsRuntimeSplatLoadInProgress() const;

	/** The Gaussian splat component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gaussian Splatting")
	TObjectPtr<UGaussianSplatComponent> GaussianSplatComponent;

	/** Optional disk file to load at runtime. Supports the registered UESplatting runtime decoders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Runtime Import", meta = (FilePathFilter = "Supported splats (*.ply;*.spz)|*.ply;*.spz|All files (*.*)|*.*"))
	FFilePath RuntimeSplatFile;

	/** Load RuntimeSplatFile automatically when play begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Runtime Import")
	bool bLoadRuntimeSplatOnBeginPlay = false;

};
