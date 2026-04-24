#pragma once

struct AppDataState;
struct AppRuntimeState;

void ProcessSnapshotLoadQueue(AppDataState& data,
                              AppRuntimeState& runtime);
