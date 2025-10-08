#pragma once
#include "ShmLayout.h"
#include <cstddef>
#include <cstdint>
#include <string>

// 共有メモリ1領域の情報（POSIX/Win どちらでも使える形）
struct ShmRegion {
  void*      base = nullptr;
  size_t     bytes = 0;
  ShmHeader* hdr = nullptr;
  FieldEntry* ents = nullptr;
  std::string name;
  int        fd = -1;        // POSIX での保持（Windows なら未使用）
};

// withB に応じてエントリ（POS,VEL,B, ...）を作り、領域を作成・初期化する
bool shm_create_region(const char* name, uint64_t N, bool withB, ShmRegion& out);

// 領域の破棄（unmap + unlink）
void shm_destroy_region(ShmRegion& r);
