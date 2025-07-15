#ifndef HDF5HELPERS_H
#define HDF5HELPERS_H

#include <string>
#include <stdexcept>
#include <unordered_map>
#include <H5Cpp.h>
#include "main.h"  // TrackingVector の定義があるヘッダー


namespace HDF5Helper {
  // ヘルパー: 既存データセットを消す
  inline void removeIfExists(H5::Group &group, const std::string &name) {    
    if (H5Lexists(group.getId(), name.c_str(), H5P_DEFAULT) > 0) 
      group.unlink(name);    
  }

  inline bool datasetExists(const H5::Group &grp, const std::string &name) {
    return (H5Lexists(grp.getId(), name.c_str(), H5P_DEFAULT) > 0);
  }
  
  template<typename T>
    H5::DataType getH5Type();
  
  // int 用の特殊化
  template<>
    inline H5::DataType getH5Type<int>() {
    return H5::PredType::NATIVE_INT;
  }

  // int 用の特殊化
  template<>
    inline H5::DataType getH5Type<char>() {
    return H5::PredType::NATIVE_CHAR;
  }
  
  // float 用の特殊化
  template<>
    inline H5::DataType getH5Type<float>() {
    return H5::PredType::NATIVE_FLOAT;
  }
  
  // double 用の特殊化 (必要に応じて追加)
  template<>
    inline H5::DataType getH5Type<double>() {
    return H5::PredType::NATIVE_DOUBLE;
  }
  
  /**
   * @brief 1次元配列を HDF5 データセットとして書き込む (removeIfExistsは外部で定義)
   *
   * @tparam T int, float, double など
   * @param group     書き込み先の HDF5 グループ
   * @param dsetName  作成・上書きするデータセット名
   * @param data      書き込むベクタ配列
   */
  template<typename T>
    inline void writeArray1D(H5::Group &group,
			     const std::string &dsetName,
			     const TrackingVector<T> &data)
    {
      removeIfExists(group, dsetName);
    
      hsize_t dims[1] = { static_cast<hsize_t>(data.size()) };
      H5::DataSpace dsp(1, dims);
      H5::DataType h5type = getH5Type<T>();
      H5::DataSet dset = group.createDataSet(dsetName, h5type, dsp);
    
      dset.write(data.data(), h5type);
    }  

  
  template<typename T>
    inline void readArray1D(const H5::Group &grp, const std::string &dname, TrackingVector<T> &dst)
    {
      // データセットが存在しなければ dst.clear()
      if (!datasetExists(grp, dname)) {
	dst.clear();
	return;
      }
      // データセットを開く
      H5::DataSet dset = grp.openDataSet(dname);
      H5::DataSpace dsp = dset.getSpace();

      // 次元数を取得し、1次元かどうかチェック
      int rank = dsp.getSimpleExtentNdims();
      if (rank != 1) {
	throw std::runtime_error(dname + " is not 1D dataset");
      }

      // データセットのサイズを取得
      hsize_t dims[1];
      dsp.getSimpleExtentDims(dims, nullptr);

      // dst をリサイズ
      dst.resize(static_cast<size_t>(dims[0]));

      // 実際の読み込み
      H5::DataType h5type = getH5Type<T>();
      dset.read(dst.data(), h5type);
    }

  template<typename T, typename U, size_t NCOMP>
    inline void readDatasetHdf5(H5::Group &group,
				const std::string &name,
				TrackingVector<T> &particles,
				void (*setter)(T &, const U *),
				H5::PredType memType)
    {
      H5::DataSet ds = group.openDataSet(name);
      H5::DataSpace dsp = ds.getSpace();

      hsize_t dims[2];
      dsp.getSimpleExtentDims(dims, nullptr);
      int ndims = dsp.getSimpleExtentNdims();
      if(ndims == 1) {
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

      TrackingVector<U> buf(dims[0] * NCOMP);
      ds.read(buf.data(), memType);

      for(size_t i = 0; i < dims[0]; i++){
        U tmp[NCOMP];
        for (size_t j = 0; j < NCOMP; j++)
	  tmp[j] = buf[NCOMP * i + j];

        setter(particles[i], tmp);
      }
    }

  inline bool groupExists(const H5::H5File &file, const std::string &groupPath)
  {
    herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
    return (status > 0);
  }

}
#endif
