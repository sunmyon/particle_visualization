#pragma once
#include <H5Cpp.h>
#include <stdexcept>

static bool groupExists(const H5::H5File &file, const std::string &groupPath) {
  // H5Lexists は C API。C++ の file.getId() で hid_t を取ってくる
  herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
  return (status > 0);
}

template<typename T>
bool readAttributeScalar(H5::Group &grp, const std::string &attrName, T &value)
{
    // 属性が存在しなければ false を返す
    if(!grp.attrExists(attrName)) {
      std::cerr << "Attribute '" << attrName << "' not found in /Header.\n";
      return false;
    }
    // 属性を開く
    H5::Attribute attr = grp.openAttribute(attrName);

    // ファイル側の型を取得
    H5::DataType dtype = attr.getDataType();
    // 読み込む (型変換はHDF5が自動で行う)
    attr.read(dtype, &value);

    return true;
};

inline void readHyperslabBytes(H5::DataSet &ds,
                               const H5::PredType &memType,
                               hsize_t start0,
                               hsize_t count0,
                               int comps,                 // rank=2 の第2次元
                               std::vector<uint8_t> &buf,
                               size_t elemSize)
{
  H5::DataSpace fileSpace = ds.getSpace();
  int nd = fileSpace.getSimpleExtentNdims();
  if (!(nd == 1 || nd == 2)) {
    throw std::runtime_error("readHyperslabBytes: unsupported rank");
  }

  hsize_t offset[2] = {0, 0};
  hsize_t slab[2]   = {0, 0};

  if (nd == 1) {
    offset[0] = start0;
    slab[0]   = count0;
    comps     = 1;
  } else {
    offset[0] = start0; offset[1] = 0;
    slab[0]   = count0; slab[1]   = (hsize_t)comps;
  }

  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(nd, slab);

  buf.resize((size_t)count0 * (size_t)comps * elemSize);
  ds.read(buf.data(), memType, memSpace, fileSpace);
}

template<typename T, int D>
void readHyperslab(H5::DataSet &ds,
                   T *out,        // バッファ (count==1 のときは length=T[1], D==2なら T[3])
                   size_t idx)    // 先頭インデックス
{
  // 1) ファイル空間
  H5::DataSpace fs = ds.getSpace();
  hsize_t dims[D];
  fs.getSimpleExtentDims(dims);

  // 2) hyperslab のオフセット／ブロック
  hsize_t offset[D] = {};
  hsize_t count[D];
  for (int i = 0; i < D; ++i) count[i] = (i==0 ? 1 : dims[i]);
  offset[0] = idx;
  fs.selectHyperslab(H5S_SELECT_SET, count, offset);

  // 3) メモリ空間
  H5::DataSpace ms(D, count);

  // 4) 読み込み
  ds.read(out,
          std::is_same<T,float>::value   ? H5::PredType::NATIVE_FLOAT
        : std::is_same<T,int32_t>::value ? H5::PredType::NATIVE_INT32
        :                                   H5::PredType::NATIVE_DOUBLE,
          ms, fs);
}


template<typename T, size_t N>
  bool readAttributeArray(H5::Group &grp, const std::string &attrName, T (&arr)[N])
{
  if(!grp.attrExists(attrName)) {
    std::cerr << "Attribute '" << attrName << "' not found in /Header.\n";
    return false;
  }
  H5::Attribute attr = grp.openAttribute(attrName);
  
  // データスペースをチェック (要素数 N と一致するか確認)
  H5::DataSpace dspace = attr.getSpace();
  hssize_t totalElements = dspace.getSimpleExtentNpoints();
  if(totalElements != N) {
    std::cerr << "Attribute '" << attrName << "' has " << totalElements
	      << " elements, expected " << N << ".\n";
    return false;
  }

  H5::DataType dtype = attr.getDataType();
  attr.read(dtype, arr); // 配列全体を読み込み

  return true;
};
