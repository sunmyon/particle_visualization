#pragma once

#include <H5Cpp.h>
#include <hdf5.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <vector>

struct H5SilenceErrors {
  H5E_auto2_t old_func = nullptr;
  void* old_client = nullptr;
  bool active = false;

  explicit H5SilenceErrors(bool on = true) {
    if (!on) return;
    active = true;
    H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client);
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
  }

  ~H5SilenceErrors() {
    if (!active) return;
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client);
  }

  H5SilenceErrors(const H5SilenceErrors&) = delete;
  H5SilenceErrors& operator=(const H5SilenceErrors&) = delete;
};

namespace HDF5Utils {

// -----------------------------
// existence / metadata
// -----------------------------
inline bool linkExists(hid_t loc, const std::string& name) {
  return H5Lexists(loc, name.c_str(), H5P_DEFAULT) > 0;
}

inline bool datasetExists(const H5::Group& grp, const std::string& name) {
  return linkExists(grp.getId(), name);
}

inline bool groupExists(const H5::H5File& file, const std::string& groupPath) {
  return linkExists(file.getId(), groupPath);
}

inline void removeIfExists(H5::Group& group, const std::string& name) {
  if (datasetExists(group, name)) {
    group.unlink(name);
  }
}

struct H5DatasetMeta {
  int rank = 0;
  std::array<hsize_t, 2> dims{0, 0};
  H5T_class_t cls = H5T_NO_CLASS;
  size_t bytes = 0;
  H5T_sign_t sign = H5T_SGN_ERROR;
};

inline H5DatasetMeta getDatasetMeta(const H5::DataSet& ds) {
  H5DatasetMeta m;

  H5::DataSpace sp = ds.getSpace();
  m.rank = sp.getSimpleExtentNdims();

  if (m.rank >= 1) {
    hsize_t dims[2] = {0, 0};
    sp.getSimpleExtentDims(dims);
    m.dims[0] = dims[0];
    m.dims[1] = (m.rank >= 2) ? dims[1] : 0;
  }

  m.cls = ds.getTypeClass();
  H5::DataType dt = ds.getDataType();
  m.bytes = dt.getSize();

  if (m.cls == H5T_INTEGER) {
    H5::IntType it(dt.getId());
    m.sign = it.getSign();
  }

  return m;
}

// -----------------------------
// type mapping
// -----------------------------
template<typename T>
struct H5MemType {
  static_assert(sizeof(T) == 0, "H5MemType<T> is not specialized for this type");
};

template<> struct H5MemType<char>     { static H5::PredType type() { return H5::PredType::NATIVE_CHAR;   } };
template<> struct H5MemType<int>      { static H5::PredType type() { return H5::PredType::NATIVE_INT;    } };
template<> struct H5MemType<int64_t>  { static H5::PredType type() { return H5::PredType::NATIVE_INT64;  } };
template<> struct H5MemType<uint32_t> { static H5::PredType type() { return H5::PredType::NATIVE_UINT32; } };
template<> struct H5MemType<uint64_t> { static H5::PredType type() { return H5::PredType::NATIVE_UINT64; } };
template<> struct H5MemType<float>    { static H5::PredType type() { return H5::PredType::NATIVE_FLOAT;  } };
template<> struct H5MemType<double>   { static H5::PredType type() { return H5::PredType::NATIVE_DOUBLE; } };

// -----------------------------
// attribute readers
// -----------------------------
template<typename T>
bool readAttributeScalar(H5::Group& grp, const std::string& attrName, T& value)
{
  if (!grp.attrExists(attrName)) return false;

  H5::Attribute attr = grp.openAttribute(attrName);
  H5::DataSpace sp = attr.getSpace();
  if (sp.getSimpleExtentNpoints() != 1) {
    std::cerr << "Attribute '" << attrName << "' is not scalar.\n";
    return false;
  }

  attr.read(H5MemType<T>::type(), &value);
  return true;
}

inline bool readAttributeScalar(H5::Group& grp, const std::string& attrName, bool& value)
{
  if (!grp.attrExists(attrName)) return false;

  try {
    H5::Attribute attr = grp.openAttribute(attrName);
    H5::DataSpace sp = attr.getSpace();
    if (sp.getSimpleExtentNpoints() != 1) return false;

    int tmp = 0;
    attr.read(H5::PredType::NATIVE_INT, &tmp);
    value = (tmp != 0);
    return true;
  } catch (...) {
    return false;
  }
}

template<typename T, size_t N>
bool readAttributeArray(H5::Group& grp, const std::string& attrName, T (&arr)[N])
{
  if (!grp.attrExists(attrName)) {
    std::cerr << "Attribute '" << attrName << "' not found.\n";
    return false;
  }

  H5::Attribute attr = grp.openAttribute(attrName);
  H5::DataSpace dspace = attr.getSpace();
  hssize_t totalElements = dspace.getSimpleExtentNpoints();
  if (totalElements != static_cast<hssize_t>(N)) {
    std::cerr << "Attribute '" << attrName << "' has " << totalElements
              << " elements, expected " << N << ".\n";
    return false;
  }

  attr.read(H5MemType<T>::type(), arr);
  return true;
}

// -----------------------------
// 1D dataset readers / writers
// -----------------------------
template<typename T>
inline void writeDataset1D(H5::Group& group,
                           const std::string& dsetName,
                           const std::vector<T>& data)
{
  removeIfExists(group, dsetName);

  hsize_t dims[1] = { static_cast<hsize_t>(data.size()) };
  H5::DataSpace dsp(1, dims);
  H5::DataSet dset = group.createDataSet(dsetName, H5MemType<T>::type(), dsp);
  dset.write(data.data(), H5MemType<T>::type());
}

template<typename T>
inline void readDataset1D(const H5::Group& grp,
                          const std::string& dname,
                          std::vector<T>& dst)
{
  if (!datasetExists(grp, dname)) {
    dst.clear();
    return;
  }

  H5::DataSet dset = grp.openDataSet(dname);
  H5::DataSpace dsp = dset.getSpace();

  const int rank = dsp.getSimpleExtentNdims();
  if (rank != 1) {
    throw std::runtime_error(dname + " is not a 1D dataset");
  }

  hsize_t dims[1] = {0};
  dsp.getSimpleExtentDims(dims, nullptr);

  dst.resize(static_cast<size_t>(dims[0]));
  dset.read(dst.data(), H5MemType<T>::type());
}

// -----------------------------
// dataset -> object array
// dataset rank: 1 or 2
// -----------------------------
template<typename TObject, typename TValue, size_t NCOMP, typename Setter>
inline void readDatasetToObjects(H5::Group& group,
                                 const std::string& name,
                                 std::vector<TObject>& objects,
                                 Setter setter,
                                 H5::PredType memType = H5MemType<TValue>::type())
{
  H5::DataSet ds = group.openDataSet(name);
  H5::DataSpace dsp = ds.getSpace();

  hsize_t dims[2] = {0, 0};
  dsp.getSimpleExtentDims(dims, nullptr);
  const int ndims = dsp.getSimpleExtentNdims();

  if (ndims == 1) {
    if (NCOMP != 1) {
      throw std::runtime_error("Dataset " + name + " is 1D but NCOMP != 1");
    }
    if (dims[0] != objects.size()) {
      throw std::runtime_error("Dimension mismatch in " + name);
    }
  } else if (ndims == 2) {
    if (dims[1] != NCOMP) {
      throw std::runtime_error("Dataset " + name + " second dimension mismatch");
    }
    if (dims[0] != objects.size()) {
      throw std::runtime_error("Dimension mismatch in " + name);
    }
  } else {
    throw std::runtime_error("Dataset " + name + " has unsupported rank: " + std::to_string(ndims));
  }

  std::vector<TValue> buf(static_cast<size_t>(dims[0]) * NCOMP);
  ds.read(buf.data(), memType);

  for (size_t i = 0; i < static_cast<size_t>(dims[0]); ++i) {
    TValue tmp[NCOMP];
    for (size_t j = 0; j < NCOMP; ++j) {
      tmp[j] = buf[NCOMP * i + j];
    }
    setter(objects[i], tmp);
  }
}

// -----------------------------
// integer dataset -> uint64_t
// -----------------------------
inline void readIntegerDatasetAsU64(H5::DataSet& ds, std::vector<uint64_t>& out)
{
  H5::DataSpace sp = ds.getSpace();
  hsize_t dims[1] = {0};
  sp.getSimpleExtentDims(dims, nullptr);
  out.resize(static_cast<size_t>(dims[0]));

  H5::DataType dt = ds.getDataType();
  if (dt.getClass() != H5T_INTEGER) {
    throw std::runtime_error("Dataset is not integer");
  }

  H5::IntType it(dt.getId());
  const size_t nbytes = it.getSize();
  const bool isSigned = (it.getSign() == H5T_SGN_2);

  if (nbytes == 4 && isSigned) {
    std::vector<int32_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_INT32);
    for (size_t i = 0; i < tmp.size(); ++i) out[i] = static_cast<uint64_t>(static_cast<int64_t>(tmp[i]));
  } else if (nbytes == 4 && !isSigned) {
    std::vector<uint32_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_UINT32);
    for (size_t i = 0; i < tmp.size(); ++i) out[i] = static_cast<uint64_t>(tmp[i]);
  } else if (nbytes == 8 && isSigned) {
    std::vector<int64_t> tmp(out.size());
    ds.read(tmp.data(), H5::PredType::NATIVE_INT64);
    for (size_t i = 0; i < tmp.size(); ++i) out[i] = static_cast<uint64_t>(tmp[i]);
  } else if (nbytes == 8 && !isSigned) {
    ds.read(out.data(), H5::PredType::NATIVE_UINT64);
  } else {
    throw std::runtime_error("Unsupported integer size for integer dataset");
  }
}

