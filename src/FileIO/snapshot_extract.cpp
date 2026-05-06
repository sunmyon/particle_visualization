#include "FileIO/snapshot_extract.h"

#ifdef HAVE_HDF5

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "FileIO/hdf5_utils.h"
#include "data/simulation_block.h"

namespace {

constexpr std::size_t kExtractChunkSize = 1u << 16;

std::string PartGroupName(int ptype)
{
  return "/PartType" + std::to_string(ptype);
}

std::string DatasetPath(int ptype, const std::string& dataset)
{
  return PartGroupName(ptype) + "/" + dataset;
}

bool LinkExists(hid_t loc, const std::string& name)
{
  return H5Lexists(loc, name.c_str(), H5P_DEFAULT) > 0;
}

bool DatasetExists(H5::H5File& file, const std::string& path)
{
  return LinkExists(file.getId(), path);
}

template<typename T>
void WriteScalarAttribute(H5::Group& group,
                          const std::string& name,
                          const H5::PredType& type,
                          const T& value)
{
  if (group.attrExists(name)) group.removeAttr(name);
  H5::DataSpace scalar(H5S_SCALAR);
  H5::Attribute attr = group.createAttribute(name, type, scalar);
  attr.write(type, &value);
}

template<typename T, std::size_t N>
void WriteArrayAttribute(H5::Group& group,
                         const std::string& name,
                         const H5::PredType& type,
                         const std::array<T, N>& values)
{
  if (group.attrExists(name)) group.removeAttr(name);
  hsize_t dims[1] = {static_cast<hsize_t>(N)};
  H5::DataSpace space(1, dims);
  H5::Attribute attr = group.createAttribute(name, type, space);
  attr.write(type, values.data());
}

template<typename T, std::size_t N>
bool ReadArrayAttribute(H5::Group& group,
                        const std::string& name,
                        const H5::PredType& type,
                        std::array<T, N>& values)
{
  if (!group.attrExists(name)) return false;
  H5::Attribute attr = group.openAttribute(name);
  H5::DataSpace space = attr.getSpace();
  if (space.getSimpleExtentNpoints() != static_cast<hssize_t>(N)) return false;
  attr.read(type, values.data());
  return true;
}

template<typename T>
bool ReadScalarAttribute(H5::Group& group,
                         const std::string& name,
                         const H5::PredType& type,
                         T& value)
{
  if (!group.attrExists(name)) return false;
  H5::Attribute attr = group.openAttribute(name);
  H5::DataSpace space = attr.getSpace();
  if (space.getSimpleExtentNpoints() != 1) return false;
  attr.read(type, &value);
  return true;
}

struct SourceHeader {
  std::array<std::size_t, 6> counts = {0, 0, 0, 0, 0, 0};
  std::array<double, 6> massTable = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double time = 0.0;
  double redshift = 0.0;
  double boxSize = 0.0;
  double omega0 = 0.0;
  double omegaLambda = 0.0;
  double hubbleParam = 1.0;
  bool hasRedshift = false;
};

std::size_t InferCount(H5::H5File& file, int ptype)
{
  const std::array<const char*, 3> probes = {
    "Coordinates",
    "Velocities",
    "ParticleIDs"
  };
  for (const char* name : probes) {
    const std::string path = DatasetPath(ptype, name);
    if (!DatasetExists(file, path)) continue;
    H5::DataSet ds = file.openDataSet(path);
    H5::DataSpace sp = ds.getSpace();
    if (sp.getSimpleExtentNdims() < 1) continue;
    hsize_t dims[2] = {0, 0};
    sp.getSimpleExtentDims(dims);
    return static_cast<std::size_t>(dims[0]);
  }
  return 0;
}

SourceHeader ReadSourceHeader(H5::H5File& file)
{
  SourceHeader header;
  try {
    H5::Group group = file.openGroup("/Header");
    (void)ReadScalarAttribute(group, "Time", H5::PredType::NATIVE_DOUBLE, header.time);
    (void)ReadScalarAttribute(group, "BoxSize", H5::PredType::NATIVE_DOUBLE, header.boxSize);
    (void)ReadScalarAttribute(group, "Omega0", H5::PredType::NATIVE_DOUBLE, header.omega0);
    (void)ReadScalarAttribute(group, "OmegaLambda", H5::PredType::NATIVE_DOUBLE, header.omegaLambda);
    (void)ReadScalarAttribute(group, "HubbleParam", H5::PredType::NATIVE_DOUBLE, header.hubbleParam);
    header.hasRedshift =
      ReadScalarAttribute(group, "Redshift", H5::PredType::NATIVE_DOUBLE, header.redshift);
    (void)ReadArrayAttribute(group,
                             "MassTable",
                             H5::PredType::NATIVE_DOUBLE,
                             header.massTable);

    std::array<unsigned int, 6> counts32 = {0, 0, 0, 0, 0, 0};
    if (ReadArrayAttribute(group,
                           "NumPart_ThisFile",
                           H5::PredType::NATIVE_UINT,
                           counts32)) {
      for (int i = 0; i < 6; ++i) header.counts[i] = counts32[i];
    } else if (ReadArrayAttribute(group,
                                  "NumPart_Total",
                                  H5::PredType::NATIVE_UINT,
                                  counts32)) {
      for (int i = 0; i < 6; ++i) header.counts[i] = counts32[i];
    }
  } catch (...) {
  }

  bool allZero = true;
  for (std::size_t count : header.counts) {
    if (count != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    for (int ptype = 0; ptype < 6; ++ptype) {
      header.counts[ptype] = InferCount(file, ptype);
    }
  }
  return header;
}

struct ParametersInfo {
  double unitLength = 1.0;
  double unitMass = 1.0;
  double unitVelocity = 1.0;
  double hubbleParam = 1.0;
  int comoving = 0;
  int flagDensityInCgs = 1;
  int flagBfieldInCgs = 1;
  bool hasGroup = false;
};

ParametersInfo ReadParameters(H5::H5File& file)
{
  ParametersInfo params;
  try {
    H5::Group group = file.openGroup("/Parameters");
    params.hasGroup = true;
    (void)ReadScalarAttribute(group, "UnitLength_in_cm", H5::PredType::NATIVE_DOUBLE, params.unitLength);
    (void)ReadScalarAttribute(group, "UnitMass_in_g", H5::PredType::NATIVE_DOUBLE, params.unitMass);
    (void)ReadScalarAttribute(group,
                              "UnitVelocity_in_cm_per_s",
                              H5::PredType::NATIVE_DOUBLE,
                              params.unitVelocity);
    (void)ReadScalarAttribute(group, "HubbleParam", H5::PredType::NATIVE_DOUBLE, params.hubbleParam);
    (void)ReadScalarAttribute(group, "ComovingIntegrationOn", H5::PredType::NATIVE_INT, params.comoving);
    (void)ReadScalarAttribute(group, "FlagDensityInCgs", H5::PredType::NATIVE_INT, params.flagDensityInCgs);
    (void)ReadScalarAttribute(group, "FlagBfieldInCgs", H5::PredType::NATIVE_INT, params.flagBfieldInCgs);
  } catch (...) {
  }
  return params;
}

double SafePositive(double value, double fallback)
{
  return std::isfinite(value) && value > 0.0 ? value : fallback;
}

double UnitChangeScale(double sourceUnit, double targetUnit)
{
  sourceUnit = SafePositive(sourceUnit, 1.0);
  targetUnit = SafePositive(targetUnit, sourceUnit);
  return sourceUnit / targetUnit;
}

double ComovingLengthScale(const SnapshotExtractUnitConversion& conversion)
{
  const double a = SafePositive(conversion.scaleFactor, 1.0);
  switch (conversion.comovingMode) {
    case SnapshotExtractComovingMode::ComovingToPhysical: return a;
    case SnapshotExtractComovingMode::PhysicalToComoving: return 1.0 / a;
    case SnapshotExtractComovingMode::Preserve:
    default: return 1.0;
  }
}

ParametersInfo ApplyUnitConversionToParameters(
  ParametersInfo params,
  const SnapshotExtractUnitConversion& conversion)
{
  if (!conversion.enabled) return params;
  params.unitLength = SafePositive(conversion.unitLengthCm, params.unitLength);
  params.unitMass = SafePositive(conversion.unitMassG, params.unitMass);
  params.unitVelocity = SafePositive(conversion.unitVelocityCmPerS, params.unitVelocity);
  params.hubbleParam = SafePositive(conversion.hubbleParam, params.hubbleParam);
  if (conversion.comovingMode == SnapshotExtractComovingMode::ComovingToPhysical) {
    params.comoving = 0;
  } else if (conversion.comovingMode == SnapshotExtractComovingMode::PhysicalToComoving) {
    params.comoving = 1;
  }
  return params;
}

ParametersInfo ResolveSourceParameters(ParametersInfo params,
                                       const SnapshotExtractUnitConversion& conversion)
{
  if (!conversion.enabled) return params;
  if (!params.hasGroup) {
    params.unitLength = SafePositive(conversion.sourceUnitLengthCm, params.unitLength);
    params.unitMass = SafePositive(conversion.sourceUnitMassG, params.unitMass);
    params.unitVelocity =
      SafePositive(conversion.sourceUnitVelocityCmPerS, params.unitVelocity);
    params.hubbleParam = SafePositive(conversion.sourceHubbleParam, params.hubbleParam);
    params.flagDensityInCgs = 0;
    params.flagBfieldInCgs = 0;
  }
  return params;
}

double DatasetValueScale(FieldKey key,
                         const ParametersInfo& source,
                         const SnapshotExtractUnitConversion& conversion)
{
  if (!conversion.enabled) return 1.0;

  const double lengthScale =
    UnitChangeScale(source.unitLength, conversion.unitLengthCm) *
    ComovingLengthScale(conversion);
  const double massScale = UnitChangeScale(source.unitMass, conversion.unitMassG);
  const double velocityScale =
    UnitChangeScale(source.unitVelocity, conversion.unitVelocityCmPerS);

  switch (key) {
    case FieldKey::Position:
    case FieldKey::Hsml:
      return lengthScale;
    case FieldKey::Velocity:
      return velocityScale;
    case FieldKey::Mass:
      return massScale;
    case FieldKey::Volume:
      return lengthScale * lengthScale * lengthScale;
    case FieldKey::Density:
      if (source.flagDensityInCgs != 0) return 1.0;
      return massScale / (lengthScale * lengthScale * lengthScale);
    case FieldKey::InternalEnergy:
      return velocityScale * velocityScale;
    case FieldKey::Bfield:
      if (source.flagBfieldInCgs != 0) return 1.0;
      {
        const double sourceHubble = SafePositive(source.hubbleParam, 1.0);
        const double targetHubble =
          SafePositive(conversion.hubbleParam, sourceHubble);
        const double sourceFactor =
          std::sqrt(SafePositive(source.unitMass, 1.0) /
                    SafePositive(source.unitLength, 1.0)) /
          (SafePositive(source.unitLength, 1.0) /
           SafePositive(source.unitVelocity, 1.0) /
           sourceHubble);
        const double targetFactor =
          std::sqrt(SafePositive(conversion.unitMassG, source.unitMass) /
                    SafePositive(conversion.unitLengthCm, source.unitLength)) /
          (SafePositive(conversion.unitLengthCm, source.unitLength) /
           SafePositive(conversion.unitVelocityCmPerS, source.unitVelocity) /
           targetHubble);
        return sourceFactor / targetFactor;
      }
    default:
      return 1.0;
  }
}

bool InsideRegion(const SnapshotExtractRegion& region, const double p[3])
{
  if (region.kind == SnapshotExtractRegionKind::Sphere) {
    const double dx = p[0] - region.center[0];
    const double dy = p[1] - region.center[1];
    const double dz = p[2] - region.center[2];
    return dx * dx + dy * dy + dz * dz <= region.radius * region.radius;
  }
  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(p[axis] - region.center[axis]) > region.halfSize[axis]) {
      return false;
    }
  }
  return true;
}

double ComputeExtractBoxSize(const SnapshotExtractRegion& region,
                             double fallback,
                             double lengthScale)
{
  double size = 0.0;
  if (region.kind == SnapshotExtractRegionKind::Sphere) {
    size = 2.0 * std::max(0.0, region.radius);
  } else {
    const double x = 2.0 * std::max(0.0, region.halfSize[0]);
    const double y = 2.0 * std::max(0.0, region.halfSize[1]);
    const double z = 2.0 * std::max(0.0, region.halfSize[2]);
    size = std::max(x, std::max(y, z));
  }
  if (size <= 0.0) size = fallback;
  return size * lengthScale;
}

void ReadCoordinateChunk(H5::DataSet& coordinates,
                         std::size_t begin,
                         std::size_t count,
                         std::vector<double>& out)
{
  H5::DataSpace fileSpace = coordinates.getSpace();
  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), 3};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  out.resize(count * 3);
  coordinates.read(out.data(), H5::PredType::NATIVE_DOUBLE, memSpace, fileSpace);
}

