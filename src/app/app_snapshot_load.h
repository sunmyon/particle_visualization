#pragma once

struct AppDataState;
struct AppRuntimeState;
struct AppServices;
struct CameraContext;

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime,
                              AppServices& services,
                              CameraContext* camera = nullptr);
