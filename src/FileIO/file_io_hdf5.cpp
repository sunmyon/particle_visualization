#ifdef HAVE_HDF5
#include "main.h"
#include "FileIO/file_io.h"
#include "H5Cpp.h"

#include <hdf5.h>    // H5Lexists, H5P_DEFAULT

template<typename T, typename U, size_t NCOMP, typename Setter>
void readDatasetHdf5(H5::Group &group,
                     const std::string &name,
                     TrackingVector<T> &particles,
                     Setter setter,
                     H5::PredType memType){
  // データセットを開く
  H5::DataSet ds = group.openDataSet(name);

  // 次元を取得
  H5::DataSpace dsp = ds.getSpace();

  hsize_t dims[2];
  dsp.getSimpleExtentDims(dims, nullptr);
  // dims[0] = 粒子数N, dims[1] =  NCOMP(のはず)
    
  int ndims = dsp.getSimpleExtentNdims();
  if(ndims == 1) {
    // もし1次元なら NCOMPは1 であることを確認
    if(NCOMP != 1) 
      throw std::runtime_error("Dataset " + name + " is 1D but NCOMP != 1");      
  } else if(ndims == 2) {
    if(dims[1] != NCOMP) 
      throw std::runtime_error("Dataset " + name + " second dimension is not equal to NCOMP");      

    if(dims[0] != particles.size())
      throw std::runtime_error("Dimension mismatch in " + name);	 
  } else {
    throw std::runtime_error("Dataset " + name + " has unsupported rank: " + std::to_string(ndims));
  }
    
  // HDF5から読み出し用のバッファを確保
  TrackingVector<U> buf(dims[0] * NCOMP);
  ds.read(buf.data(), memType);

  // 読み込んだバッファを構造体メンバへコピー
  for(size_t i=0; i<dims[0]; i++){
    U tmp[NCOMP];
    for (size_t j = 0; j < NCOMP; j++) 
      tmp[j] = buf[NCOMP * i + j];
      
    setter(particles[i], tmp);
  }
};

static void readIDDatasetAsU64(H5::DataSet& ds, std::vector<uint64_t>& out)
{
  H5::DataSpace sp = ds.getSpace();
  hsize_t dims[1]; sp.getSimpleExtentDims(dims, nullptr);
  out.resize((size_t)dims[0]);

  H5::DataType dt = ds.getDataType();
  if (dt.getClass() != H5T_INTEGER)
    throw std::runtime_error("ID dataset is not integer");

  H5::IntType it(dt.getId());
  const size_t nbytes = it.getSize();          // 4 or 8
  const bool isSigned = (it.getSign() == H5T_SGN_2);

  if (nbytes == 4 && isSigned) {
    std::vector<int32_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_INT32);
    for (size_t i=0;i<tmp.size();++i) out[i] = (uint64_t)(int64_t)tmp[i];
  } else if (nbytes == 4 && !isSigned) {
    std::vector<uint32_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_UINT32);
    for (size_t i=0;i<tmp.size();++i) out[i] = (uint64_t)tmp[i];
  } else if (nbytes == 8 && isSigned) {
    std::vector<int64_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_INT64);
    for (size_t i=0;i<tmp.size();++i) out[i] = (uint64_t)tmp[i];
  } else if (nbytes == 8 && !isSigned) {
    ds.read(out.data(), H5::PredType::NATIVE_UINT64);
  } else {
    throw std::runtime_error("Unsupported integer size for ID dataset");
  }
}


