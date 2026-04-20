#pragma once

#include "data/particle_array.h"

#ifdef CLUMP_DATA_READ
TrackingVector<ClumpData>
loadClumpData(const char* fname_clump_file,
              int snapshotIndex,
              float scale_from_phys);
#endif
