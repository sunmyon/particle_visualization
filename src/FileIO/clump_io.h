#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifdef CLUMP_DATA_READ
enum LoadMask : uint32_t {
  L_TIME                        = 1u << 0,
    L_CLUMP_ID                    = 1u << 1,
    L_CLUMP_NEXT_ID               = 1u << 2,
    L_CLUMP_OFFSET                = 1u << 3,
    L_CLUMP_SIZE                  = 1u << 4,
    L_CLUMP_STELLAR_COUNT         = 1u << 5,
    L_CLUMP_STELLAR_ID            = 1u << 6,
    L_CLUMP_POSITION              = 1u << 7,
    L_CLUMP_VELOCITY              = 1u << 8,
    L_CLUMP_DENSITY               = 1u << 9,
    L_CLUMP_MAX_DENSITY           = 1u << 10,
    L_CLUMP_TEMPERATURE           = 1u << 11,
    L_CLUMP_TEMP_DENSITY_WEIGHTED = 1u << 12,
    L_CLUMP_MASS                  = 1u << 13,
    L_CLUMP_STELLAR_MASS          = 1u << 14,
    L_CLUMP_STELLAR_MASS_MAXIMUM  = 1u << 15,
    L_PARTICLE_IDS                = 1u << 16,
    L_PARTICLE_TYPE               = 1u << 17,
    L_DENSITY_THRESHOLD           = 1u << 18,

    L_ALL_CLUMP_FIELDS = (L_CLUMP_ID
			  | L_CLUMP_NEXT_ID
			  | L_CLUMP_OFFSET
			  | L_CLUMP_SIZE
			  | L_CLUMP_STELLAR_COUNT
			  | L_CLUMP_STELLAR_ID
			  | L_CLUMP_POSITION
			  | L_CLUMP_VELOCITY
			  | L_CLUMP_DENSITY
			  | L_CLUMP_MAX_DENSITY
			  | L_CLUMP_TEMPERATURE
			  | L_CLUMP_TEMP_DENSITY_WEIGHTED
			  | L_CLUMP_MASS
			  | L_CLUMP_STELLAR_MASS
			  | L_CLUMP_STELLAR_MASS_MAXIMUM),

    L_ALL_PARTICLE_FIELDS = (L_PARTICLE_IDS | L_PARTICLE_TYPE),

    L_ALL = 0xFFFFFFFFu
    };

struct ClumpInfoIO {
  float   time = 0.0f;
  float   density_threshold = 0.0f;
  
  // Cluster information. Vector length equals the number of clusters.
  std::vector<float>         clump_position;    // 3×nClumps
  std::vector<float>         clump_velocity;    // 3×nClumps
  std::vector<float>         clump_density;
  std::vector<float>         clump_max_density;
  std::vector<float>         clump_temperature;
  std::vector<float>         clump_temperature_density_weighted;
  std::vector<float>         clump_mass;
  std::vector<float>         clump_stellar_mass;
  std::vector<float>         clump_stellar_mass_maximum;
  std::vector<int>           clump_id;
  std::vector<int>           clump_next_id;
  std::vector<int>           clump_offset;
  std::vector<int>           clump_size;
  std::vector<int>           clump_stellar_count;
  std::vector<int>           clump_stellar_id;
  std::vector<int64_t>       particle_ids;
  std::vector<char>           particle_type;
  
  ClumpInfoIO() = default;
};

//------------------------------------------------------------------------------
// ClumpIO: namespace for reading and writing HDF5 files.
namespace ClumpIO {
  bool readSnapshot(const std::string& fname,
                    int snapshotID,
                    uint32_t mask,
                    ClumpInfoIO& out);

  bool writeSnapshot(const std::string& fname,
                     int snapshotID,
                     uint32_t mask,
                     const ClumpInfoIO& data);
}

class StructureNode;
class ClumpData;
float readClumpTime(std::string fname, int snapshotIndex);

void readClumpEvolution(const std::string& fname, int snapshotInit, int snapshotEnd, int dsnapshot, int clumpID_init,
			std::vector<float>& times, std::vector<ClumpData>& clumps);

void addNextClumpIDtoHDF5(const std::vector<StructureNode *>& nodes,
			  const std::string &filename, int snapshotIndex);
#endif
