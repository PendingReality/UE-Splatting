// SPDX-License-Identifier: MIT

#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AUESplattingCaptureVolume;

class FUESplattingCaptureVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TArray<AUESplattingCaptureVolume*> GetValidVolumes() const;

	FReply OnExportClicked();
	FReply OnCalibrateClicked();
	FReply OnOpenCalibrationPreviewClicked();
	FText GetSummaryText() const;
	FText GetOutputRootText() const;

	TArray<TWeakObjectPtr<AUESplattingCaptureVolume>> CaptureVolumes;
};
