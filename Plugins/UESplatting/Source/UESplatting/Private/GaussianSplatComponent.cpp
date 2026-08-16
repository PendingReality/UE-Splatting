// SPDX-License-Identifier: MIT

#include "GaussianSplatComponent.h"
#include "UESplattingLog.h"
#include "GaussianSplatAsset.h"
#include "GaussianSplatDecoder.h"
#include "GaussianSplatSceneProxy.h"
#include "GaussianSplatViewExtension.h"
#include "Async/Async.h"
#include "Engine/World.h"

UGaussianSplatComponent::UGaussianSplatComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bUseAsOccluder = false;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	Mobility = EComponentMobility::Movable;
}

#if WITH_EDITOR
void UGaussianSplatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, SplatAsset))
	{
		OnAssetChanged();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, SHOrder) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, OpacityScale) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, SplatScale) ||
			 PropertyName == GET_MEMBER_NAME_CHECKED(UGaussianSplatComponent, LODErrorThreshold))
	{
		MarkRenderStateDirty();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UGaussianSplatComponent::OnRegister()
{
	Super::OnRegister();

	if (SplatAsset)
	{
		SubscribeToAssetChanges();
	}
}

void UGaussianSplatComponent::OnUnregister()
{
	UnsubscribeFromAssetChanges();
	Super::OnUnregister();
}

FPrimitiveSceneProxy* UGaussianSplatComponent::CreateSceneProxy()
{
	if (!SplatAsset || !SplatAsset->IsValid())
	{
		return nullptr;
	}

	// Don't create proxy for preview worlds (Blueprint editor, etc.)
	UWorld* World = GetWorld();
	if (World)
	{
		EWorldType::Type WorldType = World->WorldType;
		if (WorldType == EWorldType::EditorPreview || WorldType == EWorldType::GamePreview)
		{
			return nullptr;
		}
	}

	return new FGaussianSplatSceneProxy(this);
}

FBoxSphereBounds UGaussianSplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (SplatAsset && SplatAsset->IsValid())
	{
		FBox LocalBox = SplatAsset->GetBounds();

		// Transform to world space
		FBox WorldBox = LocalBox.TransformBy(LocalToWorld);

		return FBoxSphereBounds(WorldBox);
	}

	// Return small default bounds if no asset
	return FBoxSphereBounds(FVector::ZeroVector, FVector(100.0f), 100.0f);
}

void UGaussianSplatComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	(void)OutMaterials;
	(void)bGetDebugMaterials;
}

void UGaussianSplatComponent::SetSplatAsset(UGaussianSplatAsset* NewAsset)
{
	if (SplatAsset != NewAsset)
	{
		// Unsubscribe from old asset
		UnsubscribeFromAssetChanges();

		SplatAsset = NewAsset;

		// Subscribe to new asset
		if (IsRegistered())
		{
			SubscribeToAssetChanges();
		}

		OnAssetChanged();
	}
}

int32 UGaussianSplatComponent::GetSplatCount() const
{
	return SplatAsset ? SplatAsset->GetSplatCount() : 0;
}

