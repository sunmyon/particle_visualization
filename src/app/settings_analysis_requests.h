#pragma once

struct AnalysisRequestState;
struct SettingsAnalysisEditState;

void SyncSettingsAnalysisDraftsFromRuntime(SettingsAnalysisEditState& edit,
                                           const AnalysisRequestState& requests);

void SubmitSettingsAnalysisRequests(SettingsAnalysisEditState& edit,
                                    AnalysisRequestState& requests);
