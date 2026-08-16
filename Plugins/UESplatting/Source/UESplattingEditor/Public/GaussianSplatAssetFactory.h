// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "GaussianDataTypes.h"
#include "GaussianSplatAssetFactory.generated.h"

class UGaussianSplatAsset;

/**
 * Factory for importing registered splat formats as Gaussian Splat assets
 */
UCLASS(hidecategories = Object)
class UESPLATTINGEDITOR_API UGaussianSplatAssetFactory : public UFactory, public FReimportHandler
{
	GENERATED_BODY()

public:
	UGaussianSplatAssetFactory();

	//~ Begin UFactory Interface
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateFile(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		const FString& Filename,
		const TCHAR* Parms,
		FFeedbackContext* Warn,
		bool& bOutOperationCanceled
	) override;
	virtual bool CanCreateNew() const override { return false; }
	virtual FText GetDisplayName() const override;
	//~ End UFactory Interface

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	//~ End FReimportHandler Interface

private:
	/**
	 * Import a supported splat file and create/update a Gaussian Splat asset
	 * @param FilePath Path to the source splat file
	 * @param InParent Parent object for the new asset
	 * @param InName Name for the new asset
	 * @param Flags Object flags
	 * @param ExistingAsset Existing asset to update (for reimport)
	 * @return The created or updated asset, or nullptr on failure
	 */
	UGaussianSplatAsset* ImportPLYFile(
		const FString& FilePath,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UGaussianSplatAsset* ExistingAsset = nullptr
	);
};
