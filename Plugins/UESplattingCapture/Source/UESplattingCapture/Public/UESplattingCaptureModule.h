// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUESplattingCaptureModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void AddCaptureVolumeToLevel();
	void AddDirectionalArrayToLevel();
	void AddFocusedDetailRegionToLevel();
	void ExportSelectedCamerasToColmapDataset();
	void ExportSelectedCaptureVolumesToColmapDataset();
};
