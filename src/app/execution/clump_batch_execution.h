#pragma once

#ifdef CLUMP_DATA_READ

class FindClump;
class ParticleArray;
struct ClumpRequestState;
struct ClumpBatchRequestState;
struct ClumpBatchResultState;
struct ClumpBatchRuntimeState;
struct FileNavigationRuntimeState;
struct SnapshotLoadRuntimeState;

bool ExecuteClumpRequest(ParticleArray& particles,
                         FindClump& clumpFind,
                         ClumpRequestState& request);

void ExecuteClumpBatchRequest(ParticleArray& particles,
                              FileNavigationRuntimeState& fileNav,
                              SnapshotLoadRuntimeState& snapshotLoad,
                              FindClump& clumpFind,
                              ClumpBatchRequestState& request,
                              ClumpBatchRuntimeState& runtime,
                              ClumpBatchResultState& result);

#endif
