#pragma once
#include "ShmLayout.h"
#include <cstddef>
#include <cstdint>
#include <string>

// Information for one shared-memory region, shaped for both POSIX and Windows.
struct ShmRegion {
  void*      base = nullptr;
  size_t     bytes = 0;
  ShmHeader* hdr = nullptr;
  FieldEntry* ents = nullptr;
  std::string name;
  int        fd = -1;        // POSIX handle, unused on Windows.
  bool       unlinkOnDestroy = false;
};

// Create entries such as POS, VEL, and B according to withB, then create and initialize the region.
bool shm_create_region(const char* name, uint64_t N, bool withB, ShmRegion& out);

// Destroy the region with unmap plus unlink.
void shm_destroy_region(ShmRegion& r);