std::vector<std::uint64_t> BuildKeepList(H5::H5File& file,
                                         int ptype,
                                         std::size_t sourceCount,
                                         const SnapshotExtractRegion& region)
{
  std::vector<std::uint64_t> keep;
  if (sourceCount == 0) return keep;

  const std::string path = DatasetPath(ptype, "Coordinates");
  if (!DatasetExists(file, path)) return keep;

  H5::DataSet coords = file.openDataSet(path);
  H5::DataSpace space = coords.getSpace();
  if (space.getSimpleExtentNdims() != 2) return keep;
  hsize_t dims[2] = {0, 0};
  space.getSimpleExtentDims(dims);
  if (dims[1] != 3) return keep;

  std::vector<double> coordChunk;
  for (std::size_t begin = 0; begin < sourceCount; begin += kExtractChunkSize) {
    const std::size_t n = std::min(kExtractChunkSize, sourceCount - begin);
    ReadCoordinateChunk(coords, begin, n, coordChunk);
    for (std::size_t i = 0; i < n; ++i) {
      const double p[3] = {
        coordChunk[3 * i + 0],
        coordChunk[3 * i + 1],
        coordChunk[3 * i + 2]
      };
      if (InsideRegion(region, p)) {
        keep.push_back(static_cast<std::uint64_t>(begin + i));
      }
    }
  }
  return keep;
}

struct DatasetShape {
  int rank = 0;
  hsize_t components = 1;
  hsize_t sourceRows = 0;
  std::size_t rowBytes = 0;
};

struct BackgroundGridData {
  std::vector<double> coordinates;
  std::vector<std::uint64_t> nearestGasRows;
  std::size_t count = 0;
  bool hasNearestGasRows = false;
  double cellVolume = 0.0;
  double cellLength = 0.0;
  std::uint64_t firstId = 1;
  std::array<int, 3> cells = {0, 0, 0};
  std::array<double, 3> lo = {0.0, 0.0, 0.0};
  std::array<double, 3> cellSize = {0.0, 0.0, 0.0};
};

struct ActiveOutputField {
  FieldSpec field;
  std::string sourceName;
  std::string outputName;
  SnapshotOutputMissingPolicy missingPolicy = SnapshotOutputMissingPolicy::Omit;
  std::vector<double> defaultValues;
  unsigned int typeMask = 0x3fu;
};

std::vector<double> ExpandedDefaultValues(const ActiveOutputField& field);

void CompactBackgroundGridToEmptyCells(BackgroundGridData& grid,
                                       const std::vector<double>& nearestDist2)
{
  if (grid.count == 0 || nearestDist2.size() != grid.count) return;

  const double minDist = 0.5 * grid.cellLength;
  const double minDist2 = minDist * minDist;
  std::vector<double> compactCoords;
  std::vector<std::uint64_t> compactNearest;
  compactCoords.reserve(grid.coordinates.size());
  compactNearest.reserve(grid.nearestGasRows.size());

  for (std::size_t i = 0; i < grid.count; ++i) {
    if (!(nearestDist2[i] > minDist2)) continue;
    compactCoords.push_back(grid.coordinates[3 * i + 0]);
    compactCoords.push_back(grid.coordinates[3 * i + 1]);
    compactCoords.push_back(grid.coordinates[3 * i + 2]);
    compactNearest.push_back(grid.nearestGasRows[i]);
  }

  grid.coordinates.swap(compactCoords);
  grid.nearestGasRows.swap(compactNearest);
  grid.count = grid.nearestGasRows.size();
  if (grid.count == 0) grid.hasNearestGasRows = false;
}

DatasetShape GetDatasetShape(H5::DataSet& ds)
{
  DatasetShape shape;
  H5::DataSpace space = ds.getSpace();
  shape.rank = space.getSimpleExtentNdims();
  if (shape.rank != 1 && shape.rank != 2) {
    throw std::runtime_error("unsupported dataset rank");
  }
  hsize_t dims[2] = {0, 0};
  space.getSimpleExtentDims(dims);
  shape.sourceRows = dims[0];
  shape.components = (shape.rank == 2) ? dims[1] : 1;
  shape.rowBytes = ds.getDataType().getSize() * static_cast<std::size_t>(shape.components);
  return shape;
}

H5::DataSet CreateOutputDataset(H5::Group& outGroup,
                                const std::string& name,
                                const H5::DataType& dataType,
                                const DatasetShape& shape,
                                std::size_t rowCount)
{
  if (LinkExists(outGroup.getId(), name)) {
    outGroup.unlink(name);
  }

  if (shape.rank == 1) {
    hsize_t dims[1] = {static_cast<hsize_t>(rowCount)};
    H5::DataSpace space(1, dims);
    return outGroup.createDataSet(name, dataType, space);
  }

  hsize_t dims[2] = {
    static_cast<hsize_t>(rowCount),
    shape.components
  };
  H5::DataSpace space(2, dims);
  return outGroup.createDataSet(name, dataType, space);
}

H5::PredType NativeTypeForDataType(DataType type)
{
  switch (type) {
  case DataType::Float:  return H5::PredType::NATIVE_FLOAT;
  case DataType::Double: return H5::PredType::NATIVE_DOUBLE;
  case DataType::Int32:  return H5::PredType::NATIVE_INT32;
  case DataType::Int64:  return H5::PredType::NATIVE_INT64;
  }
  return H5::PredType::NATIVE_DOUBLE;
}

H5::PredType NativeTypeForOutputField(const FieldSpec& field)
{
  return field.key == FieldKey::ID
    ? H5::PredType::NATIVE_UINT64
    : NativeTypeForDataType(field.type);
}

DatasetShape ShapeForFieldSpec(const FieldSpec& field,
                               std::size_t rowCount)
{
  DatasetShape shape;
  shape.rank = field.count == 1 ? 1 : 2;
  shape.components = static_cast<hsize_t>(std::max(1, field.count));
  shape.sourceRows = static_cast<hsize_t>(rowCount);
  shape.rowBytes =
    dataTypeSize(field.type) * static_cast<std::size_t>(shape.components);
  return shape;
}

void ReadRawRows(H5::DataSet& ds,
                 const H5::DataType& memType,
                 const DatasetShape& shape,
                 std::size_t begin,
                 std::size_t count,
                 std::vector<std::uint8_t>& out)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    out.resize(count * shape.rowBytes);
    ds.read(out.data(), memType, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  out.resize(count * shape.rowBytes);
  ds.read(out.data(), memType, memSpace, fileSpace);
}

void WriteRawRows(H5::DataSet& ds,
                  const H5::DataType& memType,
                  const DatasetShape& shape,
                  std::size_t begin,
                  std::size_t count,
                  const std::vector<std::uint8_t>& rows)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    ds.write(rows.data(), memType, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  ds.write(rows.data(), memType, memSpace, fileSpace);
}

void ReadDoubleRows(H5::DataSet& ds,
                    const DatasetShape& shape,
                    std::size_t begin,
                    std::size_t count,
                    std::vector<double>& out)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    out.resize(count);
    ds.read(out.data(), H5::PredType::NATIVE_DOUBLE, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  out.resize(count * static_cast<std::size_t>(shape.components));
  ds.read(out.data(), H5::PredType::NATIVE_DOUBLE, memSpace, fileSpace);
}

void WriteDoubleRows(H5::DataSet& ds,
                     const DatasetShape& shape,
                     std::size_t begin,
                     std::size_t count,
                     const std::vector<double>& rows)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    ds.write(rows.data(), H5::PredType::NATIVE_DOUBLE, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  ds.write(rows.data(), H5::PredType::NATIVE_DOUBLE, memSpace, fileSpace);
}

void WriteUInt64Rows(H5::DataSet& ds,
                     const DatasetShape& shape,
                     std::size_t begin,
                     std::size_t count,
                     const std::vector<std::uint64_t>& rows)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    ds.write(rows.data(), H5::PredType::NATIVE_UINT64, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  ds.write(rows.data(), H5::PredType::NATIVE_UINT64, memSpace, fileSpace);
}

void ReadUInt64Rows(H5::DataSet& ds,
                    const DatasetShape& shape,
                    std::size_t begin,
                    std::size_t count,
                    std::vector<std::uint64_t>& out)
{
  H5::DataSpace fileSpace = ds.getSpace();
  if (shape.rank == 1) {
    hsize_t offset[1] = {static_cast<hsize_t>(begin)};
    hsize_t slab[1] = {static_cast<hsize_t>(count)};
    fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
    H5::DataSpace memSpace(1, slab);
    out.resize(count);
    ds.read(out.data(), H5::PredType::NATIVE_UINT64, memSpace, fileSpace);
    return;
  }

  hsize_t offset[2] = {static_cast<hsize_t>(begin), 0};
  hsize_t slab[2] = {static_cast<hsize_t>(count), shape.components};
  fileSpace.selectHyperslab(H5S_SELECT_SET, slab, offset);
  H5::DataSpace memSpace(2, slab);
  out.resize(count * static_cast<std::size_t>(shape.components));
  ds.read(out.data(), H5::PredType::NATIVE_UINT64, memSpace, fileSpace);
}

void CopyDatasetWithKeepList(H5::H5File& input,
                             H5::Group& outGroup,
                             int ptype,
                             const std::string& sourceName,
                             const std::string& outputName,
                             const std::vector<std::uint64_t>& keep,
                             std::size_t outputRowCount,
                             double valueScale)
{
  H5::DataSet src = input.openDataSet(DatasetPath(ptype, sourceName));
  DatasetShape shape = GetDatasetShape(src);
  if (!keep.empty() && shape.sourceRows <= keep.back()) {
    throw std::runtime_error("dataset '" + sourceName + "' is shorter than the keep list");
  }

  H5::DataType dataType = src.getDataType();
  H5::DataSet dst = CreateOutputDataset(outGroup, outputName, dataType, shape, outputRowCount);
  if (keep.empty()) return;
  const bool scaleValues =
    std::isfinite(valueScale) &&
    std::abs(valueScale - 1.0) > 1.0e-14 &&
    dataType.getClass() == H5T_FLOAT;

  std::vector<std::uint8_t> srcChunk;
  std::vector<std::uint8_t> packed;
  std::vector<double> doubleChunk;
  std::vector<double> doublePacked;
  std::size_t keepCursor = 0;
  std::size_t outCursor = 0;
  while (keepCursor < keep.size()) {
    const std::size_t chunkBegin =
      static_cast<std::size_t>((keep[keepCursor] / kExtractChunkSize) * kExtractChunkSize);
    const std::size_t chunkEnd =
      std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                            static_cast<std::size_t>(shape.sourceRows));
    std::size_t keepEnd = keepCursor;
    while (keepEnd < keep.size() && keep[keepEnd] < chunkEnd) {
      ++keepEnd;
    }

    if (scaleValues) {
      ReadDoubleRows(src, shape, chunkBegin, chunkEnd - chunkBegin, doubleChunk);
      const std::size_t components = static_cast<std::size_t>(shape.components);
      doublePacked.resize((keepEnd - keepCursor) * components);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        const std::size_t outLocal = k - keepCursor;
        for (std::size_t c = 0; c < components; ++c) {
          doublePacked[outLocal * components + c] =
            doubleChunk[local * components + c] * valueScale;
        }
      }
      WriteDoubleRows(dst, shape, outCursor, keepEnd - keepCursor, doublePacked);
    } else {
      ReadRawRows(src, dataType, shape, chunkBegin, chunkEnd - chunkBegin, srcChunk);
      packed.resize((keepEnd - keepCursor) * shape.rowBytes);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        std::memcpy(packed.data() + (k - keepCursor) * shape.rowBytes,
                    srcChunk.data() + local * shape.rowBytes,
                    shape.rowBytes);
      }

      WriteRawRows(dst, dataType, shape, outCursor, keepEnd - keepCursor, packed);
    }
    outCursor += keepEnd - keepCursor;
    keepCursor = keepEnd;
  }
}

void CopyDatasetWithKeepListAsOutputField(H5::H5File& input,
                                          H5::Group& outGroup,
                                          int ptype,
                                          const std::string& sourceName,
                                          const ActiveOutputField& outputField,
                                          const std::vector<std::uint64_t>& keep,
                                          std::size_t outputRowCount,
                                          double valueScale)
{
  H5::DataSet src = input.openDataSet(DatasetPath(ptype, sourceName));
  DatasetShape sourceShape = GetDatasetShape(src);
  if (!keep.empty() && sourceShape.sourceRows <= keep.back()) {
    throw std::runtime_error("dataset '" + sourceName + "' is shorter than the keep list");
  }

  DatasetShape outputShape =
    ShapeForFieldSpec(outputField.field, outputRowCount);
  H5::DataSet dst = CreateOutputDataset(outGroup,
                                        outputField.outputName,
                                        NativeTypeForOutputField(outputField.field),
                                        outputShape,
                                        outputRowCount);
  if (keep.empty()) return;

  const std::size_t sourceComps =
    static_cast<std::size_t>(std::max<hsize_t>(hsize_t{1}, sourceShape.components));
  const std::size_t outputComps =
    static_cast<std::size_t>(std::max<hsize_t>(hsize_t{1}, outputShape.components));
  const std::vector<double> defaults = ExpandedDefaultValues(outputField);

  std::vector<double> doubleChunk;
  std::vector<double> doublePacked;
  std::vector<std::uint64_t> idChunk;
  std::vector<std::uint64_t> idPacked;
  std::size_t keepCursor = 0;
  std::size_t outCursor = 0;
  while (keepCursor < keep.size()) {
    const std::size_t chunkBegin =
      static_cast<std::size_t>((keep[keepCursor] / kExtractChunkSize) *
                               kExtractChunkSize);
    const std::size_t chunkEnd =
      std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                            static_cast<std::size_t>(sourceShape.sourceRows));
    std::size_t keepEnd = keepCursor;
    while (keepEnd < keep.size() && keep[keepEnd] < chunkEnd) {
      ++keepEnd;
    }

    if (outputField.field.key == FieldKey::ID) {
      ReadUInt64Rows(src, sourceShape, chunkBegin, chunkEnd - chunkBegin, idChunk);
      idPacked.resize((keepEnd - keepCursor) * outputComps);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        const std::size_t outLocal = k - keepCursor;
        for (std::size_t c = 0; c < outputComps; ++c) {
          idPacked[outLocal * outputComps + c] =
            c < sourceComps
              ? idChunk[local * sourceComps + c]
              : static_cast<std::uint64_t>(defaults[std::min(c, defaults.size() - 1)]);
        }
      }
      WriteUInt64Rows(dst, outputShape, outCursor, keepEnd - keepCursor, idPacked);
    } else {
      ReadDoubleRows(src, sourceShape, chunkBegin, chunkEnd - chunkBegin, doubleChunk);
      doublePacked.resize((keepEnd - keepCursor) * outputComps);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        const std::size_t outLocal = k - keepCursor;
        for (std::size_t c = 0; c < outputComps; ++c) {
          const double value = c < sourceComps
            ? doubleChunk[local * sourceComps + c] * valueScale
            : defaults[std::min(c, defaults.size() - 1)];
          doublePacked[outLocal * outputComps + c] = value;
        }
      }
      WriteDoubleRows(dst, outputShape, outCursor, keepEnd - keepCursor, doublePacked);
    }

    outCursor += keepEnd - keepCursor;
    keepCursor = keepEnd;
  }
}