void UGaussianSplatComponent::LoadSplatFromFileAsync(const FString& FilePath)
{
	const FString TrimmedFilePath = FilePath.TrimStartAndEnd();
	const int32 RequestId = ++RuntimeSplatLoadRequestId;
	bRuntimeSplatLoadInProgress = true;

	if (TrimmedFilePath.IsEmpty())
	{
		bRuntimeSplatLoadInProgress = false;
		OnRuntimeSplatLoadFailed.Broadcast(TEXT("Runtime splat load failed: file path is empty"));
		return;
	}

	// Cheap game-thread sniff also initializes the decoder registry before worker-thread decode.
	if (!FGaussianSplatDecoderRegistry::CanDecodeFile(TrimmedFilePath))
	{
		bRuntimeSplatLoadInProgress = false;
		OnRuntimeSplatLoadFailed.Broadcast(FString::Printf(TEXT("Runtime splat load failed: unsupported or unreadable file '%s'"), *TrimmedFilePath));
		return;
	}

	TWeakObjectPtr<UGaussianSplatComponent> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, RequestId, TrimmedFilePath]()
	{
		TSharedPtr<FSplatDecodeResult, ESPMode::ThreadSafe> Decoded = MakeShared<FSplatDecodeResult, ESPMode::ThreadSafe>();
		FString ErrorMessage;
		bool bSuccess = FGaussianSplatDecoderRegistry::DecodeFile(TrimmedFilePath, *Decoded, ErrorMessage);

		if (bSuccess && Decoded->Splats.Num() == 0)
		{
			bSuccess = false;
			ErrorMessage = FString::Printf(TEXT("Runtime splat load decoded zero splats from '%s'"), *TrimmedFilePath);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, TrimmedFilePath, Decoded, ErrorMessage = MoveTemp(ErrorMessage), bSuccess]() mutable
		{
			UGaussianSplatComponent* Component = WeakThis.Get();
			if (!Component || Component->RuntimeSplatLoadRequestId != RequestId)
			{
				return;
			}

			Component->bRuntimeSplatLoadInProgress = false;

			if (!bSuccess)
			{
				const FString FinalError = ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Runtime splat load failed for '%s'"), *TrimmedFilePath)
					: ErrorMessage;
				Component->OnRuntimeSplatLoadFailed.Broadcast(FinalError);
				return;
			}

			UGaussianSplatAsset* NewAsset = NewObject<UGaussianSplatAsset>(Component, NAME_None, RF_Transient);
			if (!NewAsset)
			{
				Component->OnRuntimeSplatLoadFailed.Broadcast(TEXT("Runtime splat load failed: could not create transient Gaussian splat asset"));
				return;
			}

			NewAsset->SHBands = Decoded->SHBands;
			NewAsset->SourceFilePath = TrimmedFilePath;
			NewAsset->InitializeFromSplatData(Decoded->Splats, EGaussianQualityLevel::VeryHigh);

			if (!NewAsset->IsValid())
			{
				Component->OnRuntimeSplatLoadFailed.Broadcast(FString::Printf(TEXT("Runtime splat load failed: initialized asset is invalid for '%s'"), *TrimmedFilePath));
				return;
			}

			Component->RuntimeLoadedSplatAsset = NewAsset;
			Component->SetSplatAsset(NewAsset);
			Component->OnRuntimeSplatLoadSucceeded.Broadcast(NewAsset, NewAsset->GetSplatCount());
		});
	});
}

void UGaussianSplatComponent::CancelRuntimeSplatLoad()
{
	if (!bRuntimeSplatLoadInProgress)
	{
		return;
	}

	++RuntimeSplatLoadRequestId;
	bRuntimeSplatLoadInProgress = false;
	OnRuntimeSplatLoadFailed.Broadcast(TEXT("Runtime splat load canceled"));
}

void UGaussianSplatComponent::OnAssetChanged()
{
	UpdateBounds();
	MarkRenderStateDirty();
}

void UGaussianSplatComponent::MarkRenderStateDirty()
{
	MarkRenderDynamicDataDirty();

	if (IsRegistered())
	{
		Super::MarkRenderStateDirty();
	}
}

void UGaussianSplatComponent::OnAssetDataChanged(UGaussianSplatAsset* ChangedAsset)
{
	// Only respond if this is our asset
	if (ChangedAsset == SplatAsset)
	{
		UE_LOG(LogUESplatting, Log, TEXT("GaussianSplat: Asset data changed, recreating scene proxy"));

		UpdateBounds();

		// Recreate the scene proxy with updated asset data
		// This recreates the proxy with the current asset representation.
		MarkRenderStateDirty();
	}
}

void UGaussianSplatComponent::SubscribeToAssetChanges()
{
	if (SplatAsset && !AssetChangedDelegateHandle.IsValid())
	{
		AssetChangedDelegateHandle = SplatAsset->OnAssetChanged.AddUObject(this, &UGaussianSplatComponent::OnAssetDataChanged);
		UE_LOG(LogUESplatting, Verbose, TEXT("GaussianSplat: Subscribed to asset change notifications"));
	}
}

void UGaussianSplatComponent::UnsubscribeFromAssetChanges()
{
	if (SplatAsset && AssetChangedDelegateHandle.IsValid())
	{
		SplatAsset->OnAssetChanged.Remove(AssetChangedDelegateHandle);
		AssetChangedDelegateHandle.Reset();
		UE_LOG(LogUESplatting, Verbose, TEXT("GaussianSplat: Unsubscribed from asset change notifications"));
	}
}
