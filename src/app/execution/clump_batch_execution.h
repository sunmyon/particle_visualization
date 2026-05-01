#pragma once

#ifdef CLUMP_DATA_READ

class FindClump;
class SimulationDataset;
struct ClumpRequestState;
struct ClumpBatchRequestState;
struct ClumpBatchResultState;
struct ClumpBatchRuntimeState;
struct FileNavigationRuntimeState;
struct SnapshotLoadRuntimeState;

bool ExecuteClumpRequest(SimulationDataset& particles,
                         FindClump& clumpFind,
                         ClumpRequestState& request);

void ExecuteClumpBatchRequest(SimulationDataset& particles,
                              FileNavigationRuntimeState& fileNav,
                              SnapshotLoadRuntimeState& snapshotLoad,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchRuntimeState& runtime,
                              ClumpBatchResultState& result);

#endif
