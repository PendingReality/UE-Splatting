// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "UESplattingCaptureTimeStep.h"
#include "UESplattingDatasetExporter.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace UESplattingMovieRenderQueueTests
{
	struct FExportState
	{
		bool bComplete = false;
		FUESplattingDatasetExportResult Result;
		FString OutputDirectory;
		TArray<FUESplattingCaptureView> ExpectedViews;
		TMap<int32, FUESplattingCaptureFrameObservation> ObservedFrames;
		FDelegateHandle ObservationHandle;
		int32 ObservationCount = 0;
	};

	struct FFreezeWitnessFrame
	{
		bool bWorldPaused = false;
		double WorldTimeSeconds = 0.0;
		double RealTimeSeconds = 0.0;
		FTransform MotionTransform = FTransform::Identity;
		double MotionLastTickGameTime = -1.0;
		FTransform PhysicsTransform = FTransform::Identity;
		FVector PhysicsLinearVelocity = FVector::ZeroVector;
		double NiagaraLastTickGameTime = -1.0;
		int32 TimerFireCount = 0;
	};

	struct FFreezeExportState : FExportState
	{
		TWeakObjectPtr<UWorld> RenderWorld;
		TWeakObjectPtr<AActor> MotionActor;
		TWeakObjectPtr<URotatingMovementComponent> MotionComponent;
		TWeakObjectPtr<AStaticMeshActor> PhysicsActor;
		TWeakObjectPtr<UStaticMeshComponent> PhysicsComponent;
		TWeakObjectPtr<AActor> NiagaraActor;
		TWeakObjectPtr<UActorComponent> NiagaraComponent;
		FTransform InitialMotionTransform = FTransform::Identity;
		double InitialMotionLastTickGameTime = -1.0;
		FTransform InitialPhysicsTransform = FTransform::Identity;
		FVector InitialPhysicsLinearVelocity = FVector::ZeroVector;
		double InitialNiagaraLastTickGameTime = -1.0;
		double InitialWorldTimeSeconds = 0.0;
		FTimerHandle TimerHandle;
		int32 TimerFireCount = 0;
		TArray<FFreezeWitnessFrame> WitnessFrames;
		FString WitnessError;
		FUESplattingSceneFreezeObservation AppliedObservation;
		FUESplattingSceneFreezeObservation RestoredObservation;
		FDelegateHandle FreezeAppliedHandle;
		FDelegateHandle FreezeRestoredHandle;
		bool bFreezeAppliedObserved = false;
		bool bFreezeRestoredObserved = false;
		bool bWitnessesReady = false;
	};

	TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	bool InvokeObjectAndBoolFunction(UObject* Object, FName FunctionName, UObject* ObjectArgument, bool bBoolArgument)
	{
		UFunction* Function = Object ? Object->FindFunction(FunctionName) : nullptr;
		if (!Function)
		{
			return false;
		}

		TArray<uint8> Parameters;
		Parameters.SetNumZeroed(Function->ParmsSize);
		bool bSetObject = ObjectArgument == nullptr;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				ObjectProperty->SetObjectPropertyValue_InContainer(Parameters.GetData(), ObjectArgument);
				bSetObject = true;
			}
			else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				BoolProperty->SetPropertyValue_InContainer(Parameters.GetData(), bBoolArgument);
			}
		}
		if (!bSetObject)
		{
			return false;
		}
		Object->ProcessEvent(Function, Parameters.GetData());
		return true;
	}

	bool InitializeFreezeWitnesses(UWorld* World, const TSharedPtr<FFreezeExportState>& State)
	{
		if (!World || !State || State->bWitnessesReady)
		{
			return false;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* MotionActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(100000.0, 100000.0, 100000.0)),
			SpawnParameters);
		if (!MotionActor)
		{
			State->WitnessError = TEXT("Could not spawn the actor-motion witness in the MRQ render world.");
			return false;
		}
		MotionActor->SetActorHiddenInGame(true);
		USceneComponent* MotionRoot = NewObject<USceneComponent>(MotionActor, TEXT("UESplattingFreezeMotionRoot"));
		MotionActor->AddInstanceComponent(MotionRoot);
		MotionActor->SetRootComponent(MotionRoot);
		MotionRoot->RegisterComponent();
		URotatingMovementComponent* MotionComponent = NewObject<URotatingMovementComponent>(MotionActor, TEXT("UESplattingFreezeMotion"));
		MotionComponent->RotationRate = FRotator(0.0, 180.0, 0.0);
		MotionActor->AddInstanceComponent(MotionComponent);
		MotionComponent->SetUpdatedComponent(MotionRoot);
		MotionComponent->RegisterComponent();
		MotionComponent->SetComponentTickEnabled(true);

		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* PhysicsActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(101000.0, 100000.0, 100000.0)),
			SpawnParameters);
		UStaticMeshComponent* PhysicsComponent = PhysicsActor ? PhysicsActor->GetStaticMeshComponent() : nullptr;
		if (!CubeMesh || !PhysicsComponent)
		{
			State->WitnessError = TEXT("Could not create the Chaos rigid-body witness in the MRQ render world.");
			return false;
		}
		PhysicsActor->SetActorHiddenInGame(true);
		PhysicsComponent->SetMobility(EComponentMobility::Movable);
		PhysicsComponent->SetStaticMesh(CubeMesh);
		PhysicsComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PhysicsComponent->SetSimulatePhysics(true);
		PhysicsComponent->SetEnableGravity(true);
		PhysicsComponent->SetPhysicsLinearVelocity(FVector(250.0, 0.0, -100.0));
		PhysicsComponent->WakeAllRigidBodies();

		AActor* NiagaraActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(102000.0, 100000.0, 100000.0)),
			SpawnParameters);
		UClass* NiagaraComponentClass = LoadClass<UActorComponent>(nullptr, TEXT("/Script/Niagara.NiagaraComponent"));
		UObject* NiagaraSystem = LoadObject<UObject>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion.SimpleExplosion"));
		UActorComponent* NiagaraComponent = NiagaraActor && NiagaraComponentClass
			? NewObject<UActorComponent>(NiagaraActor, NiagaraComponentClass, TEXT("UESplattingFreezeNiagara"))
			: nullptr;
		if (!NiagaraActor || !NiagaraComponent || !NiagaraSystem)
		{
			State->WitnessError = TEXT("Could not load the built-in Niagara system used by the scene-freeze witness.");
			return false;
		}
		NiagaraActor->SetActorHiddenInGame(true);
		NiagaraActor->AddInstanceComponent(NiagaraComponent);
		if (USceneComponent* NiagaraSceneComponent = Cast<USceneComponent>(NiagaraComponent))
		{
			USceneComponent* NiagaraRoot = NewObject<USceneComponent>(NiagaraActor, TEXT("UESplattingFreezeNiagaraRoot"));
			NiagaraActor->AddInstanceComponent(NiagaraRoot);
			NiagaraActor->SetRootComponent(NiagaraRoot);
			NiagaraRoot->RegisterComponent();
			NiagaraSceneComponent->SetupAttachment(NiagaraRoot);
		}
		if (!InvokeObjectAndBoolFunction(NiagaraComponent, TEXT("SetAsset"), NiagaraSystem, true)
			|| !InvokeObjectAndBoolFunction(NiagaraComponent, TEXT("SetForceSolo"), nullptr, true))
		{
			State->WitnessError = TEXT("Could not configure the built-in Niagara scene-freeze witness through its reflected API.");
			return false;
		}
		NiagaraComponent->RegisterComponent();
		NiagaraComponent->Activate(true);
		NiagaraComponent->SetComponentTickEnabled(true);
		if (!NiagaraComponent->PrimaryComponentTick.IsTickFunctionRegistered()
			|| !NiagaraComponent->PrimaryComponentTick.IsTickFunctionEnabled())
		{
			State->WitnessError = TEXT("The Niagara witness did not register an enabled simulation tick.");
			return false;
		}

		State->RenderWorld = World;
		State->MotionActor = MotionActor;
		State->MotionComponent = MotionComponent;
		State->PhysicsActor = PhysicsActor;
		State->PhysicsComponent = PhysicsComponent;
		State->NiagaraActor = NiagaraActor;
		State->NiagaraComponent = NiagaraComponent;
		State->InitialMotionTransform = MotionActor->GetActorTransform();
		State->InitialMotionLastTickGameTime = MotionComponent->PrimaryComponentTick.GetLastIntervalTickGameTime();
		State->InitialPhysicsTransform = PhysicsComponent->GetComponentTransform();
		State->InitialPhysicsLinearVelocity = PhysicsComponent->GetPhysicsLinearVelocity();
		State->InitialNiagaraLastTickGameTime = NiagaraComponent->PrimaryComponentTick.GetLastIntervalTickGameTime();
		State->InitialWorldTimeSeconds = World->GetTimeSeconds();
		State->TimerFireCount = 0;
		const TWeakPtr<FFreezeExportState> WeakState = State;
		World->GetTimerManager().SetTimer(
			State->TimerHandle,
			FTimerDelegate::CreateLambda([WeakState]()
			{
				if (const TSharedPtr<FFreezeExportState> PinnedState = WeakState.Pin())
				{
					++PinnedState->TimerFireCount;
				}
			}),
			0.001f,
			true,
			0.001f);
		State->bWitnessesReady = true;
		return true;
	}

	void RecordFreezeWitnessFrame(const TSharedPtr<FFreezeExportState>& State)
	{
		UWorld* World = State ? State->RenderWorld.Get() : nullptr;
		AActor* MotionActor = State ? State->MotionActor.Get() : nullptr;
		URotatingMovementComponent* MotionComponent = State ? State->MotionComponent.Get() : nullptr;
		UStaticMeshComponent* PhysicsComponent = State ? State->PhysicsComponent.Get() : nullptr;
		UActorComponent* NiagaraComponent = State ? State->NiagaraComponent.Get() : nullptr;
		if (!State || !State->bWitnessesReady || !World || !MotionActor || !MotionComponent || !PhysicsComponent || !NiagaraComponent)
		{
			return;
		}

		FFreezeWitnessFrame& Frame = State->WitnessFrames.AddDefaulted_GetRef();
		Frame.bWorldPaused = World->IsPaused();
		Frame.WorldTimeSeconds = World->GetTimeSeconds();
		Frame.RealTimeSeconds = World->GetRealTimeSeconds();
		Frame.MotionTransform = MotionActor->GetActorTransform();
		Frame.MotionLastTickGameTime = MotionComponent->PrimaryComponentTick.GetLastIntervalTickGameTime();
		Frame.PhysicsTransform = PhysicsComponent->GetComponentTransform();
		Frame.PhysicsLinearVelocity = PhysicsComponent->GetPhysicsLinearVelocity();
		Frame.NiagaraLastTickGameTime = NiagaraComponent->PrimaryComponentTick.GetLastIntervalTickGameTime();
		Frame.TimerFireCount = State->TimerFireCount;
	}

	void RemoveFreezeDelegates(const TSharedPtr<FFreezeExportState>& State)
	{
		if (!State)
		{
			return;
		}
		UUESplattingCaptureTimeStep::OnFrameObservedForTesting().Remove(State->ObservationHandle);
		UUESplattingCaptureTimeStep::OnSceneFreezeAppliedForTesting().Remove(State->FreezeAppliedHandle);
		UUESplattingCaptureTimeStep::OnSceneFreezeRestoredForTesting().Remove(State->FreezeRestoredHandle);
		State->ObservationHandle.Reset();
		State->FreezeAppliedHandle.Reset();
		State->FreezeRestoredHandle.Reset();
	}

	bool DoesObservationMatchView(
		const FUESplattingCaptureFrameObservation& Observation,
		const FUESplattingCaptureView& View,
		float& OutLocationErrorCm,
		float& OutRotationErrorDegrees,
		float& OutFovErrorDegrees)
	{
		OutLocationErrorCm = FVector::Distance(Observation.CameraTransform.GetLocation(), View.Transform.GetLocation());
		OutRotationErrorDegrees = FMath::RadiansToDegrees(
			Observation.CameraTransform.GetRotation().AngularDistance(View.Transform.GetRotation()));
		OutFovErrorDegrees = FMath::Abs(Observation.HorizontalFieldOfView - View.HorizontalFieldOfView);
		return OutLocationErrorCm <= 0.1f && OutRotationErrorDegrees <= 0.01f && OutFovErrorDegrees <= 0.01f;
	}

	double ComputeMeanRgbDelta(const FString& FirstPath, const FString& SecondPath)
	{
		FImage FirstImage;
		FImage SecondImage;
		if (!FImageUtils::LoadImage(*FirstPath, FirstImage)
			|| !FImageUtils::LoadImage(*SecondPath, SecondImage)
			|| FirstImage.SizeX != SecondImage.SizeX
			|| FirstImage.SizeY != SecondImage.SizeY)
		{
			return -1.0;
		}

		FirstImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		SecondImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const TArrayView64<FColor> FirstPixels = FirstImage.AsBGRA8();
		const TArrayView64<FColor> SecondPixels = SecondImage.AsBGRA8();
		if (FirstPixels.IsEmpty() || FirstPixels.Num() != SecondPixels.Num())
		{
			return -1.0;
		}

		double TotalDelta = 0.0;
		int64 SampleCount = 0;
		const int64 SampleStride = FMath::Max<int64>(1, FirstPixels.Num() / 4096);
		for (int64 PixelIndex = 0; PixelIndex < FirstPixels.Num(); PixelIndex += SampleStride)
		{
			const FColor& First = FirstPixels[PixelIndex];
			const FColor& Second = SecondPixels[PixelIndex];
			TotalDelta += FMath::Abs(static_cast<int32>(First.R) - static_cast<int32>(Second.R));
			TotalDelta += FMath::Abs(static_cast<int32>(First.G) - static_cast<int32>(Second.G));
			TotalDelta += FMath::Abs(static_cast<int32>(First.B) - static_cast<int32>(Second.B));
			SampleCount += 3;
		}

		return SampleCount > 0 ? TotalDelta / static_cast<double>(SampleCount) : -1.0;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
		FWaitForSequentialExport,
		TSharedPtr<FExportState>, State,
		FAutomationTestBase*, Test);

	bool FWaitForSequentialExport::Update()
	{
		if (!State->bComplete)
		{
			return false;
		}

		Test->TestTrue(TEXT("Sequential MRQ export completed successfully"), State->Result.bSuccess);
		Test->TestEqual(TEXT("All sequential camera poses were exported"), State->Result.ImageCount, 16);
		Test->TestEqual(TEXT("Camera-only export reports no initialization points"), State->Result.SparsePointCount, 0);
		Test->TestEqual(TEXT("Exactly one render-time camera observation was recorded per output frame"), State->ObservationCount, 16);
		Test->TestEqual(TEXT("All 16 output frame ordinals were observed at render time"), State->ObservedFrames.Num(), 16);

		for (int32 ViewIndex = 0; ViewIndex < State->ExpectedViews.Num(); ++ViewIndex)
		{
			const FUESplattingCaptureFrameObservation* Observation = State->ObservedFrames.Find(ViewIndex);
			if (!Test->TestNotNull(*FString::Printf(TEXT("Render-time observation exists for frame %d"), ViewIndex + 1), Observation))
			{
				continue;
			}

			float LocationErrorCm = 0.0f;
			float RotationErrorDegrees = 0.0f;
			float FovErrorDegrees = 0.0f;
			const bool bMatchesExpectedView = DoesObservationMatchView(
				*Observation,
				State->ExpectedViews[ViewIndex],
				LocationErrorCm,
				RotationErrorDegrees,
				FovErrorDegrees);
			Test->TestTrue(
				*FString::Printf(TEXT("Frame %d uses its exact intended transform and FOV (location %.4f cm, rotation %.5f deg, FOV %.5f deg)"), ViewIndex + 1, LocationErrorCm, RotationErrorDegrees, FovErrorDegrees),
				bMatchesExpectedView);
		}

		TArray<FString> Images;
		IFileManager::Get().FindFiles(Images, *FPaths::Combine(State->OutputDirectory, TEXT("images"), TEXT("*.jpg")), true, false);
		Test->TestEqual(TEXT("Final image directory contains exactly 16 JPEGs"), Images.Num(), 16);
		Test->TestTrue(
			TEXT("First globally numbered frame exists"),
			IFileManager::Get().FileExists(*FPaths::Combine(State->OutputDirectory, TEXT("images"), TEXT("frame_000001.jpg"))));
		Test->TestTrue(
			TEXT("Last globally numbered frame exists"),
			IFileManager::Get().FileExists(*FPaths::Combine(State->OutputDirectory, TEXT("images"), TEXT("frame_000016.jpg"))));

		const FString ImagesDirectory = FPaths::Combine(State->OutputDirectory, TEXT("images"));
		const double OppositeDirectionDelta = ComputeMeanRgbDelta(
			FPaths::Combine(ImagesDirectory, TEXT("frame_000001.jpg")),
			FPaths::Combine(ImagesDirectory, TEXT("frame_000005.jpg")));
		Test->TestTrue(
			TEXT("Opposite keyed camera directions render materially different compositions"),
			OppositeDirectionDelta >= 10.0);
		Test->TestFalse(
			TEXT("MRQ staging directory is removed after the sequential shot"),
			IFileManager::Get().DirectoryExists(*FPaths::Combine(State->OutputDirectory, TEXT(".uesplatting-mrq-staging"))));
		Test->TestFalse(
			TEXT("Camera-only export does not emit a placeholder sparse_pc.ply"),
			IFileManager::Get().FileExists(*FPaths::Combine(State->OutputDirectory, TEXT("sparse_pc.ply"))));

		FString ColmapImagesText;
		if (Test->TestTrue(
			TEXT("Camera-only COLMAP images.txt can be read"),
			FFileHelper::LoadFileToString(ColmapImagesText, *FPaths::Combine(State->OutputDirectory, TEXT("colmap"), TEXT("sparse"), TEXT("0"), TEXT("images.txt")))))
		{
			ColmapImagesText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			for (int32 ImageIndex = 1; ImageIndex < State->ExpectedViews.Num(); ++ImageIndex)
			{
				const FString RecordBoundary = FString::Printf(
					TEXT("images/frame_%06d.jpg\n\n%d "),
					ImageIndex,
					ImageIndex + 1);
				Test->TestTrue(
					*FString::Printf(TEXT("Camera-only COLMAP image %d retains its empty POINTS2D line"), ImageIndex),
					ColmapImagesText.Contains(RecordBoundary));
			}
		}

		const TSharedPtr<FJsonObject> Transforms = LoadJsonObject(FPaths::Combine(State->OutputDirectory, TEXT("transforms.json")));
		if (Test->TestTrue(TEXT("Camera-only transforms.json parses"), Transforms.IsValid()))
		{
			Test->TestFalse(TEXT("Camera-only transforms.json omits ply_file_path"), Transforms->HasField(TEXT("ply_file_path")));
		}

		const TSharedPtr<FJsonObject> Manifest = LoadJsonObject(FPaths::Combine(State->OutputDirectory, TEXT("capture-manifest.json")));
		if (Test->TestTrue(TEXT("Camera-only capture manifest parses"), Manifest.IsValid()))
		{
			const TSharedPtr<FJsonValue>* OutputsValue = Manifest->Values.Find(TEXT("outputs"));
			if (Test->TestTrue(
				TEXT("Capture manifest contains an outputs object"),
				OutputsValue && OutputsValue->IsValid() && (*OutputsValue)->Type == EJson::Object))
			{
				const TSharedPtr<FJsonObject> Outputs = (*OutputsValue)->AsObject();
				const TSharedPtr<FJsonValue>* PointCloudValue = Outputs->Values.Find(TEXT("point_cloud"));
				Test->TestTrue(
					TEXT("Camera-only manifest records outputs.point_cloud as null"),
					PointCloudValue && PointCloudValue->IsValid() && (*PointCloudValue)->Type == EJson::Null);
			}

			const TSharedPtr<FJsonValue>* PointCloudMetadataValue = Manifest->Values.Find(TEXT("point_cloud"));
			if (Test->TestTrue(
				TEXT("Capture manifest contains point-cloud provenance metadata"),
				PointCloudMetadataValue && PointCloudMetadataValue->IsValid() && (*PointCloudMetadataValue)->Type == EJson::Object))
			{
				const TSharedPtr<FJsonObject> PointCloudMetadata = (*PointCloudMetadataValue)->AsObject();
				Test->TestEqual(TEXT("Camera-only manifest marks seed generation disabled"), PointCloudMetadata->GetStringField(TEXT("status")), FString(TEXT("disabled")));
				Test->TestFalse(TEXT("Manifest never claims authoritative scene geometry"), PointCloudMetadata->GetBoolField(TEXT("authoritative_scene_geometry")));
			}
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
		FWaitForFrozenSequentialExport,
		TSharedPtr<FFreezeExportState>, State,
		FAutomationTestBase*, Test);

	bool FWaitForFrozenSequentialExport::Update()
	{
		if (!State->bComplete)
		{
			return false;
		}

		Test->TestTrue(TEXT("Frozen MRQ export completed successfully"), State->Result.bSuccess);
		Test->TestEqual(TEXT("All four frozen known-pose views were exported"), State->Result.ImageCount, 4);
		Test->TestEqual(TEXT("All four frozen output frames were observed"), State->ObservedFrames.Num(), 4);
		for (int32 ViewIndex = 0; ViewIndex < State->ExpectedViews.Num(); ++ViewIndex)
		{
			const FUESplattingCaptureFrameObservation* Observation = State->ObservedFrames.Find(ViewIndex);
			if (!Test->TestNotNull(*FString::Printf(TEXT("Frozen render-time observation exists for frame %d"), ViewIndex + 1), Observation))
			{
				continue;
			}
			float LocationErrorCm = 0.0f;
			float RotationErrorDegrees = 0.0f;
			float FovErrorDegrees = 0.0f;
			Test->TestTrue(
				*FString::Printf(TEXT("Frozen frame %d still uses its exact keyed camera pose"), ViewIndex + 1),
				DoesObservationMatchView(
					*Observation,
					State->ExpectedViews[ViewIndex],
					LocationErrorCm,
					RotationErrorDegrees,
					FovErrorDegrees));
		}

		Test->TestTrue(TEXT("Scene freeze was applied in the MRQ render world"), State->bFreezeAppliedObserved);
		Test->TestTrue(TEXT("Scene freeze was restored during MRQ shutdown"), State->bFreezeRestoredObserved);
		Test->TestTrue(TEXT("The MRQ render world entered paused ticking"), State->AppliedObservation.bWorldIsPaused);
		Test->TestTrue(TEXT("The camera manager was allowed to update while paused"), State->AppliedObservation.bFullCameraTickIsEnabled);
		Test->TestEqual(TEXT("The prior world pause state was restored"), State->RestoredObservation.bWorldIsPaused, State->AppliedObservation.bWorldWasPaused);
		Test->TestEqual(TEXT("The prior full-camera-tick flag was restored"), State->RestoredObservation.bFullCameraTickIsEnabled, State->AppliedObservation.bFullCameraTickWasEnabled);
		Test->TestTrue(TEXT("Actor, Chaos, Niagara, timer, and material-time witnesses initialized"), State->bWitnessesReady);
		if (!State->WitnessError.IsEmpty())
		{
			Test->AddError(State->WitnessError);
		}
		Test->TestEqual(TEXT("One freeze witness sample was recorded per output frame"), State->WitnessFrames.Num(), 4);
		for (int32 FrameIndex = 0; FrameIndex < State->WitnessFrames.Num(); ++FrameIndex)
		{
			const FFreezeWitnessFrame& Frame = State->WitnessFrames[FrameIndex];
			Test->TestTrue(*FString::Printf(TEXT("Frame %d used a paused world tick"), FrameIndex + 1), Frame.bWorldPaused);
			Test->TestTrue(
				*FString::Printf(TEXT("Frame %d kept game/material world time fixed"), FrameIndex + 1),
				FMath::IsNearlyEqual(Frame.WorldTimeSeconds, State->InitialWorldTimeSeconds, UE_DOUBLE_SMALL_NUMBER));
			Test->TestTrue(
				*FString::Printf(TEXT("Frame %d kept the normal ticking actor fixed"), FrameIndex + 1),
				Frame.MotionTransform.Equals(State->InitialMotionTransform, 0.001f)
					&& FMath::IsNearlyEqual(Frame.MotionLastTickGameTime, State->InitialMotionLastTickGameTime, UE_DOUBLE_SMALL_NUMBER));
			Test->TestTrue(
				*FString::Printf(TEXT("Frame %d kept the Chaos body fixed"), FrameIndex + 1),
				Frame.PhysicsTransform.Equals(State->InitialPhysicsTransform, 0.01f)
					&& Frame.PhysicsLinearVelocity.Equals(State->InitialPhysicsLinearVelocity, 0.01f));
			Test->TestTrue(
				*FString::Printf(TEXT("Frame %d kept the enabled Niagara simulation tick fixed"), FrameIndex + 1),
				FMath::IsNearlyEqual(Frame.NiagaraLastTickGameTime, State->InitialNiagaraLastTickGameTime, UE_DOUBLE_SMALL_NUMBER));
			Test->TestEqual(*FString::Printf(TEXT("Frame %d did not fire the gameplay timer"), FrameIndex + 1), Frame.TimerFireCount, 0);
		}
		if (State->WitnessFrames.Num() > 1)
		{
			Test->TestTrue(
				TEXT("Real time advanced while material game time remained frozen"),
				State->WitnessFrames.Last().RealTimeSeconds > State->WitnessFrames[0].RealTimeSeconds);
		}

		const FString ImagesDirectory = FPaths::Combine(State->OutputDirectory, TEXT("images"));
		const double OppositeDirectionDelta = ComputeMeanRgbDelta(
			FPaths::Combine(ImagesDirectory, TEXT("frame_000001.jpg")),
			FPaths::Combine(ImagesDirectory, TEXT("frame_000003.jpg")));
		Test->TestTrue(TEXT("Opposite keyed views remain materially different while frozen"), OppositeDirectionDelta >= 10.0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingMovieRenderQueueSequentialTest,
	"UESplatting.Capture.MovieRenderQueue.SequentialKnownPoses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FUESplattingMovieRenderQueueSequentialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UESplattingMovieRenderQueueTests;
	AddExpectedError(
		TEXT("Cannot resolve version number without a valid evaluated graph"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedError(
		TEXT("Script call stack:"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedError(
		TEXT("LoadPackage: SkipPackage: /Temp/MovieRenderPipeline/QuickRenderSettings"),
		EAutomationExpectedErrorFlags::Contains,
		-1);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available"), World))
	{
		return false;
	}

	TArray<FUESplattingCaptureView> Views;
	Views.Reserve(16);
	for (int32 StationIndex = 0; StationIndex < 2; ++StationIndex)
	{
		const FVector StationLocation(StationIndex * 200.0, 0.0, 200.0);
		for (int32 ViewIndex = 0; ViewIndex < 8; ++ViewIndex)
		{
			FUESplattingCaptureView& View = Views.AddDefaulted_GetRef();
			View.StationIndex = StationIndex;
			View.HorizontalFieldOfView = 72.0f + static_cast<float>(StationIndex * 8 + ViewIndex);
			View.Transform = FTransform(FRotator(0.0, ViewIndex * 45.0, 0.0), StationLocation);
		}
	}

	const TSharedPtr<FExportState> State = MakeShared<FExportState>();
	State->OutputDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("UESplatting"),
		TEXT("Automation"),
		FString::Printf(TEXT("MRQSequential_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	State->ExpectedViews = Views;
	State->ObservationHandle = UUESplattingCaptureTimeStep::OnFrameObservedForTesting().AddLambda(
		[State](const FUESplattingCaptureFrameObservation& Observation)
		{
			++State->ObservationCount;
			State->ObservedFrames.Add(Observation.OutputFrameNumber, Observation);
		});

	FUESplattingDatasetExportSettings Settings;
	Settings.Renderer = EUESplattingSceneCaptureRenderer::MovieRenderQueue;
	Settings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
	Settings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
	Settings.ImageWidth = 320;
	Settings.ImageHeight = 180;
	Settings.OutputDirectory.Path = State->OutputDirectory;
	Settings.CaptureId = TEXT("mrq_sequential_test");
	Settings.bGenerateTracePointCloud = false;

	FUESplattingDatasetExportResult StartResult;
	const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
		World,
		Views,
		Settings,
		[State](const FUESplattingDatasetExportResult& Result)
		{
			UUESplattingCaptureTimeStep::OnFrameObservedForTesting().Remove(State->ObservationHandle);
			State->ObservationHandle.Reset();
			State->Result = Result;
			State->bComplete = true;
		},
		StartResult);
	if (!TestTrue(TEXT("Sequential MRQ export started"), bStarted))
	{
		UUESplattingCaptureTimeStep::OnFrameObservedForTesting().Remove(State->ObservationHandle);
		State->ObservationHandle.Reset();
		AddError(StartResult.Message);
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForSequentialExport(State, this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUESplattingMovieRenderQueueSceneFreezeTest,
	"UESplatting.Capture.MovieRenderQueue.SceneFreezeKnownPoses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FUESplattingMovieRenderQueueSceneFreezeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UESplattingMovieRenderQueueTests;
	AddExpectedError(TEXT("Cannot resolve version number without a valid evaluated graph"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Script call stack:"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("LoadPackage: SkipPackage: /Temp/MovieRenderPipeline/QuickRenderSettings"), EAutomationExpectedErrorFlags::Contains, -1);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world is available for the scene-freeze test"), World))
	{
		return false;
	}

	TArray<FUESplattingCaptureView> Views;
	Views.Reserve(4);
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		FUESplattingCaptureView& View = Views.AddDefaulted_GetRef();
		View.StationIndex = 0;
		View.HorizontalFieldOfView = 80.0f;
		View.Transform = FTransform(FRotator(0.0, ViewIndex * 90.0, 0.0), FVector(0.0, 0.0, 200.0));
	}

	const TSharedPtr<FFreezeExportState> State = MakeShared<FFreezeExportState>();
	State->OutputDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("UESplatting"),
		TEXT("Automation"),
		FString::Printf(TEXT("MRQSceneFreeze_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	State->ExpectedViews = Views;
	State->FreezeAppliedHandle = UUESplattingCaptureTimeStep::OnSceneFreezeAppliedForTesting().AddLambda(
		[State](UWorld* RenderWorld, const FUESplattingSceneFreezeObservation& Observation)
		{
			State->AppliedObservation = Observation;
			State->bFreezeAppliedObserved = true;
			InitializeFreezeWitnesses(RenderWorld, State);
		});
	State->FreezeRestoredHandle = UUESplattingCaptureTimeStep::OnSceneFreezeRestoredForTesting().AddLambda(
		[State](UWorld* RenderWorld, const FUESplattingSceneFreezeObservation& Observation)
		{
			State->RestoredObservation = Observation;
			State->bFreezeRestoredObserved = true;
			if (RenderWorld)
			{
				RenderWorld->GetTimerManager().ClearTimer(State->TimerHandle);
			}
			if (AActor* Actor = State->MotionActor.Get())
			{
				Actor->Destroy();
			}
			if (AActor* Actor = State->PhysicsActor.Get())
			{
				Actor->Destroy();
			}
			if (AActor* Actor = State->NiagaraActor.Get())
			{
				Actor->Destroy();
			}
		});
	State->ObservationHandle = UUESplattingCaptureTimeStep::OnFrameObservedForTesting().AddLambda(
		[State](const FUESplattingCaptureFrameObservation& Observation)
		{
			++State->ObservationCount;
			State->ObservedFrames.Add(Observation.OutputFrameNumber, Observation);
			RecordFreezeWitnessFrame(State);
		});

	FUESplattingDatasetExportSettings Settings;
	Settings.Renderer = EUESplattingSceneCaptureRenderer::MovieRenderQueue;
	Settings.PhotometricMode = EUESplattingSceneCapturePhotometricMode::SceneAuthored;
	Settings.ImageFormat = EUESplattingSceneCaptureImageFormat::JPEG;
	Settings.ImageWidth = 320;
	Settings.ImageHeight = 180;
	Settings.OutputDirectory.Path = State->OutputDirectory;
	Settings.CaptureId = TEXT("mrq_scene_freeze_test");
	Settings.bGenerateTracePointCloud = false;
	Settings.bFreezeSceneDuringCapture = true;

	FUESplattingDatasetExportResult StartResult;
	const bool bStarted = UUESplattingDatasetExporter::StartColmapDatasetExportFromCaptureViews(
		World,
		Views,
		Settings,
		[State](const FUESplattingDatasetExportResult& Result)
		{
			RemoveFreezeDelegates(State);
			State->Result = Result;
			State->bComplete = true;
		},
		StartResult);
	if (!TestTrue(TEXT("Frozen sequential MRQ export started"), bStarted))
	{
		RemoveFreezeDelegates(State);
		AddError(StartResult.Message);
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForFrozenSequentialExport(State, this));
	return true;
}

#endif
