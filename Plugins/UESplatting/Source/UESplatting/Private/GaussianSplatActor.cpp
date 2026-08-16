// SPDX-License-Identifier: MIT

#include "GaussianSplatActor.h"
#include "GaussianSplatAsset.h"
#include "GaussianSplatComponent.h"

AGaussianSplatActor::AGaussianSplatActor()
{
	// Create the Gaussian Splat component as a default subobject
	GaussianSplatComponent = CreateDefaultSubobject<UGaussianSplatComponent>(TEXT("GaussianSplatComponent"));
	RootComponent = GaussianSplatComponent;
}

void AGaussianSplatActor::BeginPlay()
{
	Super::BeginPlay();

	if (bLoadRuntimeSplatOnBeginPlay)
	{
		LoadRuntimeSplat();
	}
}

void AGaussianSplatActor::SetSplatAsset(UGaussianSplatAsset* NewAsset)
{
	if (GaussianSplatComponent)
	{
		GaussianSplatComponent->SetSplatAsset(NewAsset);
	}
}

UGaussianSplatAsset* AGaussianSplatActor::GetSplatAsset() const
{
	return GaussianSplatComponent ? GaussianSplatComponent->GetSplatAsset() : nullptr;
}

int32 AGaussianSplatActor::GetSplatCount() const
{
	return GaussianSplatComponent ? GaussianSplatComponent->GetSplatCount() : 0;
}

void AGaussianSplatActor::LoadRuntimeSplat()
{
	LoadSplatFromFileAsync(RuntimeSplatFile.FilePath);
}

void AGaussianSplatActor::LoadSplatFromFileAsync(const FString& FilePath)
{
	if (GaussianSplatComponent)
	{
		GaussianSplatComponent->LoadSplatFromFileAsync(FilePath);
	}
}

void AGaussianSplatActor::CancelRuntimeSplatLoad()
{
	if (GaussianSplatComponent)
	{
		GaussianSplatComponent->CancelRuntimeSplatLoad();
	}
}

bool AGaussianSplatActor::IsRuntimeSplatLoadInProgress() const
{
	return GaussianSplatComponent && GaussianSplatComponent->IsRuntimeSplatLoadInProgress();
}
