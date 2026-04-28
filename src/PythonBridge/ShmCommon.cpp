#include "ShmCommon.h"
#include "ShmLayout.h"
#include <vector>
#include <cstddef>
#include <cstring>
#include <algorithm>

static inline size_t align64(size_t x){ return (x + 63) & ~size_t(63); }

struct LayoutSpec { uint32_t id, dt, comps; };

static std::vector<LayoutSpec> make_specs(bool withB){
  std::vector<LayoutSpec> s = {
    {F_POS, DT_FLOAT32, 3}, {F_VEL, DT_FLOAT32, 3},
    {F_DENS, DT_FLOAT32, 1}, {F_TEMP, DT_FLOAT32, 1},
    {F_MASS, DT_FLOAT32, 1}, {F_HSML, DT_FLOAT32, 1},
    {F_VAL, DT_FLOAT32, 1},  {F_VAL2, DT_FLOAT32, 1},
    {F_ID,  DT_UINT32,  1},  {F_TYPE, DT_UINT8,   1},
    {F_ORIGPOS, DT_FLOAT32, 3},
    {F_MASK, DT_UINT8, 1},
    {F_FLAG, DT_UINT8, 1} 
  };
  if(withB) s.insert(s.begin()+2, {F_B, DT_FLOAT32, 3});
  return s;
}

struct BuiltLayout {
  std::vector<FieldEntry> entries;
  size_t total_bytes;
};

static BuiltLayout build_layout(uint64_t N, bool withB){
  auto specs = make_specs(withB);
  std::vector<FieldEntry> ents; ents.reserve(specs.size());
  size_t off_entries = align64(sizeof(ShmHeader));
  size_t table_bytes = align64(specs.size() * sizeof(FieldEntry));
  size_t cursor = off_entries + table_bytes;
  for(auto s : specs){
    size_t sz = N * s.comps * itemsize(s.dt);
    sz = align64(sz);
    FieldEntry e{};
    e.field_id = s.id; e.dtype = s.dt; e.ndim = (s.comps==1?1:2);
    e.comps = s.comps; e.offset = cursor; e.bytes = sz;
    ents.push_back(e);
    cursor += sz;
  }
  BuiltLayout bl; bl.entries = std::move(ents); bl.total_bytes = cursor;
  return bl;
}

// ---- POSIX shared memory minimal (macOS/Linux) ----
#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

static inline size_t align_up(size_t x, size_t a){ return (x + (a-1)) & ~(a-1); }

static bool mmap_file_fallback(const char* path, size_t bytes, ShmRegion& out){
  int fd = ::open(path, O_CREAT|O_RDWR, 0600);
  if (fd < 0) {
    std::fprintf(stderr, "[shm-fb] open('%s') failed: %s\n", path, std::strerror(errno));
    return false;
  }
  if (ftruncate(fd, (off_t)bytes) != 0) {
    std::fprintf(stderr, "[shm-fb] ftruncate('%s', %zu) failed: %s\n", path, bytes, std::strerror(errno));
    ::close(fd);
    return false;
  }
  void* p = mmap(nullptr, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    std::fprintf(stderr, "[shm-fb] mmap('%s', %zu) failed: %s\n", path, bytes, std::strerror(errno));
    ::close(fd);
    return false;
  }
  ::close(fd);
  out.base = p;
  out.bytes = bytes;
  out.name = path;
  out.fd = -1;
  out.unlinkOnDestroy = false;
  return true;
}

