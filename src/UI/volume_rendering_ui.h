#pragma once

#ifdef VOLUME_RENDERING

struct QuantityState;
struct SettingsActionRequestState;
struct SettingsAnalysisEditState;
struct SettingsAnalysisResultView;

void DrawVolumeRenderingSettingsSection(
  const QuantityState& quantity,
  SettingsAnalysisEditState& edit,
  const SettingsAnalysisResultView& result,
  SettingsActionRequestState& settingsReq);

#endif
