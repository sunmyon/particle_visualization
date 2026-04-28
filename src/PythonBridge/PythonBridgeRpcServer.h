#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <thread>

#include "PythonBridge/ShmLayout.h"

class PythonBridgeRpcServer {
public:
  using DirtyCallback = std::function<void(FieldId)>;

  PythonBridgeRpcServer() = default;
  ~PythonBridgeRpcServer();

  PythonBridgeRpcServer(const PythonBridgeRpcServer&) = delete;
  PythonBridgeRpcServer& operator=(const PythonBridgeRpcServer&) = delete;

  bool start(const std::string& endpoint,
             DirtyCallback onDirty,
             std::string* errorMessage = nullptr);
  void stop();
  bool running() const { return running_.load(std::memory_order_acquire); }

private:
  static bool fieldNameToId(const std::string& name, FieldId& out);
  void serverLoop(std::string endpoint,
                  DirtyCallback onDirty,
                  std::promise<bool> started,
                  std::promise<std::string> startError);

  std::atomic<bool> running_{false};
  std::thread thread_;
};