bool FieldUsesBackgroundGrid(FieldKey key)
{
  switch (key) {
  case FieldKey::Position:
  case FieldKey::Velocity:
  case FieldKey::ID:
  case FieldKey::Mass:
  case FieldKey::Density:
  case FieldKey::InternalEnergy:
  case FieldKey::Temperature:
  case FieldKey::Hsml:
  case FieldKey::Volume:
  case FieldKey::Bfield:
  case FieldKey::Metallicity:
  case FieldKey::ElectronAbundance:
  case FieldKey::H2Abundance:
  case FieldKey::HDAbundance:
  case FieldKey::J21:
  case FieldKey::Gamma:
    return true;
  default:
    return false;
  }
}

bool FieldCanBeSynthesizedForBackgroundGrid(FieldKey key)
{
  switch (key) {
  case FieldKey::Position:
  case FieldKey::ID:
  case FieldKey::Mass:
  case FieldKey::Density:
    return true;
  default:
    return false;
  }
}

bool FieldUsesNearestBackgroundValue(FieldKey key)
{
  switch (key) {
  case FieldKey::Velocity:
  case FieldKey::InternalEnergy:
  case FieldKey::Temperature:
  case FieldKey::Bfield:
  case FieldKey::Metallicity:
  case FieldKey::ElectronAbundance:
  case FieldKey::H2Abundance:
  case FieldKey::HDAbundance:
  case FieldKey::J21:
  case FieldKey::Gamma:
    return true;
  default:
    return false;
  }
}

BackgroundGridData BuildBackgroundGrid(const SnapshotExtractRegion& region,
                                       const SnapshotBackgroundGridConfig& config,
                                       double lengthScale,
                                       std::uint64_t firstId)
{
  BackgroundGridData grid;
  if (!config.enabled) return grid;

  const int n = std::clamp(config.cellsPerAxis, 1, 512);
  const int nx = n;
  const int ny = n;
  const int nz = n;

  std::array<double, 3> lo{};
  std::array<double, 3> hi{};
  if (region.kind == SnapshotExtractRegionKind::Sphere) {
    for (int axis = 0; axis < 3; ++axis) {
      lo[axis] = (region.center[axis] - region.radius) * lengthScale;
      hi[axis] = (region.center[axis] + region.radius) * lengthScale;
    }
  } else {
    for (int axis = 0; axis < 3; ++axis) {
      lo[axis] = (region.center[axis] - region.halfSize[axis]) * lengthScale;
      hi[axis] = (region.center[axis] + region.halfSize[axis]) * lengthScale;
    }
  }

  const double dx = (hi[0] - lo[0]) / static_cast<double>(nx);
  const double dy = (hi[1] - lo[1]) / static_cast<double>(ny);
  const double dz = (hi[2] - lo[2]) / static_cast<double>(nz);
  if (!(dx > 0.0 && dy > 0.0 && dz > 0.0)) return grid;

  grid.count = static_cast<std::size_t>(nx) *
               static_cast<std::size_t>(ny) *
               static_cast<std::size_t>(nz);
  grid.coordinates.resize(grid.count * 3);
  grid.nearestGasRows.assign(grid.count, 0);
  grid.cellVolume = dx * dy * dz;
  grid.cellLength = std::cbrt(grid.cellVolume);
  grid.firstId = firstId;
  grid.cells = {nx, ny, nz};
  grid.lo = lo;
  grid.cellSize = {dx, dy, dz};

  std::size_t cursor = 0;
  for (int iz = 0; iz < nz; ++iz) {
    const double z = lo[2] + (static_cast<double>(iz) + 0.5) * dz;
    for (int iy = 0; iy < ny; ++iy) {
      const double y = lo[1] + (static_cast<double>(iy) + 0.5) * dy;
      for (int ix = 0; ix < nx; ++ix) {
        const double x = lo[0] + (static_cast<double>(ix) + 0.5) * dx;
        grid.coordinates[3 * cursor + 0] = x;
        grid.coordinates[3 * cursor + 1] = y;
        grid.coordinates[3 * cursor + 2] = z;
        ++cursor;
      }
    }
  }
  return grid;
}

std::size_t BackgroundGridFlatIndex(const BackgroundGridData& grid,
                                    int ix,
                                    int iy,
                                    int iz)
{
  return (static_cast<std::size_t>(iz) * static_cast<std::size_t>(grid.cells[1]) +
          static_cast<std::size_t>(iy)) * static_cast<std::size_t>(grid.cells[0]) +
         static_cast<std::size_t>(ix);
}

int BackgroundGridCullSearchRadius(const BackgroundGridData& grid)
{
  const double minDist = 0.5 * grid.cellLength;
  int radius = 1;
  for (double cellSize : grid.cellSize) {
    if (cellSize > 0.0) {
      radius = std::max(radius,
                        static_cast<int>(std::ceil(minDist / cellSize)));
    }
  }
  return std::min(radius, std::max({grid.cells[0], grid.cells[1], grid.cells[2]}));
}

void AssignNearestGasRows(H5::H5File& input,
                          const std::vector<std::uint64_t>& gasKeep,
                          double lengthScale,
                          BackgroundGridData& grid)
{
  if (grid.count == 0 || gasKeep.empty()) return;
  const std::string coordPath = DatasetPath(0, "Coordinates");
  if (!DatasetExists(input, coordPath)) return;

  H5::DataSet coords = input.openDataSet(coordPath);
  DatasetShape shape = GetDatasetShape(coords);
  if (shape.components < 3 || shape.sourceRows <= gasKeep.back()) return;

  std::vector<std::vector<std::size_t>> bins(grid.count);
  std::vector<double> sourceCoords;
  std::vector<std::uint64_t> sourceRows;
  sourceCoords.reserve(gasKeep.size() * 3);
  sourceRows.reserve(gasKeep.size());

  std::vector<double> coordChunk;
  std::size_t keepCursor = 0;
  while (keepCursor < gasKeep.size()) {
    const std::size_t chunkBegin =
      static_cast<std::size_t>((gasKeep[keepCursor] / kExtractChunkSize) *
                               kExtractChunkSize);
    const std::size_t chunkEnd =
      std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                            static_cast<std::size_t>(shape.sourceRows));
    std::size_t keepEnd = keepCursor;
    while (keepEnd < gasKeep.size() && gasKeep[keepEnd] < chunkEnd) ++keepEnd;

    ReadDoubleRows(coords, shape, chunkBegin, chunkEnd - chunkBegin, coordChunk);
    for (std::size_t k = keepCursor; k < keepEnd; ++k) {
      const std::size_t local = static_cast<std::size_t>(gasKeep[k]) - chunkBegin;
      const double x = coordChunk[local * static_cast<std::size_t>(shape.components) + 0] *
                       lengthScale;
      const double y = coordChunk[local * static_cast<std::size_t>(shape.components) + 1] *
                       lengthScale;
      const double z = coordChunk[local * static_cast<std::size_t>(shape.components) + 2] *
                       lengthScale;
      const std::size_t sourceIndex = sourceRows.size();
      sourceRows.push_back(gasKeep[k]);
      sourceCoords.push_back(x);
      sourceCoords.push_back(y);
      sourceCoords.push_back(z);

      const int ix = std::clamp(
        static_cast<int>((x - grid.lo[0]) / grid.cellSize[0]),
        0,
        grid.cells[0] - 1);
      const int iy = std::clamp(
        static_cast<int>((y - grid.lo[1]) / grid.cellSize[1]),
        0,
        grid.cells[1] - 1);
      const int iz = std::clamp(
        static_cast<int>((z - grid.lo[2]) / grid.cellSize[2]),
        0,
        grid.cells[2] - 1);
      bins[BackgroundGridFlatIndex(grid, ix, iy, iz)].push_back(sourceIndex);
    }
    keepCursor = keepEnd;
  }

  if (sourceRows.empty()) return;

  std::vector<double> nearestDist2(grid.count,
                                   std::numeric_limits<double>::infinity());
  for (std::size_t i = 0; i < grid.count; ++i) {
    const int nx = grid.cells[0];
    const int ny = grid.cells[1];
    const int ix0 = static_cast<int>(i % static_cast<std::size_t>(nx));
    const int iy0 = static_cast<int>((i / static_cast<std::size_t>(nx)) %
                                     static_cast<std::size_t>(ny));
    const int iz0 = static_cast<int>(i / (static_cast<std::size_t>(nx) *
                                          static_cast<std::size_t>(ny)));
    const double gx = grid.coordinates[3 * i + 0];
    const double gy = grid.coordinates[3 * i + 1];
    const double gz = grid.coordinates[3 * i + 2];

    double bestDist2 = std::numeric_limits<double>::infinity();
    std::size_t bestSourceIndex = 0;
    bool found = false;
    const int maxRadius = std::max({grid.cells[0], grid.cells[1], grid.cells[2]});
    for (int r = 0; r <= maxRadius && !found; ++r) {
      const int zBegin = std::max(0, iz0 - r);
      const int zEnd = std::min(grid.cells[2] - 1, iz0 + r);
      const int yBegin = std::max(0, iy0 - r);
      const int yEnd = std::min(grid.cells[1] - 1, iy0 + r);
      const int xBegin = std::max(0, ix0 - r);
      const int xEnd = std::min(grid.cells[0] - 1, ix0 + r);
      for (int iz = zBegin; iz <= zEnd; ++iz) {
        for (int iy = yBegin; iy <= yEnd; ++iy) {
          for (int ix = xBegin; ix <= xEnd; ++ix) {
            if (std::max({std::abs(ix - ix0),
                          std::abs(iy - iy0),
                          std::abs(iz - iz0)}) != r) {
              continue;
            }
            const auto& bin = bins[BackgroundGridFlatIndex(grid, ix, iy, iz)];
            for (std::size_t sourceIndex : bin) {
              const double dx = sourceCoords[3 * sourceIndex + 0] - gx;
              const double dy = sourceCoords[3 * sourceIndex + 1] - gy;
              const double dz = sourceCoords[3 * sourceIndex + 2] - gz;
              const double dist2 = dx * dx + dy * dy + dz * dz;
              if (dist2 < bestDist2) {
                bestDist2 = dist2;
                bestSourceIndex = sourceIndex;
                found = true;
              }
            }
          }
        }
      }
    }
    grid.nearestGasRows[i] = sourceRows[bestSourceIndex];
    nearestDist2[i] = bestDist2;
  }
  grid.hasNearestGasRows = true;
}

