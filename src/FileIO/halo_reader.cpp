#ifdef HAVE_HDF5
#include "FileIO/file_io.h"
#include "FileIO/hdf5_utils.h"
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

#endif
