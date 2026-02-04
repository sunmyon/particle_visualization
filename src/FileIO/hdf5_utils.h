#pragma once
#include <H5Cpp.h>
#include <stdexcept>

struct H5SilenceErrors {
  H5E_auto2_t old_func = nullptr;
  void* old_client = nullptr;
  bool active = false;

  explicit H5SilenceErrors(bool on = true) {
    if (!on) return;
    active = true;
    H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client);   // 現状保存
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);         // 全停止
  }

  ~H5SilenceErrors() {
    if (!active) return;
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client);     // 完全復元
  }

  H5SilenceErrors(const H5SilenceErrors&) = delete;
  H5SilenceErrors& operator=(const H5SilenceErrors&) = delete;
};

static bool groupExists(const H5::H5File &file, const std::string &groupPath) {
  // H5Lexists は C API。C++ の file.getId() で hid_t を取ってくる
  herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
  return (status > 0);
}

template<typename T>
struct H5MemType {
  static_assert(sizeof(T) == 0, "H5MemType<T> is not specialized for this type");
};

template<> struct H5MemType<float>  { static H5::PredType type(){ return H5::PredType::NATIVE_FLOAT;  } };
template<> struct H5MemType<double> { static H5::PredType type(){ return H5::PredType::NATIVE_DOUBLE; } };
template<> struct H5MemType<int32_t>{ static H5::PredType type(){ return H5::PredType::NATIVE_INT32;  } };
template<> struct H5MemType<int64_t>{ static H5::PredType type(){ return H5::PredType::NATIVE_INT64;  } };
template<> struct H5MemType<uint32_t>{static H5::PredType type(){ return H5::PredType::NATIVE_UINT32; } };
template<> struct H5MemType<uint64_t>{static H5::PredType type(){ return H5::PredType::NATIVE_UINT64; } };


template<typename T>
bool readAttributeScalar(H5::Group &grp, const std::string &attrName, T &value)
{
  if(!grp.attrExists(attrName)) return false;

  H5::Attribute attr = grp.openAttribute(attrName);

  // スカラーか確認（任意だが事故防止におすすめ）
  H5::DataSpace sp = attr.getSpace();
  if (sp.getSimpleExtentNpoints() != 1) {
    std::cerr << "Attribute '" << attrName << "' is not scalar.\n";
    return false;
  }
    
  attr.read(H5MemType<T>::type(), &value);
  return true;
};

inline bool readAttributeScalar(H5::Group& grp, const std::string& attrName, bool& value)
{
  if (!grp.attrExists(attrName)) return false;

  try {
    H5::Attribute attr = grp.openAttribute(attrName);

    H5::DataSpace sp = attr.getSpace();
    if (sp.getSimpleExtentNpoints() != 1) return false;

    // まずは int で読む（HDF5が変換してくれる）
    int tmp = 0;
    attr.read(H5::PredType::NATIVE_INT, &tmp);
    value = (tmp != 0);
    return true;
  } catch (...) {
    return false;
  }
}

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

  attr.read(H5MemType<T>::type(), arr);
  return true;
};

inline bool readHyperslabBytes(const H5::DataSet& ds,
                               const H5::PredType& memType,
                               hsize_t start0,
                               hsize_t n0,
                               int comps,                      // 期待 comps（rank==2 のとき）
                               std::vector<uint8_t>& buf,
                               size_t elemSz)                  // memType の1要素サイズ
{
  H5::DataSpace fsp = ds.getSpace();
  const int rank = fsp.getSimpleExtentNdims();
  if(!(rank==1 || rank==2)) return false;

  // file dims
  hsize_t dims[2] = {0,0};
  fsp.getSimpleExtentDims(dims);

  if(start0 + n0 > dims[0]) return false;

  hsize_t count[2] = { n0, 1 };
  hsize_t start[2] = { start0, 0 };

  if(rank==2){
    if((int)dims[1] < comps) return false;      // ファイルの第2次元が足りない
    count[1] = (hsize_t)comps;
  } else {
    // rank==1 のとき comps は 1 を期待
    if(comps != 1) return false;
  }

  // file hyperslab
  fsp.selectHyperslab(H5S_SELECT_SET, count, start);

  // mem space
  H5::DataSpace msp(rank, count);

  const size_t nElem = (size_t)n0 * (size_t)comps;
  buf.resize(nElem * elemSz);

  ds.read(buf.data(), memType, msp, fsp);
  return true;
}

struct H5DatasetMeta {
  int rank = 0;                       // 1 or 2 を想定
  std::array<hsize_t,2> dims{0,0};    // rank==1なら dims[0]=N, dims[1]=0
  H5T_class_t cls = H5T_NO_CLASS;     // H5T_FLOAT / H5T_INTEGER など
  size_t bytes = 0;                   // 4 or 8 など
  H5T_sign_t sign = H5T_SGN_ERROR;    // integerのときのみ意味あり
};

// “事実だけ”抜く。判断しない。HDF5例外はそのまま上位へ。
inline H5DatasetMeta getDatasetMeta(const H5::DataSet& ds)
{
  H5DatasetMeta m;

  // dataspace
  H5::DataSpace sp = ds.getSpace();
  m.rank = sp.getSimpleExtentNdims();
  if (m.rank >= 1) {
    hsize_t dims[2] = {0,0};
    sp.getSimpleExtentDims(dims);
    m.dims[0] = dims[0];
    m.dims[1] = (m.rank >= 2) ? dims[1] : 0;
  }

  // datatype
  m.cls   = ds.getTypeClass();
  H5::DataType dt = ds.getDataType();
  m.bytes = dt.getSize();

  if (m.cls == H5T_INTEGER) {
    H5::IntType it(dt.getId());  // signed/unsigned だけ抜く
    m.sign = it.getSign();
  }

  return m;
}