void CullBackgroundGridNearSelectedParticles(
  H5::H5File& input,
  const std::array<std::vector<std::uint64_t>, 6>& keepByType,
  double lengthScale,
  BackgroundGridData& grid)
{
  if (grid.count == 0) return;

  std::vector<std::vector<std::size_t>> bins(grid.count);
  std::vector<double> sourceCoords;

  for (int ptype = 0; ptype < 6; ++ptype) {
    const auto& keep = keepByType[ptype];
    if (keep.empty()) continue;

    const std::string coordPath = DatasetPath(ptype, "Coordinates");
    if (!DatasetExists(input, coordPath)) continue;
    H5::DataSet coords = input.openDataSet(coordPath);
    DatasetShape shape = GetDatasetShape(coords);
    if (shape.components < 3 || shape.sourceRows <= keep.back()) continue;

    std::vector<double> coordChunk;
    std::size_t keepCursor = 0;
    while (keepCursor < keep.size()) {
      const std::size_t chunkBegin =
        static_cast<std::size_t>((keep[keepCursor] / kExtractChunkSize) *
                                 kExtractChunkSize);
      const std::size_t chunkEnd =
        std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                              static_cast<std::size_t>(shape.sourceRows));
      std::size_t keepEnd = keepCursor;
      while (keepEnd < keep.size() && keep[keepEnd] < chunkEnd) ++keepEnd;

      ReadDoubleRows(coords, shape, chunkBegin, chunkEnd - chunkBegin, coordChunk);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        const double x =
          coordChunk[local * static_cast<std::size_t>(shape.components) + 0] *
          lengthScale;
        const double y =
          coordChunk[local * static_cast<std::size_t>(shape.components) + 1] *
          lengthScale;
        const double z =
          coordChunk[local * static_cast<std::size_t>(shape.components) + 2] *
          lengthScale;
        const std::size_t sourceIndex = sourceCoords.size() / 3;
        sourceCoords.push_back(x);
        sourceCoords.push_back(y);
        sourceCoords.push_back(z);

        const int ix = std::clamp(
          static_cast<int>((x - grid.lo[0]) / grid.cellSize[0]),
          0,
          grid.cells[0] - 1);
        const int iy = std::clamp(
          static_cast<int>((y - grid.lo[1]) / grid.cellSize[1]),
          0,
          grid.cells[1] - 1);
        const int iz = std::clamp(
          static_cast<int>((z - grid.lo[2]) / grid.cellSize[2]),
          0,
          grid.cells[2] - 1);
        bins[BackgroundGridFlatIndex(grid, ix, iy, iz)].push_back(sourceIndex);
      }
      keepCursor = keepEnd;
    }
  }

  if (sourceCoords.empty()) return;

  std::vector<double> nearestDist2(grid.count,
                                   std::numeric_limits<double>::infinity());
  const int nx = grid.cells[0];
  const int ny = grid.cells[1];
  const int maxSearchRadius = BackgroundGridCullSearchRadius(grid);
  for (std::size_t i = 0; i < grid.count; ++i) {
    const int ix0 = static_cast<int>(i % static_cast<std::size_t>(nx));
    const int iy0 = static_cast<int>((i / static_cast<std::size_t>(nx)) %
                                     static_cast<std::size_t>(ny));
    const int iz0 = static_cast<int>(i / (static_cast<std::size_t>(nx) *
                                          static_cast<std::size_t>(ny)));
    const double gx = grid.coordinates[3 * i + 0];
    const double gy = grid.coordinates[3 * i + 1];
    const double gz = grid.coordinates[3 * i + 2];

    double bestDist2 = std::numeric_limits<double>::infinity();
    for (int r = 0; r <= maxSearchRadius; ++r) {
      const int zBegin = std::max(0, iz0 - r);
      const int zEnd = std::min(grid.cells[2] - 1, iz0 + r);
      const int yBegin = std::max(0, iy0 - r);
      const int yEnd = std::min(grid.cells[1] - 1, iy0 + r);
      const int xBegin = std::max(0, ix0 - r);
      const int xEnd = std::min(grid.cells[0] - 1, ix0 + r);
      for (int iz = zBegin; iz <= zEnd; ++iz) {
        for (int iy = yBegin; iy <= yEnd; ++iy) {
          for (int ix = xBegin; ix <= xEnd; ++ix) {
            if (std::max({std::abs(ix - ix0),
                          std::abs(iy - iy0),
                          std::abs(iz - iz0)}) != r) {
              continue;
            }
            const auto& bin = bins[BackgroundGridFlatIndex(grid, ix, iy, iz)];
            for (std::size_t sourceIndex : bin) {
              const double dx = sourceCoords[3 * sourceIndex + 0] - gx;
              const double dy = sourceCoords[3 * sourceIndex + 1] - gy;
              const double dz = sourceCoords[3 * sourceIndex + 2] - gz;
              const double dist2 = dx * dx + dy * dy + dz * dz;
              if (dist2 < bestDist2) {
                bestDist2 = dist2;
              }
            }
          }
        }
      }
    }
    nearestDist2[i] = bestDist2;
  }
  CompactBackgroundGridToEmptyCells(grid, nearestDist2);
}

std::uint64_t MaxSelectedParticleId(
  H5::H5File& input,
  const SourceHeader& source,
  const std::array<std::vector<std::uint64_t>, 6>& keepByType)
{
  std::uint64_t maxId = 0;
  bool haveAny = false;
  for (int ptype = 0; ptype < 6; ++ptype) {
    const auto& keep = keepByType[ptype];
    if (keep.empty()) continue;
    const std::string path = DatasetPath(ptype, "ParticleIDs");
    if (!DatasetExists(input, path)) continue;
    H5::DataSet ds = input.openDataSet(path);
    DatasetShape shape = GetDatasetShape(ds);
    if (shape.components != 1 || shape.sourceRows <= keep.back()) continue;

    std::vector<std::uint64_t> ids;
    std::size_t keepCursor = 0;
    while (keepCursor < keep.size()) {
      const std::size_t chunkBegin =
        static_cast<std::size_t>((keep[keepCursor] / kExtractChunkSize) *
                                 kExtractChunkSize);
      const std::size_t chunkEnd =
        std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                              static_cast<std::size_t>(shape.sourceRows));
      std::size_t keepEnd = keepCursor;
      while (keepEnd < keep.size() && keep[keepEnd] < chunkEnd) ++keepEnd;
      ReadUInt64Rows(ds, shape, chunkBegin, chunkEnd - chunkBegin, ids);
      for (std::size_t k = keepCursor; k < keepEnd; ++k) {
        const std::size_t local = static_cast<std::size_t>(keep[k]) - chunkBegin;
        maxId = std::max(maxId, ids[local]);
        haveAny = true;
      }
      keepCursor = keepEnd;
    }
  }
  if (haveAny) return maxId;
  return static_cast<std::uint64_t>(
    std::accumulate(source.counts.begin(), source.counts.end(), std::size_t{0}));
}

std::vector<double> ReadNearestBackgroundRows(H5::H5File& input,
                                              const std::string& sourceName,
                                              const FieldSpec& field,
                                              const DatasetShape& outputShape,
                                              const BackgroundGridData& grid,
                                              double valueScale)
{
  if (!FieldUsesNearestBackgroundValue(field.key) ||
      !grid.hasNearestGasRows ||
      grid.nearestGasRows.size() != grid.count) {
    return {};
  }

  const std::string sourcePath = DatasetPath(0, sourceName);
  if (!DatasetExists(input, sourcePath)) {
    return {};
  }

  H5::DataSet ds = input.openDataSet(sourcePath);
  DatasetShape sourceShape = GetDatasetShape(ds);
  if (sourceShape.sourceRows == 0 ||
      sourceShape.components < outputShape.components) {
    return {};
  }

  const std::size_t comps =
    static_cast<std::size_t>(std::max<hsize_t>(1, outputShape.components));
  std::vector<double> rows(grid.count * comps, 0.0);
  std::vector<double> chunk;
  std::vector<std::uint64_t> order(grid.count);
  std::iota(order.begin(), order.end(), std::uint64_t{0});
  std::sort(order.begin(), order.end(), [&](std::uint64_t a, std::uint64_t b) {
    return grid.nearestGasRows[static_cast<std::size_t>(a)] <
           grid.nearestGasRows[static_cast<std::size_t>(b)];
  });

  std::size_t cursor = 0;
  const std::size_t sourceComps =
    static_cast<std::size_t>(sourceShape.components);
  while (cursor < order.size()) {
    const std::uint64_t row =
      grid.nearestGasRows[static_cast<std::size_t>(order[cursor])];
    const std::size_t chunkBegin =
      static_cast<std::size_t>((row / kExtractChunkSize) * kExtractChunkSize);
    const std::size_t chunkEnd =
      std::min<std::size_t>(chunkBegin + kExtractChunkSize,
                            static_cast<std::size_t>(sourceShape.sourceRows));
    std::size_t end = cursor;
    while (end < order.size() &&
           grid.nearestGasRows[static_cast<std::size_t>(order[end])] < chunkEnd) {
      ++end;
    }
    ReadDoubleRows(ds, sourceShape, chunkBegin, chunkEnd - chunkBegin, chunk);
    for (std::size_t k = cursor; k < end; ++k) {
      const std::size_t outRow = static_cast<std::size_t>(order[k]);
      const std::size_t local =
        static_cast<std::size_t>(grid.nearestGasRows[outRow]) - chunkBegin;
      for (std::size_t c = 0; c < comps; ++c) {
        rows[outRow * comps + c] =
          chunk[local * sourceComps + c] * valueScale;
      }
    }
    cursor = end;
  }
  return rows;
}

std::vector<double> BackgroundDoubleRows(const FieldSpec& field,
                                         const BackgroundGridData& grid,
                                         const SnapshotBackgroundGridConfig& config,
                                         const std::vector<double>& nearestRows)
{
  const std::size_t comps = static_cast<std::size_t>(std::max(1, field.count));
  std::vector<double> rows(grid.count * comps, 0.0);
  for (std::size_t i = 0; i < grid.count; ++i) {
    switch (field.key) {
    case FieldKey::Position:
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[i * comps + c] = grid.coordinates[3 * i + c];
      }
      break;
    case FieldKey::Velocity:
      if (nearestRows.size() == rows.size()) {
        for (std::size_t c = 0; c < comps; ++c) {
          rows[i * comps + c] = nearestRows[i * comps + c];
        }
      }
      break;
    case FieldKey::Mass:
      rows[i * comps] = config.density * grid.cellVolume;
      break;
    case FieldKey::Density:
      rows[i * comps] = config.density;
      break;
    case FieldKey::InternalEnergy:
    case FieldKey::Temperature:
    case FieldKey::Metallicity:
    case FieldKey::ElectronAbundance:
    case FieldKey::H2Abundance:
    case FieldKey::HDAbundance:
    case FieldKey::J21:
    case FieldKey::Gamma:
      if (nearestRows.size() == rows.size()) {
        for (std::size_t c = 0; c < comps; ++c) {
          rows[i * comps + c] = nearestRows[i * comps + c];
        }
      }
      break;
    case FieldKey::Hsml:
      rows[i * comps] = grid.cellLength;
      break;
    case FieldKey::Volume:
      rows[i * comps] = grid.cellVolume;
      break;
    case FieldKey::Bfield:
      if (nearestRows.size() == rows.size()) {
        for (std::size_t c = 0; c < comps; ++c) {
          rows[i * comps + c] = nearestRows[i * comps + c];
        }
      }
      break;
    default:
      break;
    }
  }
  return rows;
}

std::vector<std::uint64_t> BackgroundIdRows(const BackgroundGridData& grid)
{
  std::vector<std::uint64_t> ids(grid.count);
  for (std::size_t i = 0; i < grid.count; ++i) {
    ids[i] = grid.firstId + static_cast<std::uint64_t>(i);
  }
  return ids;
}

void WriteBackgroundRows(H5::H5File& input,
                         H5::DataSet& dst,
                         const FieldSpec& field,
                         const DatasetShape& shape,
                         std::size_t begin,
                         const BackgroundGridData& grid,
                         const SnapshotBackgroundGridConfig& config,
                         const std::string& sourceName,
                         double valueScale)
{
  if (grid.count == 0 || !FieldUsesBackgroundGrid(field.key)) return;
  if (field.key == FieldKey::ID) {
    const std::vector<std::uint64_t> ids = BackgroundIdRows(grid);
    WriteUInt64Rows(dst, shape, begin, grid.count, ids);
    return;
  }
  const std::vector<double> nearestRows =
    ReadNearestBackgroundRows(input, sourceName, field, shape, grid, valueScale);
  const std::vector<double> rows =
    BackgroundDoubleRows(field, grid, config, nearestRows);
  WriteDoubleRows(dst, shape, begin, grid.count, rows);
}

void FillExistingRowsForMissingDataset(H5::DataSet& dst,
                                       const FieldSpec& field,
                                       const DatasetShape& shape,
                                       std::size_t count,
                                       double constantMass)
{
  if (count == 0) return;
  const std::size_t comps = static_cast<std::size_t>(std::max(1, field.count));
  std::vector<double> rows(count * comps, 0.0);
  if (field.key == FieldKey::Mass && constantMass > 0.0) {
    for (std::size_t i = 0; i < count; ++i) {
      rows[i * comps] = constantMass;
    }
  }
  WriteDoubleRows(dst, shape, 0, count, rows);
}

std::vector<double> ExpandedDefaultValues(const ActiveOutputField& field)
{
  const std::size_t comps =
    static_cast<std::size_t>(std::max(1, field.field.count));
  std::vector<double> values(comps, 0.0);
  for (std::size_t c = 0; c < comps && c < field.defaultValues.size(); ++c) {
    values[c] = field.defaultValues[c];
  }
  return values;
}

void FillRowsWithDefaultValue(H5::DataSet& dst,
                              const ActiveOutputField& field,
                              const DatasetShape& shape,
                              std::size_t begin,
                              std::size_t count)
{
  if (count == 0) return;
  const std::size_t comps =
    static_cast<std::size_t>(std::max<hsize_t>(hsize_t{1}, shape.components));
  if (field.field.key == FieldKey::ID) {
    std::vector<std::uint64_t> rows(count * comps, 0);
    const std::vector<double> defaults = ExpandedDefaultValues(field);
    for (std::size_t i = 0; i < count; ++i) {
      for (std::size_t c = 0; c < comps; ++c) {
        rows[i * comps + c] =
          static_cast<std::uint64_t>(defaults[std::min(c, defaults.size() - 1)]);
      }
    }
    WriteUInt64Rows(dst, shape, begin, count, rows);
    return;
  }

  std::vector<double> rows(count * comps, 0.0);
  const std::vector<double> defaults = ExpandedDefaultValues(field);
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t c = 0; c < comps; ++c) {
      rows[i * comps + c] = defaults[std::min(c, defaults.size() - 1)];
    }
  }
  WriteDoubleRows(dst, shape, begin, count, rows);
}

bool IsTypePseudoField(FieldKey key)
{
  return key == FieldKey::Type || key == FieldKey::Dummy || key == FieldKey::Unknown;
}

std::string OutputDatasetName(const FieldSpec& field)
{
  const char* defaultName = GetDefaultHDF5SourceName(field.key);
  if (defaultName && std::strcmp(defaultName, "unknown") != 0 &&
      std::strcmp(defaultName, "dummy") != 0) {
    return defaultName;
  }
  return field.sourceName;
}

std::string DefaultOutputDatasetName(FieldKey key)
{
  const char* name = GetDefaultHDF5SourceName(key);
  if (!name || std::strcmp(name, "unknown") == 0 ||
      std::strcmp(name, "dummy") == 0) {
    return {};
  }
  return name;
}