// -----------------------------
// hyperslab readers
// -----------------------------
template<typename T, int D>
void readHyperslab(H5::DataSet& ds, T* out, size_t idx)
{
  H5::DataSpace fs = ds.getSpace();

  hsize_t dims[D];
  fs.getSimpleExtentDims(dims);

  hsize_t offset[D] = {};
  hsize_t count[D];
  for (int i = 0; i < D; ++i) {
    count[i] = (i == 0 ? 1 : dims[i]);
  }
  offset[0] = static_cast<hsize_t>(idx);

  fs.selectHyperslab(H5S_SELECT_SET, count, offset);
  H5::DataSpace ms(D, count);

  ds.read(out, H5MemType<T>::type(), ms, fs);
}

inline void readHyperslabBytes(H5::DataSet& ds,
                               const H5::PredType& memType,
                               hsize_t start0,
                               hsize_t count0,
                               int comps,
                               std::vector<uint8_t>& buf,
                               size_t elemSize)
{
  H5::DataSpace fileSpace = ds.getSpace();
  const int nd = fileSpace.getSimpleExtentNdims();
  if (!(nd == 1 || nd == 2)) {
    throw std::runtime_error("readHyperslabBytes: unsupported rank");
  }

  hsize_t offset[2] = {0, 0};
  hsize_t slab[2]   = {0, 0};

  if (nd == 1) {
    offset[0] = start0;
    slab[0] = count0;
    comps = 1;
  } else {
    offset[0] = start0;
    offset[1] = 0;
    slab[0] = count0;
    slab[1] = static_cast<hsize_t>(comps);
  }

  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(nd, slab);

  buf.resize(static_cast<size_t>(count0) * static_cast<size_t>(comps) * elemSize);
  ds.read(buf.data(), memType, memSpace, fileSpace);
}

inline bool readHyperslabBytesChecked(const H5::DataSet& ds,
                                      const H5::PredType& memType,
                                      hsize_t start0,
                                      hsize_t n0,
                                      int comps,
                                      std::vector<uint8_t>& buf,
                                      size_t elemSz)
{
  H5::DataSpace fsp = ds.getSpace();
  const int rank = fsp.getSimpleExtentNdims();
  if (!(rank == 1 || rank == 2)) return false;

  hsize_t dims[2] = {0, 0};
  fsp.getSimpleExtentDims(dims);
  if (start0 + n0 > dims[0]) return false;

  hsize_t count[2] = { n0, 1 };
  hsize_t start[2] = { start0, 0 };

  if (rank == 2) {
    if (static_cast<int>(dims[1]) < comps) return false;
    count[1] = static_cast<hsize_t>(comps);
  } else {
    if (comps != 1) return false;
  }

  fsp.selectHyperslab(H5S_SELECT_SET, count, start);
  H5::DataSpace msp(rank, count);

  const size_t nElem = static_cast<size_t>(n0) * static_cast<size_t>(comps);
  buf.resize(nElem * elemSz);

  ds.read(buf.data(), memType, msp, fsp);
  return true;
}

} // namespace HDF5Utils
