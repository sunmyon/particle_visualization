#include "data/particle_data.h"
#include "data/halo_store.h"

#include <unordered_map>

void HaloStore::recomputeHaloPositionsFromParticles(const TrackingVector<ParticleData>& particles,
                                                    bool useMassWeight,
                                                    bool useOriginalPos)
{
  if (!haloIDsLoaded_) return;
  if (haloIDs_.size() != haloes_.size()) return;

  std::unordered_map<uint64_t, size_t> idToIndex;
  idToIndex.reserve(particles.size());

  for (size_t i = 0; i < particles.size(); ++i) {
    idToIndex[static_cast<uint64_t>(particles[i].ID)] = i;
  }

  const size_t nHalos = haloes_.size();

  for (size_t ih = 0; ih < nHalos; ++ih) {
    const auto& ids = haloIDs_[ih];

    double sx = 0.0, sy = 0.0, sz = 0.0;
    double sw = 0.0;

    for (const auto& pidRaw : ids) {
      const uint64_t pid = static_cast<uint64_t>(pidRaw);
      auto it = idToIndex.find(pid);
      if (it == idToIndex.end()) continue;

      const ParticleData& p = particles[it->second];
      const float* x = useOriginalPos ? p.original_pos : p.pos;

      double w = 1.0;
      if (useMassWeight) {
        w = (p.mass > 0.0f) ? static_cast<double>(p.mass) : 1.0;
      }

      sx += w * static_cast<double>(x[0]);
      sy += w * static_cast<double>(x[1]);
      sz += w * static_cast<double>(x[2]);
      sw += w;
    }

    if (sw <= 0.0) continue;

    haloes_[ih].GroupPos[0] = static_cast<float>(sx / sw);
    haloes_[ih].GroupPos[1] = static_cast<float>(sy / sw);
    haloes_[ih].GroupPos[2] = static_cast<float>(sz / sw);
  }
}

#ifdef HAVE_HDF5
#include "FileIO/hdf5_utils.h"
#include "H5Cpp.h"
#include <hdf5.h>

bool HaloStore::loadFromHDF5(const char* fname, bool loadIDs)
{
  clear();

  try {
    H5::H5File file(fname, H5F_ACC_RDONLY);

    const char* gpath = "/Group";
    if (!HDF5Utils::groupExists(file, gpath)) {
      return false;
    }

    H5::Group grp = file.openGroup(gpath);

    H5::DataSet posDS = grp.openDataSet("GroupPos");
    const HDF5Utils::H5DatasetMeta posMeta = HDF5Utils::getDatasetMeta(posDS);
    if (posMeta.rank != 2 || posMeta.dims[1] != 3) {
      throw std::runtime_error("GroupPos must be a rank-2 dataset with shape [N,3]");
    }
    
    const size_t nHalos = static_cast<size_t>(posMeta.dims[0]);
    haloes_ = TrackingVector<HaloData>(nHalos);

    TrackingVector<int> groupLen;
    HDF5Utils::readDataset1D(grp, "GroupLen", groupLen);
    
    if (groupLen.size() != nHalos) {
      throw std::runtime_error("GroupLen size mismatch");
    }
    
    for (size_t i = 0; i < nHalos; ++i) {
      haloes_[i].GroupLen = groupLen[i];
    }
    
    HDF5Utils::readDatasetToObjects<HaloData, float, 3>(
      grp, "GroupPos", haloes_,
      [](HaloData& h, const float xyz[3]) {
        h.GroupPos[0] = xyz[0];
        h.GroupPos[1] = xyz[1];
        h.GroupPos[2] = xyz[2];
      },
      H5::PredType::NATIVE_FLOAT
    );

    HDF5Utils::readDatasetToObjects<HaloData, float, 3>(
      grp, "GroupVel", haloes_,
      [](HaloData& h, const float v[3]) {
        h.GroupVel[0] = v[0];
        h.GroupVel[1] = v[1];
        h.GroupVel[2] = v[2];
      },
      H5::PredType::NATIVE_FLOAT
    );

    HDF5Utils::readDatasetToObjects<HaloData, float, 1>(
      grp, "GroupMass", haloes_,
      [](HaloData& h, const float m[1]) {
        h.GroupMass = m[0];
      },
      H5::PredType::NATIVE_FLOAT
    );

    HDF5Utils::readDatasetToObjects<HaloData, float, 6>(
      grp, "GroupMassType", haloes_,
      [](HaloData& h, const float m[6]) {
        for (int k = 0; k < 6; ++k) h.GroupMassType[k] = m[k];
      },
      H5::PredType::NATIVE_FLOAT
    );

    if (HDF5Utils::datasetExists(grp, "GroupLenType")) {
      HDF5Utils::readDatasetToObjects<HaloData, int, 6>(
        grp, "GroupLenType", haloes_,
        [](HaloData& h, const int m[6]) {
          for (int k = 0; k < 6; ++k) h.GroupLenType[k] = m[k];
        },
        H5::PredType::NATIVE_INT
      );
    }

    if (HDF5Utils::datasetExists(grp, "GroupGasMetallicity")) {
      HDF5Utils::readDatasetToObjects<HaloData, float, 1>(
        grp, "GroupGasMetallicity", haloes_,
        [](HaloData& h, const float z[1]) {
          h.GroupMetallicity[0] = z[0];
        },
        H5::PredType::NATIVE_FLOAT
      );
    }

    if (HDF5Utils::datasetExists(grp, "GroupStarMetallicity")) {
      HDF5Utils::readDatasetToObjects<HaloData, float, 1>(
        grp, "GroupStarMetallicity", haloes_,
        [](HaloData& h, const float z[1]) {
          h.GroupMetallicity[1] = z[0];
        },
        H5::PredType::NATIVE_FLOAT
      );
    }

    if (loadIDs) {
      if (!HDF5Utils::datasetExists(grp, "ID")) {
        throw std::runtime_error("Dataset /Group/ID not found");
      }

      H5::DataSet idDS = grp.openDataSet("ID");
      std::vector<uint64_t> allIDs;
      HDF5Utils::readIntegerDatasetAsU64(idDS, allIDs);

      std::vector<uint64_t> offset(nHalos + 1, 0);
      for (size_t i = 0; i < nHalos; ++i) {
        offset[i + 1] = offset[i] + static_cast<uint64_t>(groupLen[i]);
      }

      if (offset.back() != static_cast<uint64_t>(allIDs.size())) {
        throw std::runtime_error("ID size mismatch: sum(GroupLen) != ID.size()");
      }

      haloIDs_.resize(nHalos);
      for (size_t i = 0; i < nHalos; ++i) {
        const uint64_t a = offset[i];
        const uint64_t b = offset[i + 1];
        haloIDs_[i].assign(allIDs.begin() + a, allIDs.begin() + b);
      }
      haloIDsLoaded_ = true;
    }

    return true;
  }
  catch (const H5::Exception& err) {
    std::fprintf(stderr, "HDF5 Error in HaloStore::loadFromHDF5: %s\n", err.getCDetailMsg());
    clear();
    return false;
  }
  catch (const std::exception& e) {
    std::fprintf(stderr, "Error in HaloStore::loadFromHDF5: %s\n", e.what());
    clear();
    return false;
  }
}
#endif