const FieldSpec* FindInputField(const std::vector<FieldSpec>& fields, FieldKey key)
{
  for (const FieldSpec& field : fields) {
    if (field.key == key) return &field;
  }
  return nullptr;
}

ActiveOutputField MakeActiveOutputField(const FieldSpec& field)
{
  ActiveOutputField active;
  active.field = field;
  active.sourceName = field.sourceName.empty()
    ? DefaultOutputDatasetName(field.key)
    : field.sourceName;
  active.outputName = OutputDatasetName(field);
  active.typeMask = 0x3fu;
  return active;
}

ActiveOutputField MakeActiveOutputField(
  const SnapshotOutputFieldSpec& output,
  const std::vector<FieldSpec>& inputFields)
{
  ActiveOutputField active;
  active.field.key = output.key;
  active.field.type = output.type;
  active.field.count = std::max(1, output.count);
  active.outputName = output.outputName.empty()
    ? DefaultOutputDatasetName(output.key)
    : output.outputName;
  active.field.sourceName = active.outputName;
  active.missingPolicy = output.missingPolicy;
  active.defaultValues = output.defaultValues;
  active.typeMask = output.typeMask & 0x3fu;

  if (const FieldSpec* input = FindInputField(inputFields, output.key)) {
    active.sourceName = input->sourceName.empty()
      ? DefaultOutputDatasetName(output.key)
      : input->sourceName;
  }
  return active;
}

std::vector<ActiveOutputField> BuildActiveOutputFields(
  const std::vector<FieldSpec>& inputFields,
  const SnapshotOutputFormatConfig& outputFormat)
{
  std::vector<ActiveOutputField> active;
  if (outputFormat.enabled) {
    active.reserve(outputFormat.fields.size());
    for (const SnapshotOutputFieldSpec& output : outputFormat.fields) {
      ActiveOutputField field = MakeActiveOutputField(output, inputFields);
      if (field.outputName.empty() || field.outputName == "unknown" ||
          field.outputName == "dummy" || field.typeMask == 0) {
        continue;
      }
      active.push_back(std::move(field));
    }
    return active;
  }

  active.reserve(inputFields.size());
  for (const FieldSpec& field : inputFields) {
    ActiveOutputField output = MakeActiveOutputField(field);
    if (output.outputName.empty() || output.outputName == "unknown" ||
        output.outputName == "dummy") {
      continue;
    }
    active.push_back(std::move(output));
  }
  return active;
}

bool OutputFieldAppliesToType(const ActiveOutputField& field, int ptype)
{
  return ptype >= 0 && ptype < 6 &&
         (field.typeMask & static_cast<unsigned int>(1u << ptype)) != 0;
}

SnapshotOutputFieldSpec MakeDefaultOutputField(FieldKey key,
                                               unsigned int typeMask)
{
  SnapshotOutputFieldSpec field;
  field.key = key;
  FieldSpec defaults;
  defaults.key = key;
  ApplyDefaultFieldSpec(defaults);
  field.type = key == FieldKey::ID ? DataType::Int64 : DataType::Double;
  field.count = std::max(1, defaults.count);
  field.outputName = DefaultOutputDatasetName(key);
  field.missingPolicy = SnapshotOutputMissingPolicy::Omit;
  field.typeMask = typeMask & 0x3fu;
  field.defaultValues.assign(static_cast<std::size_t>(std::max(1, field.count)),
                             0.0);
  return field;
}

void EnsureActiveBackgroundField(std::vector<ActiveOutputField>& fields,
                                 FieldKey key)
{
  const std::string outputName = DefaultOutputDatasetName(key);
  for (const ActiveOutputField& field : fields) {
    if (field.field.key == key || field.outputName == outputName) return;
  }

  SnapshotOutputFieldSpec spec = MakeDefaultOutputField(key, 0x01u);
  if (key == FieldKey::Density) {
    spec.missingPolicy = SnapshotOutputMissingPolicy::FillDefault;
  }
  fields.push_back(MakeActiveOutputField(spec, {}));
}

void EnsureBackgroundGridOutputFields(std::vector<ActiveOutputField>& fields)
{
  EnsureActiveBackgroundField(fields, FieldKey::Position);
  EnsureActiveBackgroundField(fields, FieldKey::ID);
  EnsureActiveBackgroundField(fields, FieldKey::Mass);
  EnsureActiveBackgroundField(fields, FieldKey::Density);
}

void WriteHeader(H5::H5File& output,
                 const SourceHeader& source,
                 const ParametersInfo& outputParameters,
                 const SnapshotExtractRegion& region,
                 double lengthScale,
                 const std::array<std::size_t, 6>& extractedCounts,
                 const std::array<double, 6>& outputMassTable)
{
  H5::Group header = output.createGroup("/Header");

  std::array<unsigned int, 6> num32 = {0, 0, 0, 0, 0, 0};
  std::array<unsigned int, 6> high32 = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 6; ++i) {
    num32[i] = static_cast<unsigned int>(extractedCounts[i] & 0xffffffffu);
    high32[i] = static_cast<unsigned int>((extractedCounts[i] >> 32u) & 0xffffffffu);
  }

  WriteArrayAttribute(header, "NumPart_ThisFile", H5::PredType::NATIVE_UINT, num32);
  WriteArrayAttribute(header, "NumPart_Total", H5::PredType::NATIVE_UINT, num32);
  WriteArrayAttribute(header, "NumPart_Total_HighWord", H5::PredType::NATIVE_UINT, high32);
  WriteArrayAttribute(header, "MassTable", H5::PredType::NATIVE_DOUBLE, outputMassTable);
  WriteScalarAttribute(header, "Time", H5::PredType::NATIVE_DOUBLE, source.time);
  WriteScalarAttribute(header, "Redshift", H5::PredType::NATIVE_DOUBLE, source.redshift);
  const double outputBoxSize = ComputeExtractBoxSize(region, source.boxSize, lengthScale);
  WriteScalarAttribute(header, "BoxSize", H5::PredType::NATIVE_DOUBLE, outputBoxSize);
  WriteScalarAttribute(header, "OriginalBoxSize", H5::PredType::NATIVE_DOUBLE, source.boxSize);
  WriteScalarAttribute(header, "Omega0", H5::PredType::NATIVE_DOUBLE, source.omega0);
  WriteScalarAttribute(header, "OmegaLambda", H5::PredType::NATIVE_DOUBLE, source.omegaLambda);
  WriteScalarAttribute(header,
                       "HubbleParam",
                       H5::PredType::NATIVE_DOUBLE,
                       outputParameters.hubbleParam);

  const int extractRegionShape = region.kind == SnapshotExtractRegionKind::Sphere ? 1 : 0;
  std::array<double, 3> outputCenter = region.center;
  std::array<double, 3> outputHalfSize = region.halfSize;
  for (int axis = 0; axis < 3; ++axis) {
    outputCenter[axis] *= lengthScale;
    outputHalfSize[axis] *= lengthScale;
  }
  const double outputRadius = region.radius * lengthScale;
  WriteScalarAttribute(header,
                       "ExtractRegionShape",
                       H5::PredType::NATIVE_INT,
                       extractRegionShape);
  WriteArrayAttribute(header,
                      "ExtractRegionCenter",
                      H5::PredType::NATIVE_DOUBLE,
                      outputCenter);
  WriteArrayAttribute(header,
                      "ExtractRegionHalfSize",
                      H5::PredType::NATIVE_DOUBLE,
                      outputHalfSize);
  WriteScalarAttribute(header,
                       "ExtractRegionRadius",
                       H5::PredType::NATIVE_DOUBLE,
                       outputRadius);
  WriteArrayAttribute(header,
                      "OriginalExtractRegionCenter",
                      H5::PredType::NATIVE_DOUBLE,
                      region.center);
  WriteArrayAttribute(header,
                      "OriginalExtractRegionHalfSize",
                      H5::PredType::NATIVE_DOUBLE,
                      region.halfSize);
  WriteScalarAttribute(header,
                       "OriginalExtractRegionRadius",
                       H5::PredType::NATIVE_DOUBLE,
                       region.radius);
  WriteScalarAttribute(header,
                       "ExtractLengthScaleApplied",
                       H5::PredType::NATIVE_DOUBLE,
                       lengthScale);

  constexpr int one = 1;
  constexpr int zero = 0;
  WriteScalarAttribute(header, "NumFilesPerSnapshot", H5::PredType::NATIVE_INT, one);
  WriteScalarAttribute(header, "Flag_Sfr", H5::PredType::NATIVE_INT, zero);
  WriteScalarAttribute(header, "Flag_Cooling", H5::PredType::NATIVE_INT, zero);
  WriteScalarAttribute(header, "Flag_StellarAge", H5::PredType::NATIVE_INT, zero);
  WriteScalarAttribute(header, "Flag_Metals", H5::PredType::NATIVE_INT, zero);
}

void WriteParameters(H5::H5File& output,
                     const ParametersInfo& params,
                     const SnapshotExtractUnitConversion& conversion)
{
  H5::Group group = output.createGroup("/Parameters");
  WriteScalarAttribute(group, "UnitLength_in_cm", H5::PredType::NATIVE_DOUBLE, params.unitLength);
  WriteScalarAttribute(group, "UnitMass_in_g", H5::PredType::NATIVE_DOUBLE, params.unitMass);
  WriteScalarAttribute(group,
                       "UnitVelocity_in_cm_per_s",
                       H5::PredType::NATIVE_DOUBLE,
                       params.unitVelocity);
  WriteScalarAttribute(group, "HubbleParam", H5::PredType::NATIVE_DOUBLE, params.hubbleParam);
  WriteScalarAttribute(group, "ComovingIntegrationOn", H5::PredType::NATIVE_INT, params.comoving);
  WriteScalarAttribute(group, "FlagDensityInCgs", H5::PredType::NATIVE_INT, params.flagDensityInCgs);
  WriteScalarAttribute(group, "FlagBfieldInCgs", H5::PredType::NATIVE_INT, params.flagBfieldInCgs);
  const int conversionEnabled = conversion.enabled ? 1 : 0;
  const int comovingMode = static_cast<int>(conversion.comovingMode);
  WriteScalarAttribute(group,
                       "SnapshotExtractUnitConversionEnabled",
                       H5::PredType::NATIVE_INT,
                       conversionEnabled);
  WriteScalarAttribute(group,
                       "SnapshotExtractComovingConversionMode",
                       H5::PredType::NATIVE_INT,
                       comovingMode);
  WriteScalarAttribute(group,
                       "SnapshotExtractScaleFactor",
                       H5::PredType::NATIVE_DOUBLE,
                       SafePositive(conversion.scaleFactor, 1.0));
}

std::vector<FieldSpec> NormalizeExtractFields(std::vector<FieldSpec> fields)
{
  if (fields.empty()) {
    fields = MakeDefaultSnapshotExtractFields();
  }
  for (FieldSpec& field : fields) {
    if (field.sourceName.empty()) {
      field.sourceName = GetDefaultHDF5SourceName(field.key);
    }
  }
  return fields;
}

SourceHeader MakeSourceHeaderFromLoadedBlock(
  const SimulationBlock& block,
  const SnapshotLoadedExtractMetadata& metadata)
{
  SourceHeader header;
  header.time = metadata.time;
  header.redshift = metadata.redshift;
  header.boxSize = metadata.boxSize;
  header.omega0 = metadata.omega0;
  header.omegaLambda = metadata.omegaLambda;
  header.hubbleParam = metadata.hubbleParam;
  header.hasRedshift = true;
  for (const SimulationElement& p : block.particles) {
    if (p.type < 6) {
      ++header.counts[p.type];
    }
  }
  return header;
}

ParametersInfo MakeParametersFromLoadedMetadata(
  const SnapshotLoadedExtractMetadata& metadata)
{
  ParametersInfo params;
  params.unitLength = metadata.unitLengthCm;
  params.unitMass = metadata.unitMassG;
  params.unitVelocity = metadata.unitVelocityCmPerS;
  params.hubbleParam = metadata.hubbleParam;
  params.comoving = metadata.comoving ? 1 : 0;
  // Loaded blocks store density/B in the viewer's internal physical units.
  params.flagDensityInCgs = 1;
  params.flagBfieldInCgs = 1;
  params.hasGroup = true;
  return params;
}

std::array<std::vector<std::size_t>, 6> BuildLoadedKeepLists(
  const SimulationBlock& block,
  const SnapshotExtractRegion& region)
{
  std::array<std::vector<std::size_t>, 6> keepByType;
  for (std::size_t i = 0; i < block.particles.size(); ++i) {
    const SimulationElement& p = block.particles[i];
    if (p.type >= 6) continue;
    const double pos[3] = {p.position[0], p.position[1], p.position[2]};
    if (InsideRegion(region, pos)) {
      keepByType[p.type].push_back(i);
    }
  }
  return keepByType;
}

std::uint64_t MaxLoadedSelectedParticleId(
  const SimulationBlock& block,
  const std::array<std::vector<std::size_t>, 6>& keepByType)
{
  std::uint64_t maxId = 0;
  bool haveAny = false;
  for (const auto& keep : keepByType) {
    for (std::size_t index : keep) {
      if (index >= block.particles.size()) continue;
      maxId = std::max(maxId, block.particleId(index));
      haveAny = true;
    }
  }
  return haveAny ? maxId : static_cast<std::uint64_t>(block.particles.size());
}

