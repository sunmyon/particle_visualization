#include "data/clump_loader.h"

#ifdef CLUMP_DATA_READ
#include "FileIO/clump_io.h"
#include "data/clump_data.h"
#include <stdexcept>

TrackingVector<ClumpData>
loadClumpData(const char* fname_clump_file,
              int snapshotIndex,
              const float scale_from_phys)
{
  uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET
                   | L_CLUMP_STELLAR_COUNT | L_CLUMP_STELLAR_ID | L_CLUMP_STELLAR_MASS
                   | L_CLUMP_POSITION | L_CLUMP_DENSITY
                   | L_CLUMP_TEMPERATURE | L_CLUMP_MASS
                   | L_PARTICLE_IDS);

  ClumpInfoIO in;
  if (!ClumpIO::readSnapshot(fname_clump_file, snapshotIndex, mask, in)) {
    return {};
  }

  TrackingVector<ClumpData> clumps;
  clumps.reserve(in.clump_id.size());

  for (size_t i = 0; i < in.clump_id.size(); i++) {
    ClumpData cd;
    cd.clumpID = in.clump_id[i];

    if (in.clump_next_id.size())
      cd.nextClumpID = in.clump_next_id[i];

    cd.count = in.clump_size[i];
    cd.offset = in.clump_offset[i];

    for (int k = 0; k < 3; k++) {
      cd.originalPos[k] = in.clump_position[i * 3 + k];
      cd.Pos[k] = cd.originalPos[k] * scale_from_phys;
    }

    cd.density = in.clump_density[i];
    cd.temperature = in.clump_temperature[i];
    cd.mass = in.clump_mass[i];
    cd.stellar_mass = in.clump_stellar_mass[i];
    cd.stellar_count = in.clump_stellar_count[i];
    cd.stellar_id = in.clump_stellar_id[i];

    int off = in.clump_offset[i];
    int sz = in.clump_size[i];
    for (int j = 0; j < sz; j++) {
      if (off + j < static_cast<int>(in.particle_ids.size()))
        cd.IDs.push_back(in.particle_ids[off + j]);
      else
        throw std::runtime_error("Invalid offset/size: exceeds sorted_particle_ids size");
    }

    clumps.push_back(std::move(cd));
  }

  return clumps;
}
#endif
