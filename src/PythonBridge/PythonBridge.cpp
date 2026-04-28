// PythonBridge.cpp: keep implementation dependencies contained here.
#include "PythonBridge.h"
#include <atomic>
#include <mutex>
#include <vector>

#include "PythonBridge/PythonBridgeRpcServer.h"
#include "PythonBridge/PythonBridgeSharedMemory.h"
#include "ShmLayout.h" 

// ===== PIMPL =====
class PythonBridgeImpl : public PythonBridge {
public:
  PythonBridgeImpl() = default;
  ~PythonBridgeImpl() override; 

  bool init(uint64_t N, bool withB, const std::string& shmNameHint) override;
  void shutdown() override;
  void setSharedValid(bool v) override;
  
  bool launchNotebook(const std::string& workdir) override;
  const JupyterInfo& notebookInfo() const override;
  const std::string& lastError() const override { return lastError_; }
  
  const Shared& shared() const override { return sharedMemory_.shared(); }
  
  BridgeParams getParams() const override { std::scoped_lock lk(mtx_); return params_; }
  void setParams(const BridgeParams& p)   override { std::scoped_lock lk(mtx_); params_ = p; }

  void pushDirty(FieldId f);
  void drainEditFields(std::vector<FieldId>& out) override;

private:
  BridgeParams params_{};

  std::atomic<bool> running_{false};
  mutable std::mutex mtx_;
  std::atomic<uint32_t> dirtyMask_{0}; 
  
  JupyterInfo nbInfo_; 
  std::string lastError_;
  PythonBridgeSharedMemory sharedMemory_;

  PythonBridgeRpcServer rpcServer_;
  std::string endpoint_ = "tcp://127.0.0.1:5557";
};

PythonBridge* CreatePythonBridge(){ return new PythonBridgeImpl(); }

PythonBridgeImpl::~PythonBridgeImpl() {
  shutdown();
}

bool PythonBridgeImpl::init(uint64_t N, bool withB, const std::string& shmNameHint){
  lastError_.clear();

  if (running_.load(std::memory_order_acquire)) {
    lastError_ = "Python bridge is already running";
    return false;
  }

  if (!sharedMemory_.create(N, withB, shmNameHint)) {
    lastError_ = "Failed to create shared memory";
    return false;
  }
  
  std::string rpcError;
  if (!rpcServer_.start(endpoint_,
                        [this](FieldId field) { pushDirty(field); },
                        &rpcError)) {
    sharedMemory_.destroy();
    lastError_ = rpcError.empty() ? "Failed to start RPC server" : rpcError;
    return false;
  }
  running_ = true;
  return true;
}

void PythonBridgeImpl::setSharedValid(bool v) {
  std::lock_guard<std::mutex> lk(mtx_);
  sharedMemory_.setValid(v);
}

void PythonBridgeImpl::shutdown(){
  running_ = false;
  rpcServer_.stop();
  sharedMemory_.destroy();
}

bool PythonBridgeImpl::launchNotebook(const std::string& workdir) {
  lastError_.clear();

  JupyterInfo info;
  if (!::launchJupyterNotebook(workdir, info)) {  // Call the existing JupyterLauncher.
    lastError_ = "Failed to launch Jupyter notebook";
    return false;
  }

  nbInfo_ = info;
  return true;
}

const JupyterInfo& PythonBridgeImpl::notebookInfo() const {
  return nbInfo_;
}

void PythonBridgeImpl::pushDirty(FieldId f) {
  const uint32_t bit = 1u << static_cast<uint32_t>(f);
  dirtyMask_.fetch_or(bit, std::memory_order_relaxed);
}

void PythonBridgeImpl::drainEditFields(std::vector<FieldId>& out) {
  const uint32_t bits = dirtyMask_.exchange(0, std::memory_order_acq_rel);
  out.clear();
  if (!bits) return;

  // Check only known fields, matching the ShmLayout.h enum.
  auto add_if = [&](FieldId f){
    uint32_t b = 1u << static_cast<uint32_t>(f);
    if (bits & b) out.push_back(f);
  };
  add_if(F_POS);
  add_if(F_VEL);
  add_if(F_B);
  add_if(F_DENS);
  add_if(F_TEMP);
  add_if(F_MASS);
  add_if(F_HSML);
  add_if(F_VAL);
  add_if(F_VAL2);
  add_if(F_ID);
  add_if(F_TYPE);
  add_if(F_ORIGPOS);
  add_if(F_FLAG);
  add_if(F_MASK);
}