void AssignNearestGasRowsFromLoadedBlock(
  const SimulationBlock& block,
  const std::vector<std::size_t>& gasKeep,
  double lengthScale,
  BackgroundGridData& grid)
{
  if (grid.count == 0 || gasKeep.empty()) return;

  std::vector<std::vector<std::size_t>> bins(grid.count);
  for (std::size_t sourceIndex = 0; sourceIndex < gasKeep.size(); ++sourceIndex) {
    const SimulationElement& p = block.particles[gasKeep[sourceIndex]];
    const double x = static_cast<double>(p.position[0]) * lengthScale;
    const double y = static_cast<double>(p.position[1]) * lengthScale;
    const double z = static_cast<double>(p.position[2]) * lengthScale;
    const int ix = std::clamp(static_cast<int>((x - grid.lo[0]) / grid.cellSize[0]),
                              0,
                              grid.cells[0] - 1);
    const int iy = std::clamp(static_cast<int>((y - grid.lo[1]) / grid.cellSize[1]),
                              0,
                              grid.cells[1] - 1);
    const int iz = std::clamp(static_cast<int>((z - grid.lo[2]) / grid.cellSize[2]),
                              0,
                              grid.cells[2] - 1);
    bins[BackgroundGridFlatIndex(grid, ix, iy, iz)].push_back(sourceIndex);
  }

  std::vector<double> nearestDist2(grid.count,
                                   std::numeric_limits<double>::infinity());
  for (std::size_t i = 0; i < grid.count; ++i) {
    const int nx = grid.cells[0];
    const int ny = grid.cells[1];
    const int ix0 = static_cast<int>(i % static_cast<std::size_t>(nx));
    const int iy0 = static_cast<int>((i / static_cast<std::size_t>(nx)) %
                                     static_cast<std::size_t>(ny));
    const int iz0 = static_cast<int>(i / (static_cast<std::size_t>(nx) *
                                          static_cast<std::size_t>(ny)));
    const double gx = grid.coordinates[3 * i + 0];
    const double gy = grid.coordinates[3 * i + 1];
    const double gz = grid.coordinates[3 * i + 2];

    double bestDist2 = std::numeric_limits<double>::infinity();
    std::size_t bestSourceIndex = 0;
    bool found = false;
    const int maxRadius = std::max({grid.cells[0], grid.cells[1], grid.cells[2]});
    for (int r = 0; r <= maxRadius && !found; ++r) {
      const int zBegin = std::max(0, iz0 - r);
      const int zEnd = std::min(grid.cells[2] - 1, iz0 + r);
      const int yBegin = std::max(0, iy0 - r);
      const int yEnd = std::min(grid.cells[1] - 1, iy0 + r);
      const int xBegin = std::max(0, ix0 - r);
      const int xEnd = std::min(grid.cells[0] - 1, ix0 + r);
      for (int iz = zBegin; iz <= zEnd; ++iz) {
        for (int iy = yBegin; iy <= yEnd; ++iy) {
          for (int ix = xBegin; ix <= xEnd; ++ix) {
            if (std::max({std::abs(ix - ix0),
                          std::abs(iy - iy0),
                          std::abs(iz - iz0)}) != r) {
              continue;
            }
            const auto& bin = bins[BackgroundGridFlatIndex(grid, ix, iy, iz)];
            for (std::size_t sourceIndex : bin) {
              const SimulationElement& p = block.particles[gasKeep[sourceIndex]];
              const double dx = static_cast<double>(p.position[0]) * lengthScale - gx;
              const double dy = static_cast<double>(p.position[1]) * lengthScale - gy;
              const double dz = static_cast<double>(p.position[2]) * lengthScale - gz;
              const double dist2 = dx * dx + dy * dy + dz * dz;
              if (dist2 < bestDist2) {
                bestDist2 = dist2;
                bestSourceIndex = sourceIndex;
                found = true;
              }
            }
          }
        }
      }
    }
    grid.nearestGasRows[i] = static_cast<std::uint64_t>(gasKeep[bestSourceIndex]);
    nearestDist2[i] = bestDist2;
  }
  grid.hasNearestGasRows = true;
}

void CullBackgroundGridNearLoadedParticles(
  const SimulationBlock& block,
  const std::array<std::vector<std::size_t>, 6>& keepByType,
  double lengthScale,
  BackgroundGridData& grid)
{
  if (grid.count == 0) return;

  std::vector<std::vector<std::size_t>> bins(grid.count);
  std::vector<std::size_t> sourceIndices;
  for (const auto& keep : keepByType) {
    for (std::size_t particleIndex : keep) {
      if (particleIndex >= block.particles.size()) continue;
      const SimulationElement& p = block.particles[particleIndex];
      const double x = static_cast<double>(p.position[0]) * lengthScale;
      const double y = static_cast<double>(p.position[1]) * lengthScale;
      const double z = static_cast<double>(p.position[2]) * lengthScale;
      const std::size_t sourceIndex = sourceIndices.size();
      sourceIndices.push_back(particleIndex);
      const int ix = std::clamp(static_cast<int>((x - grid.lo[0]) / grid.cellSize[0]),
                                0,
                                grid.cells[0] - 1);
      const int iy = std::clamp(static_cast<int>((y - grid.lo[1]) / grid.cellSize[1]),
                                0,
                                grid.cells[1] - 1);
      const int iz = std::clamp(static_cast<int>((z - grid.lo[2]) / grid.cellSize[2]),
                                0,
                                grid.cells[2] - 1);
      bins[BackgroundGridFlatIndex(grid, ix, iy, iz)].push_back(sourceIndex);
    }
  }

  if (sourceIndices.empty()) return;

  std::vector<double> nearestDist2(grid.count,
                                   std::numeric_limits<double>::infinity());
  const int nx = grid.cells[0];
  const int ny = grid.cells[1];
  const int maxSearchRadius = BackgroundGridCullSearchRadius(grid);
  for (std::size_t i = 0; i < grid.count; ++i) {
    const int ix0 = static_cast<int>(i % static_cast<std::size_t>(nx));
    const int iy0 = static_cast<int>((i / static_cast<std::size_t>(nx)) %
                                     static_cast<std::size_t>(ny));
    const int iz0 = static_cast<int>(i / (static_cast<std::size_t>(nx) *
                                          static_cast<std::size_t>(ny)));
    const double gx = grid.coordinates[3 * i + 0];
    const double gy = grid.coordinates[3 * i + 1];
    const double gz = grid.coordinates[3 * i + 2];

    double bestDist2 = std::numeric_limits<double>::infinity();
    for (int r = 0; r <= maxSearchRadius; ++r) {
      const int zBegin = std::max(0, iz0 - r);
      const int zEnd = std::min(grid.cells[2] - 1, iz0 + r);
      const int yBegin = std::max(0, iy0 - r);
      const int yEnd = std::min(grid.cells[1] - 1, iy0 + r);
      const int xBegin = std::max(0, ix0 - r);
      const int xEnd = std::min(grid.cells[0] - 1, ix0 + r);
      for (int iz = zBegin; iz <= zEnd; ++iz) {
        for (int iy = yBegin; iy <= yEnd; ++iy) {
          for (int ix = xBegin; ix <= xEnd; ++ix) {
            if (std::max({std::abs(ix - ix0),
                          std::abs(iy - iy0),
                          std::abs(iz - iz0)}) != r) {
              continue;
            }
            const auto& bin = bins[BackgroundGridFlatIndex(grid, ix, iy, iz)];
            for (std::size_t sourceIndex : bin) {
              const SimulationElement& p = block.particles[sourceIndices[sourceIndex]];
              const double dx = static_cast<double>(p.position[0]) * lengthScale - gx;
              const double dy = static_cast<double>(p.position[1]) * lengthScale - gy;
              const double dz = static_cast<double>(p.position[2]) * lengthScale - gz;
              const double dist2 = dx * dx + dy * dy + dz * dz;
              if (dist2 < bestDist2) {
                bestDist2 = dist2;
              }
            }
          }
        }
      }
    }
    nearestDist2[i] = bestDist2;
  }
  CompactBackgroundGridToEmptyCells(grid, nearestDist2);
}

std::vector<double> LoadedBackgroundNearestRows(
  const SimulationBlock& block,
  const FieldSpec& field,
  const DatasetShape& shape,
  const BackgroundGridData& grid,
  double valueScale)
{
  if (!FieldUsesNearestBackgroundValue(field.key) ||
      !grid.hasNearestGasRows ||
      grid.nearestGasRows.size() != grid.count) {
    return {};
  }
  const std::size_t comps =
    static_cast<std::size_t>(std::max<hsize_t>(hsize_t{1}, shape.components));
  std::vector<double> rows(grid.count * comps, 0.0);
  for (std::size_t i = 0; i < grid.count; ++i) {
    const std::size_t source = static_cast<std::size_t>(grid.nearestGasRows[i]);
    if (source >= block.particles.size()) continue;
    const SimulationElement& p = block.particles[source];
    switch (field.key) {
    case FieldKey::Velocity:
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[i * comps + c] = static_cast<double>(p.vel[c]) * valueScale;
      }
      break;
    case FieldKey::Bfield: {
      float b[3] = {0.0f, 0.0f, 0.0f};
      (void)block.readSoAAs(soa_views::Bfield, source, b);
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[i * comps + c] = static_cast<double>(b[c]) * valueScale;
      }
      break;
    }
    case FieldKey::InternalEnergy:
    case FieldKey::Temperature:
      rows[i * comps] = static_cast<double>(p.temperature) * valueScale;
      break;
    case FieldKey::Metallicity:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Metallicity, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::ElectronAbundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::ElectronAbundance, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::H2Abundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::H2Abundance, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::HDAbundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::HDAbundance, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::J21:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::J21, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::Gamma:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Gamma, source, value);
        rows[i * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    default:
      break;
    }
  }
  return rows;
}

void WriteLoadedBackgroundRows(H5::DataSet& dst,
                               const FieldSpec& field,
                               const DatasetShape& shape,
                               std::size_t begin,
                               const BackgroundGridData& grid,
                               const SnapshotBackgroundGridConfig& config,
                               const std::vector<double>& nearestRows)
{
  if (grid.count == 0 || !FieldUsesBackgroundGrid(field.key)) return;
  if (field.key == FieldKey::ID) {
    const std::vector<std::uint64_t> ids = BackgroundIdRows(grid);
    WriteUInt64Rows(dst, shape, begin, grid.count, ids);
    return;
  }
  const std::vector<double> rows =
    BackgroundDoubleRows(field, grid, config, nearestRows);
  WriteDoubleRows(dst, shape, begin, grid.count, rows);
}

bool LoadedFieldAvailable(const SimulationBlock& block, FieldKey key, int ptype)
{
  auto sourceMarked = [&](FieldKey fieldKey) {
    return block.loadedFieldNames.empty() ||
           block.hasLoadedFieldForType(GetFieldKeyDisplayName(fieldKey), ptype);
  };

  switch (key) {
  case FieldKey::Position:
  case FieldKey::Velocity:
  case FieldKey::Density:
  case FieldKey::Temperature:
  case FieldKey::InternalEnergy:
  case FieldKey::Hsml:
  case FieldKey::Volume:
    return sourceMarked(key);
  case FieldKey::ID:
    return sourceMarked(key) && block.hasParticleIds();
  case FieldKey::Mass:
    return true;
  case FieldKey::Bfield:
    return sourceMarked(key) && block.hasSoAAs(soa_views::Bfield);
  case FieldKey::Metallicity:
    return sourceMarked(key) && block.hasSoAAs(soa_views::Metallicity);
  case FieldKey::ElectronAbundance:
    return sourceMarked(key) && block.hasSoAAs(soa_views::ElectronAbundance);
  case FieldKey::H2Abundance:
    return sourceMarked(key) && block.hasSoAAs(soa_views::H2Abundance);
  case FieldKey::HDAbundance:
    return sourceMarked(key) && block.hasSoAAs(soa_views::HDAbundance);
  case FieldKey::J21:
    return sourceMarked(key) && block.hasSoAAs(soa_views::J21);
  case FieldKey::Gamma:
    return sourceMarked(key) && block.hasSoAAs(soa_views::Gamma);
  case FieldKey::Value:
    return sourceMarked(key) && block.hasSoAAs(soa_views::Val1);
  case FieldKey::Value2:
    return sourceMarked(key) && block.hasSoAAs(soa_views::Val2);
  default:
    return false;
  }
}

