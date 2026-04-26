#pragma once

struct AnalysisDerivedState;
struct AppDataState;
struct AppRuntimeState;
struct AppServices;
struct CameraContext;

void ExecuteAnalysisJobRequests(AppDataState& data,
                                AppRuntimeState& runtime,
                                AnalysisDerivedState& analysis,
                                AppServices& services,
                                CameraContext& camera,
                                int currentFileIndex);
