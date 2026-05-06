#pragma once

struct AnalysisRequestState;
struct SettingsAnalysisEditState;
struct ProjectionMapParams;

void SyncSettingsAnalysisDraftsFromRuntime(SettingsAnalysisEditState& edit,
                                           const AnalysisRequestState& requests);

void SubmitSettingsAnalysisRequests(SettingsAnalysisEditState& edit,
                                    AnalysisRequestState& requests,
                                    const ProjectionMapParams* projectionMapParams = nullptr);
