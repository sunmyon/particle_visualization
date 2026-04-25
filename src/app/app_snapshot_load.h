#pragma once

struct AppDataState;
struct AppRuntimeState;
struct AppServices;

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime,
                              AppServices& services);
