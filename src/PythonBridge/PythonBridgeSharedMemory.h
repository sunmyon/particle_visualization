#pragma once

#include <cstdint>
#include <string>

#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmCommon.h"

class PythonBridgeSharedMemory {
public:
  PythonBridgeSharedMemory() = default;
  ~PythonBridgeSharedMemory();

  PythonBridgeSharedMemory(const PythonBridgeSharedMemory&) = delete;
  PythonBridgeSharedMemory& operator=(const PythonBridgeSharedMemory&) = delete;

  bool create(uint64_t count, bool withB, const std::string& name);
  void destroy();
  void setValid(bool value);

  const PythonBridge::Shared& shared() const { return shared_; }

private:
  static std::string normalizeName(const std::string& name);
  void mapSharedPointers();

  ShmRegion region_{};
  PythonBridge::Shared shared_{};
};