HaloCatalog FileInfo::readHaloCatalogFromHDF5(char *fname, bool loadIDs /*=true*/)
{
  HaloCatalog out;

  try {
    H5::H5File file(fname, H5F_ACC_RDONLY);

    const char* gpath = "/Group";
    if(!groupExists(file, gpath)){
      std::cout << gpath << " does not exist. Skipping.\n";
      return out;
    }
    H5::Group grp = file.openGroup(gpath);

    // --- halo数（例: GroupPos から）
    H5::DataSet posDS = grp.openDataSet("GroupPos");
    H5::DataSpace posSpace = posDS.getSpace();
    hsize_t posDims[2]; posSpace.getSimpleExtentDims(posDims, nullptr);
    const size_t nHalos = (size_t)posDims[0];

    out.haloes = TrackingVector<HaloData>(nHalos);

    // --- まず GroupLen を読む（IDs切り出しに必要）
    std::vector<int> groupLen(nHalos, 0);
    {
      // 既存のテンプレ readDatasetHdf5 を使って haloes[i].GroupLen に入れても良いが、
      // ID切り出し用に vector<int> も欲しいので、ここは直接読むのが便利
      H5::DataSet lenDS = grp.openDataSet("GroupLen");
      lenDS.read(groupLen.data(), H5::PredType::NATIVE_INT);

      for (size_t i=0;i<nHalos;i++) out.haloes[i].GroupLen = groupLen[i];
    }

    // --- GroupPos
    readDatasetHdf5<HaloData, float, 3>(
      grp, "GroupPos", out.haloes,
      [](HaloData &p, const float xyz[3]){
        p.GroupPos[0]=xyz[0]; p.GroupPos[1]=xyz[1]; p.GroupPos[2]=xyz[2];
      },
      H5::PredType::NATIVE_FLOAT
    );

    // --- GroupVel
    readDatasetHdf5<HaloData, float, 3>(
      grp, "GroupVel", out.haloes,
      [](HaloData &p, const float v[3]){
        p.GroupVel[0]=v[0]; p.GroupVel[1]=v[1]; p.GroupVel[2]=v[2];
      },
      H5::PredType::NATIVE_FLOAT
    );

    // --- GroupMass
    readDatasetHdf5<HaloData, float, 1>(
      grp, "GroupMass", out.haloes,
      [](HaloData &p, const float m[1]){ p.GroupMass = m[0]; },
      H5::PredType::NATIVE_FLOAT
    );

    // --- GroupMassType
    readDatasetHdf5<HaloData, float, 6>(
      grp, "GroupMassType", out.haloes,
      [](HaloData &p, const float m[6]){
        for(int k=0;k<6;k++) p.GroupMassType[k]=m[k];
      },
      H5::PredType::NATIVE_FLOAT
    );

    // --- GroupLenType
    if (H5Lexists(grp.getId(), "GroupLenType", H5P_DEFAULT) > 0) {
      readDatasetHdf5<HaloData, int, 6>(
        grp, "GroupLenType", out.haloes,
        [](HaloData &p, const int m[6]){
          for(int k=0;k<6;k++) p.GroupLenType[k]=m[k];
        },
        H5::PredType::NATIVE_INT
      );
    }

    // --- metallicity 2本
    if (H5Lexists(grp.getId(), "GroupGasMetallicity", H5P_DEFAULT) > 0) {
      readDatasetHdf5<HaloData, float, 1>(
        grp, "GroupGasMetallicity", out.haloes,
        [](HaloData &p, const float z[1]){ p.GroupMetallicity[0]=z[0]; },
        H5::PredType::NATIVE_FLOAT
      );
    }
    if (H5Lexists(grp.getId(), "GroupStarMetallicity", H5P_DEFAULT) > 0) {
      readDatasetHdf5<HaloData, float, 1>(
        grp, "GroupStarMetallicity", out.haloes,
        [](HaloData &p, const float z[1]){ p.GroupMetallicity[1]=z[0]; },
        H5::PredType::NATIVE_FLOAT
      );
    }

    // --- IDs も同じ関数内で読む（必要な時だけ）
    if (loadIDs) {
      if (H5Lexists(grp.getId(), "ID", H5P_DEFAULT) <= 0)
        throw std::runtime_error("Dataset /Group/ID not found");

      H5::DataSet idDS = grp.openDataSet("ID");
      std::vector<uint64_t> allIDs;
      readIDDatasetAsU64(idDS, allIDs);

      // prefix-sum offsets
      std::vector<uint64_t> offset(nHalos+1, 0);
      for (size_t i=0;i<nHalos;i++) offset[i+1] = offset[i] + (uint64_t)groupLen[i];

      if (offset.back() != (uint64_t)allIDs.size())
        throw std::runtime_error("ID size mismatch: sum(GroupLen) != ID.size()");

      out.haloIDs.resize(nHalos);
      for (size_t i=0;i<nHalos;i++) {
        uint64_t a = offset[i], b = offset[i+1];
        out.haloIDs[i].assign(allIDs.begin()+a, allIDs.begin()+b);
      }
    }

    file.close();
    return out;
  }
  catch(const H5::Exception &err) {
    std::fprintf(stderr, "HDF5 Error in readHaloCatalogFromHDF5: %s\n", err.getCDetailMsg());
    return HaloCatalog{};
  }
  catch(const std::exception &e) {
    std::fprintf(stderr, "Error in readHaloCatalogFromHDF5: %s\n", e.what());
    return HaloCatalog{};
  }
}
#endif