void WriteLoadedDoubleDataset(H5::Group& outGroup,
                              const std::string& outputName,
                              const FieldSpec& field,
                              const SimulationBlock& block,
                              const std::vector<std::size_t>& keep,
                              const BackgroundGridData& backgroundGrid,
                              const SnapshotBackgroundGridConfig& background,
                              double valueScale)
{
  const std::size_t backgroundRows = backgroundGrid.count;
  const std::size_t outputRows = keep.size() + backgroundRows;
  DatasetShape shape = ShapeForFieldSpec(field, outputRows);
  H5::DataSet dst = CreateOutputDataset(outGroup,
                                        outputName,
                                        H5::PredType::NATIVE_DOUBLE,
                                        shape,
                                        outputRows);
  const std::size_t comps =
    static_cast<std::size_t>(std::max<hsize_t>(hsize_t{1}, shape.components));
  std::vector<double> rows(keep.size() * comps, 0.0);
  for (std::size_t row = 0; row < keep.size(); ++row) {
    const std::size_t index = keep[row];
    const SimulationElement& p = block.particles[index];
    switch (field.key) {
    case FieldKey::Position:
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[row * comps + c] = static_cast<double>(p.position[c]) * valueScale;
      }
      break;
    case FieldKey::Velocity:
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[row * comps + c] = static_cast<double>(p.vel[c]) * valueScale;
      }
      break;
    case FieldKey::Mass:
      rows[row * comps] = static_cast<double>(p.mass) * valueScale;
      break;
    case FieldKey::Density:
      rows[row * comps] = static_cast<double>(p.density) * valueScale;
      break;
    case FieldKey::InternalEnergy:
    case FieldKey::Temperature:
      rows[row * comps] = static_cast<double>(p.temperature) * valueScale;
      break;
    case FieldKey::Hsml:
      rows[row * comps] = static_cast<double>(p.supportRadius) * valueScale;
      break;
    case FieldKey::Bfield: {
      float b[3] = {0.0f, 0.0f, 0.0f};
      (void)block.readSoAAs(soa_views::Bfield, index, b);
      for (std::size_t c = 0; c < std::min<std::size_t>(3, comps); ++c) {
        rows[row * comps + c] = static_cast<double>(b[c]) * valueScale;
      }
      break;
    }
    case FieldKey::Metallicity:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Metallicity, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::ElectronAbundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::ElectronAbundance, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::H2Abundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::H2Abundance, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::HDAbundance:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::HDAbundance, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::J21:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::J21, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::Gamma:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Gamma, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::Value:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Val1, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::Value2:
      {
        float value = 0.0f;
        (void)block.readSoAAs(soa_views::Val2, index, value);
        rows[row * comps] = static_cast<double>(value) * valueScale;
      }
      break;
    case FieldKey::Volume:
      rows[row * comps] =
        static_cast<double>(p.supportRadius) *
        static_cast<double>(p.supportRadius) *
        static_cast<double>(p.supportRadius) * valueScale;
      break;
    default:
      break;
    }
  }
  if (!rows.empty()) {
    WriteDoubleRows(dst, shape, 0, keep.size(), rows);
  }
  if (backgroundRows > 0 && FieldUsesBackgroundGrid(field.key)) {
    const std::vector<double> nearestRows =
      LoadedBackgroundNearestRows(block,
                                  field,
                                  shape,
                                  backgroundGrid,
                                  valueScale);
    WriteLoadedBackgroundRows(dst,
                              field,
                              shape,
                              keep.size(),
                              backgroundGrid,
                              background,
                              nearestRows);
  }
}

void WriteLoadedIdDataset(H5::Group& outGroup,
                          const std::string& outputName,
                          const FieldSpec& field,
                          const SimulationBlock& block,
                          const std::vector<std::size_t>& keep,
                          const BackgroundGridData& backgroundGrid)
{
  const std::size_t outputRows = keep.size() + backgroundGrid.count;
  DatasetShape shape = ShapeForFieldSpec(field, outputRows);
  H5::DataSet dst = CreateOutputDataset(outGroup,
                                        outputName,
                                        H5::PredType::NATIVE_UINT64,
                                        shape,
                                        outputRows);
  std::vector<std::uint64_t> ids(keep.size(), 0);
  for (std::size_t i = 0; i < keep.size(); ++i) {
    ids[i] = block.particleId(keep[i]);
  }
  if (!ids.empty()) {
    WriteUInt64Rows(dst, shape, 0, keep.size(), ids);
  }
  if (backgroundGrid.count > 0) {
    const std::vector<std::uint64_t> backgroundIds = BackgroundIdRows(backgroundGrid);
    WriteUInt64Rows(dst, shape, keep.size(), backgroundGrid.count, backgroundIds);
  }
}

} // namespace

std::vector<FieldSpec> MakeDefaultSnapshotExtractFields()
{
  std::vector<FieldSpec> fields;
  auto push = [&](FieldKey key) {
    FieldSpec field;
    field.key = key;
    ApplyDefaultFieldSpec(field);
    fields.push_back(std::move(field));
  };

  push(FieldKey::Position);
  push(FieldKey::Velocity);
  push(FieldKey::ID);
  push(FieldKey::Mass);
  push(FieldKey::Density);
  push(FieldKey::InternalEnergy);
  push(FieldKey::Temperature);
  push(FieldKey::Hsml);
  push(FieldKey::Volume);
  push(FieldKey::Bfield);
  push(FieldKey::Metallicity);
  push(FieldKey::ElectronAbundance);
  push(FieldKey::H2Abundance);
  push(FieldKey::HDAbundance);
  push(FieldKey::J21);
  push(FieldKey::Gamma);
  return fields;
}

std::vector<SnapshotOutputFieldSpec> MakeDefaultSnapshotOutputFields()
{
  std::vector<SnapshotOutputFieldSpec> fields;
  auto push = [&](FieldKey key, unsigned int typeMask) {
    fields.push_back(MakeDefaultOutputField(key, typeMask));
  };

  push(FieldKey::Position, 0x3fu);
  push(FieldKey::Velocity, 0x3fu);
  push(FieldKey::ID, 0x3fu);
  push(FieldKey::Mass, 0x3fu);
  push(FieldKey::Density, 0x01u);
  push(FieldKey::InternalEnergy, 0x01u);
  push(FieldKey::Temperature, 0x01u);
  push(FieldKey::Hsml, 0x01u);
  push(FieldKey::Volume, 0x01u);
  push(FieldKey::Bfield, 0x01u);
  push(FieldKey::Metallicity, 0x01u);
  push(FieldKey::ElectronAbundance, 0x01u);
  push(FieldKey::H2Abundance, 0x01u);
  push(FieldKey::HDAbundance, 0x01u);
  push(FieldKey::J21, 0x01u);
  push(FieldKey::Gamma, 0x01u);
  return fields;
}

SnapshotExtractReport ExtractHdf5SnapshotRegion(const SnapshotExtractJob& job)
{
  SnapshotExtractReport report;
  std::string extractContext = "initializing";
  try {
    extractContext = "validating paths";
    if (job.inputPath.empty()) {
      throw std::runtime_error("inputPath is empty");
    }
    if (job.outputPath.empty()) {
      throw std::runtime_error("outputPath is empty");
    }
    std::filesystem::path outPath(job.outputPath);
    if (outPath.has_parent_path()) {
      std::filesystem::create_directories(outPath.parent_path());
    }

    extractContext = "opening input file '" + job.inputPath + "'";
    H5::H5File input(job.inputPath, H5F_ACC_RDONLY);
    extractContext = "reading source header";
    SourceHeader sourceHeader = ReadSourceHeader(input);
    extractContext = "reading source parameters";
    ParametersInfo parameters = ReadParameters(input);
    SnapshotExtractUnitConversion conversion = job.unitConversion;
    if (conversion.enabled) {
      conversion.sourceScaleFactor =
        SafePositive(conversion.sourceScaleFactor, sourceHeader.time);
      conversion.scaleFactor =
        SafePositive(conversion.scaleFactor, conversion.sourceScaleFactor);
      conversion.unitLengthCm = SafePositive(conversion.unitLengthCm, parameters.unitLength);
      conversion.unitMassG = SafePositive(conversion.unitMassG, parameters.unitMass);
      conversion.unitVelocityCmPerS =
        SafePositive(conversion.unitVelocityCmPerS, parameters.unitVelocity);
      conversion.hubbleParam = SafePositive(conversion.hubbleParam, parameters.hubbleParam);
    }
    ParametersInfo sourceParameters = ResolveSourceParameters(parameters, conversion);
    if (conversion.enabled) {
      conversion.unitLengthCm =
        SafePositive(conversion.unitLengthCm, sourceParameters.unitLength);
      conversion.unitMassG = SafePositive(conversion.unitMassG, sourceParameters.unitMass);
      conversion.unitVelocityCmPerS =
        SafePositive(conversion.unitVelocityCmPerS, sourceParameters.unitVelocity);
      conversion.hubbleParam =
        SafePositive(conversion.hubbleParam, sourceParameters.hubbleParam);
    }
    const ParametersInfo outputParameters =
      ApplyUnitConversionToParameters(sourceParameters, conversion);
    const double outputLengthScale =
      conversion.enabled
        ? UnitChangeScale(sourceParameters.unitLength, conversion.unitLengthCm) *
          ComovingLengthScale(conversion)
        : 1.0;

    report.sourceCounts = sourceHeader.counts;
    report.sourceParticles =
      std::accumulate(sourceHeader.counts.begin(), sourceHeader.counts.end(), std::size_t{0});

    std::array<std::vector<std::uint64_t>, 6> keepByType;
    for (int ptype = 0; ptype < 6; ++ptype) {
      extractContext = "building keep list for PartType" + std::to_string(ptype);
      keepByType[ptype] =
        BuildKeepList(input, ptype, sourceHeader.counts[ptype], job.region);
      report.extractedCounts[ptype] = keepByType[ptype].size();
    }
    report.extractedParticles =
      std::accumulate(report.extractedCounts.begin(),
                      report.extractedCounts.end(),
                      std::size_t{0});

    SnapshotBackgroundGridConfig background = job.backgroundGrid;
    extractContext = "assigning background grid IDs";
    const std::uint64_t backgroundFirstId =
      MaxSelectedParticleId(input, sourceHeader, keepByType) + 1u;
    extractContext = "building background grid";
    BackgroundGridData backgroundGrid =
      BuildBackgroundGrid(job.region,
                          background,
                          outputLengthScale,
                          backgroundFirstId);
    extractContext = "assigning nearest gas rows for background grid";
    AssignNearestGasRows(input, keepByType[0], outputLengthScale, backgroundGrid);
    extractContext = "culling occupied background grid cells";
    CullBackgroundGridNearSelectedParticles(input,
                                            keepByType,
                                            outputLengthScale,
                                            backgroundGrid);
    report.backgroundParticles = backgroundGrid.count;
    report.suggestedMeanVolume =
      backgroundGrid.count > 0 ? backgroundGrid.cellVolume : 0.0;
    report.suggestedReferenceGasPartMass =
      report.suggestedMeanVolume * background.density;
    report.extractedCounts[0] += backgroundGrid.count;
    report.extractedParticles += backgroundGrid.count;

    extractContext = "opening output file '" + job.outputPath + "'";
    H5::H5File output(job.outputPath, H5F_ACC_TRUNC);
    std::array<double, 6> outputMassTable = sourceHeader.massTable;
    const double outputMassScale = conversion.enabled
      ? UnitChangeScale(sourceParameters.unitMass, conversion.unitMassG)
      : 1.0;
    for (double& mass : outputMassTable) {
      if (mass != 0.0) mass *= outputMassScale;
    }
    for (int ptype = 0; ptype < 6; ++ptype) {
      if (report.extractedCounts[ptype] == 0) {
        outputMassTable[ptype] = 0.0;
      }
    }

    std::vector<FieldSpec> fields = NormalizeExtractFields(job.fields);
    std::vector<ActiveOutputField> outputFields =
      BuildActiveOutputFields(fields, job.outputFormat);
    if (backgroundGrid.count > 0) {
      EnsureBackgroundGridOutputFields(outputFields);
    }
    for (int ptype = 0; ptype < 6; ++ptype) {
      const auto& keep = keepByType[ptype];
      const std::size_t backgroundRows =
        ptype == 0 ? backgroundGrid.count : std::size_t{0};
      const std::size_t outputRows = keep.size() + backgroundRows;
      if (outputRows == 0) continue;

      extractContext = "creating output group " + PartGroupName(ptype);
      H5::Group outGroup = output.createGroup(PartGroupName(ptype));
      bool copiedMassDataset = false;

      for (const ActiveOutputField& outputField : outputFields) {
        const FieldSpec& field = outputField.field;
        if (IsTypePseudoField(field.key)) continue;
        if (!OutputFieldAppliesToType(outputField, ptype)) continue;
        const std::string sourceName = outputField.sourceName;
        if (sourceName.empty() || sourceName == "unknown" || sourceName == "dummy") {
          if (outputField.missingPolicy == SnapshotOutputMissingPolicy::Require) {
            throw std::runtime_error("required output field '" +
                                     outputField.outputName +
                                     "' has no input field mapping");
          }
          if (outputField.missingPolicy != SnapshotOutputMissingPolicy::FillDefault) {
            ++report.skippedDatasets;
            continue;
          }
        }

        const std::string sourcePath = sourceName.empty()
          ? std::string{}
          : DatasetPath(ptype, sourceName);
        if (sourcePath.empty() || !DatasetExists(input, sourcePath)) {
          if (backgroundRows > 0 &&
              FieldCanBeSynthesizedForBackgroundGrid(field.key)) {
            const std::string outputName = outputField.outputName;
            extractContext = "creating background-only dataset " +
                             PartGroupName(ptype) + "/" + outputName +
                             " from missing source '" + sourceName + "'";
            DatasetShape shape = ShapeForFieldSpec(field, outputRows);
            H5::DataSet dst = CreateOutputDataset(outGroup,
                                                  outputName,
                                                  NativeTypeForOutputField(field),
                                                  shape,
                                                  outputRows);
            if (outputField.missingPolicy == SnapshotOutputMissingPolicy::FillDefault) {
              FillRowsWithDefaultValue(dst, outputField, shape, 0, keep.size());
            } else {
              FillExistingRowsForMissingDataset(dst,
                                               field,
                                               shape,
                                               keep.size(),
                                               outputMassTable[ptype]);
            }
            const double valueScale =
              DatasetValueScale(field.key, sourceParameters, conversion);
            extractContext = "writing background rows for " +
                             PartGroupName(ptype) + "/" + outputName +
                             " from source '" + sourceName + "'";
            WriteBackgroundRows(input,
                                dst,
                                field,
                                shape,
                                keep.size(),
                                backgroundGrid,
                                background,
                                sourceName,
                                valueScale);
            copiedMassDataset = copiedMassDataset || field.key == FieldKey::Mass;
            ++report.copiedDatasets;
            continue;
          }
          if (outputField.missingPolicy == SnapshotOutputMissingPolicy::FillDefault) {
            const std::string outputName = outputField.outputName;
            DatasetShape shape = ShapeForFieldSpec(field, outputRows);
            H5::DataSet dst = CreateOutputDataset(outGroup,
                                                  outputName,
                                                  NativeTypeForOutputField(field),
                                                  shape,
                                                  outputRows);
            FillRowsWithDefaultValue(dst, outputField, shape, 0, outputRows);
            copiedMassDataset = copiedMassDataset || field.key == FieldKey::Mass;
            ++report.copiedDatasets;
            continue;
          }
          if (outputField.missingPolicy == SnapshotOutputMissingPolicy::Require) {
            throw std::runtime_error("required output dataset missing: " +
                                     sourcePath);
          }
          if (field.key == FieldKey::Mass && sourceHeader.massTable[ptype] != 0.0) {
            // Gadget/AREPO convention: constant mass is stored in Header/MassTable.
            continue;
          }
          ++report.skippedDatasets;
          continue;
        }

        const std::string outputName = outputField.outputName;
        const double valueScale = DatasetValueScale(field.key, sourceParameters, conversion);
        extractContext = "copying dataset " + sourcePath + " -> " +
                         PartGroupName(ptype) + "/" + outputName;
        if (job.outputFormat.enabled) {
          CopyDatasetWithKeepListAsOutputField(input,
                                               outGroup,
                                               ptype,
                                               sourceName,
                                               outputField,
                                               keep,
                                               outputRows,
                                               valueScale);
        } else {
          CopyDatasetWithKeepList(input,
                                  outGroup,
                                  ptype,
                                  sourceName,
                                  outputName,
                                  keep,
                                  outputRows,
                                  valueScale);
        }
        if (backgroundRows > 0 && FieldUsesBackgroundGrid(field.key)) {
          extractContext = "appending background rows to " +
                           PartGroupName(ptype) + "/" + outputName +
                           " from source '" + sourceName + "'";
          H5::DataSet dst = outGroup.openDataSet(outputName);
          DatasetShape shape = GetDatasetShape(dst);
          WriteBackgroundRows(input,
                              dst,
                              field,
                              shape,
                              keep.size(),
                              backgroundGrid,
                              background,
                              sourceName,
                              valueScale);
        }
        copiedMassDataset = copiedMassDataset || field.key == FieldKey::Mass;
        ++report.copiedDatasets;
      }

      if (copiedMassDataset) {
        outputMassTable[ptype] = 0.0;
      }
    }

    if (job.copyHeader) {
      extractContext = "writing Header";
      WriteHeader(output,
                  sourceHeader,
                  outputParameters,
                  job.region,
                  outputLengthScale,
                  report.extractedCounts,
                  outputMassTable);
    }
    if (job.copyParameters) {
      extractContext = "writing Parameters";
      WriteParameters(output, outputParameters, conversion);
    }

    std::ostringstream oss;
    oss << "extracted " << report.extractedParticles << " / "
        << report.sourceParticles << " particles to " << job.outputPath;
    if (report.backgroundParticles > 0) {
      oss << " (background grid=" << report.backgroundParticles << ")";
    }
    if (report.suggestedMeanVolume > 0.0) {
      oss << "\nadded particles=" << report.backgroundParticles
          << "\nSuggested value for MeanVolume=" << report.suggestedMeanVolume
          << "\nSuggested value for ReferenceGasPartMass="
          << report.suggestedReferenceGasPartMass;
    }
    if (conversion.enabled) {
      oss << " (unit conversion enabled)";
    }
    report.message = oss.str();
    report.ok = true;
  } catch (const H5::Exception& e) {
    report.ok = false;
    report.message = "HDF5 extract error while " + extractContext + ": " +
                     e.getDetailMsg();
  } catch (const std::exception& e) {
    report.ok = false;
    report.message = "extract error while " + extractContext + ": " + e.what();
  } catch (...) {
    report.ok = false;
    report.message = "unknown extract error while " + extractContext;
  }
  return report;
}

