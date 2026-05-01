#include "PythonBridge/PythonBridgeSharedMemory.h"

PythonBridgeSharedMemory::~PythonBridgeSharedMemory()
{
  destroy();
}

bool PythonBridgeSharedMemory::create(uint64_t count,
                                      bool withB,
                                      const std::string& name)
{
  destroy();

  const std::string normalizedName = normalizeName(name);
  if (!shm_create_region(normalizedName.c_str(), count, withB, region_)) {
    shared_ = {};
    return false;
  }

  mapSharedPointers();
  return shared_.pos != nullptr;
}

void PythonBridgeSharedMemory::destroy()
{
  shm_destroy_region(region_);
  shared_ = {};
}

void PythonBridgeSharedMemory::setValid(bool value)
{
  if (!region_.hdr) return;

  if (value) {
    region_.hdr->flags |= 0b10;
  } else {
    region_.hdr->flags &= ~0b10;
  }
}

std::string PythonBridgeSharedMemory::normalizeName(const std::string& name)
{
  if (name.empty()) return "/cppvis_pos";
  return name.front() == '/' ? name : "/" + name;
}

void PythonBridgeSharedMemory::mapSharedPointers()
{
  shared_ = {};
  if (!region_.base || !region_.hdr || !region_.ents) return;

  shared_.N = region_.hdr->countN;

  auto* base = static_cast<uint8_t*>(region_.base);
  auto fieldPtr = [&](uint32_t id) -> void* {
    const uint32_t n = region_.hdr->n_fields;
    for (uint32_t i = 0; i < n; ++i) {
      if (region_.ents[i].field_id == id) {
        return base + region_.ents[i].offset;
      }
    }
    return nullptr;
  };

  shared_.pos = static_cast<float*>(fieldPtr(F_POS));
  shared_.vel = static_cast<float*>(fieldPtr(F_VEL));
  shared_.B = static_cast<float*>(fieldPtr(F_B));
  shared_.dens = static_cast<float*>(fieldPtr(F_DENS));
  shared_.temp = static_cast<float*>(fieldPtr(F_TEMP));
  shared_.mass = static_cast<float*>(fieldPtr(F_MASS));
  shared_.hsml = static_cast<float*>(fieldPtr(F_HSML));
  shared_.val = static_cast<float*>(fieldPtr(F_VAL));
  shared_.val2 = static_cast<float*>(fieldPtr(F_VAL2));
  shared_.id = static_cast<uint64_t*>(fieldPtr(F_ID));
  shared_.type = static_cast<uint8_t*>(fieldPtr(F_TYPE));
  shared_.origpos = static_cast<float*>(fieldPtr(F_ORIGPOS));
  shared_.flag = static_cast<uint8_t*>(fieldPtr(F_FLAG));
  shared_.mask = static_cast<uint8_t*>(fieldPtr(F_MASK));
}