static bool shm_create_posix(const char* name, uint64_t N, bool withB, ShmRegion& out){
  std::string nm = (name && name[0]=='/') ? std::string(name) : ("/"+std::string(name));

  auto bl = build_layout(N, withB);
  size_t pagesz = (size_t)sysconf(_SC_PAGESIZE); if (!pagesz) pagesz = 4096;
  size_t total_aligned = align_up(bl.total_bytes, pagesz);

  // 1) Try POSIX shared memory first.
  int fd = shm_open(nm.c_str(), O_CREAT|O_RDWR, 0600);
  if (fd >= 0) {
    if (ftruncate(fd, (off_t)total_aligned) == 0) {
      void* p = mmap(nullptr, total_aligned, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
      if (p != MAP_FAILED) {
        ::close(fd);
        // Initialize the header.
        auto* hdr = reinterpret_cast<ShmHeader*>(p);
        hdr->magic=0xC0FFEE01; hdr->version=1; hdr->countN=N;
        hdr->n_fields=(uint32_t)bl.entries.size();
        hdr->field_mask=0; for(auto& e: bl.entries) hdr->field_mask |= (1u<<e.field_id);
        hdr->flags=2; hdr->reserved=0;
        auto* ents = reinterpret_cast<FieldEntry*>((uint8_t*)p + align64(sizeof(ShmHeader)));
        std::memcpy(ents, bl.entries.data(), bl.entries.size()*sizeof(FieldEntry));
        out.base=p;
        out.bytes=total_aligned;
        out.hdr=hdr;
        out.ents=ents;
        out.name=nm;
        out.fd=-1;
        out.unlinkOnDestroy=true;
        std::fprintf(stderr, "[shm] created POSIX shm '%s' (%zuB)\n", nm.c_str(), total_aligned);
        return true;
      }
      std::fprintf(stderr, "[shm] mmap failed: %s\n", std::strerror(errno));
    } else {
      std::fprintf(stderr, "[shm] ftruncate('%s', %zu) failed: %s (errno=%d)\n",
                   nm.c_str(), total_aligned, std::strerror(errno), errno);
    }
    ::close(fd);
    // Clean up to avoid leaving the name behind.
    shm_unlink(nm.c_str());
  } else {
    std::fprintf(stderr, "[shm] shm_open('%s') failed: %s\n", nm.c_str(), std::strerror(errno));
  }

  // 2) Fall back to file-backed mmap if POSIX shm fails.
  char fbpath[512];
  std::snprintf(fbpath, sizeof(fbpath), "/tmp%s.mm", nm.c_str()); // Example: /tmp/cppvis_pos.mm
  if (mmap_file_fallback(fbpath, total_aligned, out)) {
    // Initialize the header with the same contents.
    auto* hdr = reinterpret_cast<ShmHeader*>(out.base);
    hdr->magic=0xC0FFEE01; hdr->version=1; hdr->countN=N;
    hdr->n_fields=(uint32_t)bl.entries.size();
    hdr->field_mask=0; for(auto& e: bl.entries) hdr->field_mask |= (1u<<e.field_id);
    hdr->flags=2; hdr->reserved=0;
    auto* ents = reinterpret_cast<FieldEntry*>((uint8_t*)out.base + align64(sizeof(ShmHeader)));
    std::memcpy(ents, bl.entries.data(), bl.entries.size()*sizeof(FieldEntry));
    out.hdr = hdr; out.ents = ents;
    std::fprintf(stderr, "[shm] FELL BACK to file mmap '%s' (%zuB)\n", fbpath, out.bytes);
    return true;
  }

  return false;
}

static void shm_destroy_posix(ShmRegion& r){
  if (!r.base) return;
  munmap(r.base, r.bytes);
  // Only unlink POSIX shm. File fallback cleanup is intentionally optional.
  if (r.unlinkOnDestroy && !r.name.empty())
    shm_unlink(r.name.c_str());
  r = {};
}


#endif

bool shm_create_region(const char* name, uint64_t N, bool withB, ShmRegion& out){
#if !defined(_WIN32)
  return shm_create_posix(name, N, withB, out);
#else
  // TODO: Implement CreateFileMapping/MapViewOfFile.
  (void)name; (void)N; (void)withB; (void)out;
  return false;
#endif
}

void shm_destroy_region(ShmRegion& r){
#if !defined(_WIN32)
  shm_destroy_posix(r);
#else
  // TODO: UnmapViewOfFile / CloseHandle
  (void)r;
#endif
}