SnapshotExtractReport ExtractLoadedSnapshotRegionToHdf5(
  const SnapshotExtractJob& job,
  const SimulationBlock& block,
  const SnapshotLoadedExtractMetadata& metadata)
{
  SnapshotExtractReport report;
  std::string extractContext = "initializing loaded snapshot export";
  try {
    if (job.outputPath.empty()) {
      throw std::runtime_error("outputPath is empty");
    }
    std::filesystem::path outPath(job.outputPath);
    if (outPath.has_parent_path()) {
      std::filesystem::create_directories(outPath.parent_path());
    }

    SourceHeader sourceHeader = MakeSourceHeaderFromLoadedBlock(block, metadata);
    ParametersInfo sourceParameters = MakeParametersFromLoadedMetadata(metadata);
    SnapshotExtractUnitConversion conversion = job.unitConversion;
    if (conversion.enabled) {
      conversion.sourceScaleFactor =
        SafePositive(conversion.sourceScaleFactor, sourceHeader.time);
      conversion.scaleFactor =
        SafePositive(conversion.scaleFactor, conversion.sourceScaleFactor);
      conversion.unitLengthCm =
        SafePositive(conversion.unitLengthCm, sourceParameters.unitLength);
      conversion.unitMassG =
        SafePositive(conversion.unitMassG, sourceParameters.unitMass);
      conversion.unitVelocityCmPerS =
        SafePositive(conversion.unitVelocityCmPerS, sourceParameters.unitVelocity);
      conversion.hubbleParam =
        SafePositive(conversion.hubbleParam, sourceParameters.hubbleParam);
    }
    const ParametersInfo outputParameters =
      ApplyUnitConversionToParameters(sourceParameters, conversion);
    const double outputLengthScale =
      conversion.enabled
        ? UnitChangeScale(sourceParameters.unitLength, conversion.unitLengthCm) *
          ComovingLengthScale(conversion)
        : 1.0;

    report.sourceCounts = sourceHeader.counts;
    report.sourceParticles = block.particles.size();

    extractContext = "selecting loaded particles";
    const auto keepByType = BuildLoadedKeepLists(block, job.region);
    for (int ptype = 0; ptype < 6; ++ptype) {
      report.extractedCounts[ptype] = keepByType[ptype].size();
    }
    report.extractedParticles =
      std::accumulate(report.extractedCounts.begin(),
                      report.extractedCounts.end(),
                      std::size_t{0});

    SnapshotBackgroundGridConfig background = job.backgroundGrid;
    extractContext = "building loaded background grid";
    BackgroundGridData backgroundGrid =
      BuildBackgroundGrid(job.region,
                          background,
                          outputLengthScale,
                          MaxLoadedSelectedParticleId(block, keepByType) + 1u);
    AssignNearestGasRowsFromLoadedBlock(block,
                                        keepByType[0],
                                        outputLengthScale,
                                        backgroundGrid);
    extractContext = "culling loaded occupied background grid cells";
    CullBackgroundGridNearLoadedParticles(block,
                                          keepByType,
                                          outputLengthScale,
                                          backgroundGrid);
    report.backgroundParticles = backgroundGrid.count;
    report.suggestedMeanVolume =
      backgroundGrid.count > 0 ? backgroundGrid.cellVolume : 0.0;
    report.suggestedReferenceGasPartMass =
      report.suggestedMeanVolume * background.density;
    report.extractedCounts[0] += backgroundGrid.count;
    report.extractedParticles += backgroundGrid.count;

    extractContext = "opening output file '" + job.outputPath + "'";
    H5::H5File output(job.outputPath, H5F_ACC_TRUNC);

    std::array<double, 6> outputMassTable = sourceHeader.massTable;
    for (int ptype = 0; ptype < 6; ++ptype) {
      if (report.extractedCounts[ptype] == 0) {
        outputMassTable[ptype] = 0.0;
      }
    }

    std::vector<FieldSpec> fields = NormalizeExtractFields(job.fields);
    std::vector<ActiveOutputField> outputFields =
      BuildActiveOutputFields(fields, job.outputFormat);
    if (backgroundGrid.count > 0) {
      EnsureBackgroundGridOutputFields(outputFields);
    }

    for (int ptype = 0; ptype < 6; ++ptype) {
      const auto& keep = keepByType[ptype];
      const std::size_t backgroundRows =
        ptype == 0 ? backgroundGrid.count : std::size_t{0};
      const std::size_t outputRows = keep.size() + backgroundRows;
      if (outputRows == 0) continue;

      extractContext = "creating output group " + PartGroupName(ptype);
      H5::Group outGroup = output.createGroup(PartGroupName(ptype));
      BackgroundGridData emptyBackground;
      const BackgroundGridData& groupBackground =
        ptype == 0 ? backgroundGrid : emptyBackground;

      for (const ActiveOutputField& outputField : outputFields) {
        const FieldSpec& field = outputField.field;
        if (IsTypePseudoField(field.key)) continue;
        if (!OutputFieldAppliesToType(outputField, ptype)) continue;
        const std::string outputName = outputField.outputName;
        if (outputName.empty() || outputName == "unknown" || outputName == "dummy") {
          continue;
        }
        const bool loadedAvailable = LoadedFieldAvailable(block, field.key, ptype);
        const bool canSynthesizeBackground =
          backgroundRows > 0 && FieldCanBeSynthesizedForBackgroundGrid(field.key);
        if (!loadedAvailable && !canSynthesizeBackground) {
          if (outputField.missingPolicy == SnapshotOutputMissingPolicy::FillDefault) {
            DatasetShape shape = ShapeForFieldSpec(field, outputRows);
            H5::DataSet dst = CreateOutputDataset(outGroup,
                                                  outputName,
                                                  NativeTypeForOutputField(field),
                                                  shape,
                                                  outputRows);
            FillRowsWithDefaultValue(dst, outputField, shape, 0, outputRows);
            ++report.copiedDatasets;
            continue;
          }
          if (outputField.missingPolicy == SnapshotOutputMissingPolicy::Require) {
            throw std::runtime_error("required loaded output field missing: " +
                                     outputName);
          }
          ++report.skippedDatasets;
          continue;
        }

        extractContext = "writing loaded dataset " + PartGroupName(ptype) +
                         "/" + outputName;
        if (!loadedAvailable && canSynthesizeBackground &&
            outputField.missingPolicy == SnapshotOutputMissingPolicy::FillDefault) {
          DatasetShape shape = ShapeForFieldSpec(field, outputRows);
          H5::DataSet dst = CreateOutputDataset(outGroup,
                                                outputName,
                                                NativeTypeForOutputField(field),
                                                shape,
                                                outputRows);
          FillRowsWithDefaultValue(dst, outputField, shape, 0, keep.size());
          if (field.key == FieldKey::ID) {
            const std::vector<std::uint64_t> ids = BackgroundIdRows(groupBackground);
            WriteUInt64Rows(dst, shape, keep.size(), groupBackground.count, ids);
          } else {
            WriteLoadedBackgroundRows(dst,
                                      field,
                                      shape,
                                      keep.size(),
                                      groupBackground,
                                      background,
                                      {});
          }
          ++report.copiedDatasets;
          continue;
        }
        if (field.key == FieldKey::ID) {
          WriteLoadedIdDataset(outGroup, outputName, field, block, keep, groupBackground);
        } else {
          const double valueScale =
            DatasetValueScale(field.key, sourceParameters, conversion);
          WriteLoadedDoubleDataset(outGroup,
                                   outputName,
                                   field,
                                   block,
                                   keep,
                                   groupBackground,
                                   background,
                                   valueScale);
        }
        ++report.copiedDatasets;
      }
    }

    if (job.copyHeader) {
      extractContext = "writing Header";
      WriteHeader(output,
                  sourceHeader,
                  outputParameters,
                  job.region,
                  outputLengthScale,
                  report.extractedCounts,
                  outputMassTable);
    }
    if (job.copyParameters) {
      extractContext = "writing Parameters";
      WriteParameters(output, outputParameters, conversion);
    }

    std::ostringstream oss;
    oss << "exported loaded snapshot " << report.extractedParticles << " / "
        << report.sourceParticles << " particles to " << job.outputPath;
    if (report.backgroundParticles > 0) {
      oss << " (background grid=" << report.backgroundParticles << ")";
    }
    if (report.suggestedMeanVolume > 0.0) {
      oss << "\nadded particles=" << report.backgroundParticles
          << "\nSuggested value for MeanVolume=" << report.suggestedMeanVolume
          << "\nSuggested value for ReferenceGasPartMass="
          << report.suggestedReferenceGasPartMass;
    }
    report.message = oss.str();
    report.ok = true;
  } catch (const H5::Exception& e) {
    report.ok = false;
    report.message = "HDF5 loaded export error while " + extractContext + ": " +
                     e.getDetailMsg();
  } catch (const std::exception& e) {
    report.ok = false;
    report.message = "loaded export error while " + extractContext + ": " + e.what();
  } catch (...) {
    report.ok = false;
    report.message = "unknown loaded export error while " + extractContext;
  }
  return report;
}

#else

std::vector<FieldSpec> MakeDefaultSnapshotExtractFields()
{
  return {};
}

std::vector<SnapshotOutputFieldSpec> MakeDefaultSnapshotOutputFields()
{
  return {};
}

SnapshotExtractReport ExtractHdf5SnapshotRegion(const SnapshotExtractJob&)
{
  SnapshotExtractReport report;
  report.ok = false;
  report.message = "HDF5 support is disabled";
  return report;
}

SnapshotExtractReport ExtractLoadedSnapshotRegionToHdf5(
  const SnapshotExtractJob&,
  const SimulationBlock&,
  const SnapshotLoadedExtractMetadata&)
{
  SnapshotExtractReport report;
  report.ok = false;
  report.message = "HDF5 support is disabled";
  return report;
}

#endif
