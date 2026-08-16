// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "GaussianDataTypes.h"
#include "GaussianSplatComponent.generated.h"

class UGaussianSplatAsset;
class FGaussianSplatSceneProxy;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGaussianSplatRuntimeLoadSucceeded, UGaussianSplatAsset*, Asset, int32, SplatCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGaussianSplatRuntimeLoadFailed, const FString&, ErrorMessage);

/**
 * Component for rendering Gaussian Splatting assets in the scene
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent), hidecategories = (Collision, Physics, Navigation))
class UESPLATTING_API UGaussianSplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UGaussianSplatComponent(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface

	/** Set the Gaussian Splat asset to render */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting")
	void SetSplatAsset(UGaussianSplatAsset* NewAsset);

	/** Get the currently assigned asset */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting")
	UGaussianSplatAsset* GetSplatAsset() const { return SplatAsset; }

	/** Get the number of splats being rendered */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting")
	int32 GetSplatCount() const;

	/**
	 * Decode a supported splat file on a worker thread, then create and assign a transient
	 * UGaussianSplatAsset on the game thread. Supports the runtime decoder registry formats.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting|Runtime Import")
	void LoadSplatFromFileAsync(const FString& FilePath);

	/** Cancels the current async load request. The worker may finish decoding, but its result will be ignored. */
	UFUNCTION(BlueprintCallable, Category = "Gaussian Splatting|Runtime Import")
	void CancelRuntimeSplatLoad();

	/** True while the newest runtime load request is pending. */
	UFUNCTION(BlueprintPure, Category = "Gaussian Splatting|Runtime Import")
	bool IsRuntimeSplatLoadInProgress() const { return bRuntimeSplatLoadInProgress; }

public:
	/** The Gaussian Splat asset to render */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting")
	TObjectPtr<UGaussianSplatAsset> SplatAsset;

	/** Fired after a runtime splat file successfully loads and is assigned to this component. */
	UPROPERTY(BlueprintAssignable, Category = "Gaussian Splatting|Runtime Import")
	FGaussianSplatRuntimeLoadSucceeded OnRuntimeSplatLoadSucceeded;

	/** Fired when a runtime splat load fails or is canceled. */
	UPROPERTY(BlueprintAssignable, Category = "Gaussian Splatting|Runtime Import")
	FGaussianSplatRuntimeLoadFailed OnRuntimeSplatLoadFailed;

	/** Spherical Harmonic order to use for rendering (0-3). Higher = more color detail but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Quality", meta = (ClampMin = "0", ClampMax = "3"))
	int32 SHOrder = 3;

	/** Global opacity multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Rendering", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float OpacityScale = 1.0f;

	/** Scale multiplier for splat sizes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Rendering", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float SplatScale = 1.0f;

	/** Projected error threshold for the custom Cluster LOD selection.
	 *  Lower values = more conservative (keep detail longer, less LOD savings)
	 *  Higher values = more aggressive (switch to LOD sooner, better performance)
	 *  Uses projection-space units. ~0.03 ≈ 32 pixels at 1080p with 90° FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splatting|Performance", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float LODErrorThreshold = 0.03f;

protected:
	/** Called when the asset changes */
	void OnAssetChanged();

	/** Mark the render state as dirty */
	void MarkRenderStateDirty();

	/** Called when the asset's data changes (for example, Cluster LOD rebuilt/cleared). */
	void OnAssetDataChanged(class UGaussianSplatAsset* ChangedAsset);

	/** Subscribe to asset change notifications */
	void SubscribeToAssetChanges();

	/** Unsubscribe from asset change notifications */
	void UnsubscribeFromAssetChanges();

private:
	/** Delegate handle for asset change subscription */
	FDelegateHandle AssetChangedDelegateHandle;

	/** Keeps the most recent transient runtime asset alive while this component references it. */
	UPROPERTY(Transient)
	TObjectPtr<UGaussianSplatAsset> RuntimeLoadedSplatAsset;

	/** Monotonic request id used to ignore stale async completions. */
	int32 RuntimeSplatLoadRequestId = 0;

	/** Whether the newest runtime load request is still pending. */
	bool bRuntimeSplatLoadInProgress = false;
};
