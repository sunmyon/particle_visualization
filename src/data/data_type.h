#pragma once
#include <cstdint>
#include <cstddef>

enum class DataType : uint8_t {
  Float = 0,
  Int32 = 1,
  Int64 = 2,
  Double = 3
};

inline size_t dataTypeSize(DataType t) {
  switch (t) {
    case DataType::Float:  return 4;
    case DataType::Int32:  return 4;
    case DataType::Int64:  return 8;
    case DataType::Double: return 8;
  }
  return 0;
}

template<typename T>
inline DataType toDataType();

template<> inline DataType toDataType<float>()   { return DataType::Float; }
template<> inline DataType toDataType<double>()  { return DataType::Double; }
template<> inline DataType toDataType<int32_t>() { return DataType::Int32; }
template<> inline DataType toDataType<int64_t>() { return DataType::Int64; }

struct DataTypeChoice {
  DataType type;
  const char* name;
};

inline constexpr DataTypeChoice kDataTypeChoices[] = {
  { DataType::Float,  "float"  },
  { DataType::Int32,  "int"    },
  { DataType::Int64,  "int64"  },
  { DataType::Double, "double" }
};

inline constexpr int kNumDataTypeChoices =
  static_cast<int>(sizeof(kDataTypeChoices) / sizeof(kDataTypeChoices[0]));

inline const char* GetDataTypeDisplayName(DataType type) {
  for (int i = 0; i < kNumDataTypeChoices; ++i) {
    if (kDataTypeChoices[i].type == type) return kDataTypeChoices[i].name;
  }
  return "unknown";
}

inline int GetDataTypeComboIndex(DataType type) {
  for (int i = 0; i < kNumDataTypeChoices; ++i) {
    if (kDataTypeChoices[i].type == type) return i;
  }
  return 0;
}

inline DataType GetDataTypeFromComboIndex(int index) {
  if (index < 0 || index >= kNumDataTypeChoices) {
    return DataType::Float;
  }
  return kDataTypeChoices[index].type;
}
