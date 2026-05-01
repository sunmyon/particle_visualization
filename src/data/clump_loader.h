#pragma once

#include "data/particle_array.h"

class ClumpData;
#ifdef CLUMP_DATA_READ
std::vector<ClumpData>
loadClumpData(const char* fname_clump_file,
              int snapshotIndex,
              float scale_from_phys);
#endif
