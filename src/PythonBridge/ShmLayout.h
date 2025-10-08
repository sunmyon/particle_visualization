#pragma once
#include <cstdint>
#include <cstddef> 

enum DType : uint32_t { DT_FLOAT32=1, DT_FLOAT64=2, DT_UINT64=3, DT_UINT32=4, DT_UINT8=5 };
enum FieldId: uint32_t {
  F_POS=0, F_VEL=1, F_B=2, F_DENS=3, F_TEMP=4, F_MASS=5,
  F_HSML=6, F_VAL=7, F_VAL2=8, F_ID=9, F_TYPE=10, F_ORIGPOS=11,
  F_MASK=12,
  F_FLAG=13  
};

#pragma pack(push,1)
struct ShmHeader {
  uint32_t magic;     // 0xC0FFEE01
  uint32_t version;   // 1
  uint64_t countN;    // N
  uint32_t n_fields;  // entries count
  uint32_t field_mask;// presence bits
  uint32_t flags;     // bit0: editing, bit1: valid
  uint32_t reserved;  // padding
}; // 32B

struct FieldEntry {
  uint32_t field_id;  // FieldId
  uint32_t dtype;     // DType
  uint32_t ndim;      // 1 or 2
  uint32_t comps;     // components (1 or 3)
  uint64_t offset;    // from base
  uint64_t bytes;     // size of field blob
}; // 32B
#pragma pack(pop)

inline size_t itemsize(uint32_t dt){
  switch(dt){
    case DT_FLOAT32: return 4; case DT_FLOAT64: return 8;
    case DT_UINT64:  return 8; case DT_UINT32:  return 4; case DT_UINT8: return 1;
    default: return 0;
  }
}
