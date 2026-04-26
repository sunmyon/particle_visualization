#pragma once

struct AnalysisRequestState;
struct SettingsAnalysisEditState;

void SyncSettingsAnalysisDraftsFromRuntime(SettingsAnalysisEditState& edit,
                                           const AnalysisRequestState& requests);

void ApplySettingsAnalysisEditRequests(SettingsAnalysisEditState& edit,
                                       AnalysisRequestState& requests);
