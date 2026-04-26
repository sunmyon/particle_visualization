// PythonBridge.cpp  — 依存はこの中に閉じ込める
#include "PythonBridge.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "ShmCommon.h"
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
  
  const Shared& shared() const override { return shared_; }
  
  BridgeParams getParams() const override { std::scoped_lock lk(mtx_); return params_; }
  void setParams(const BridgeParams& p)   override { std::scoped_lock lk(mtx_); params_ = p; }

  void pushDirty(FieldId f);
  void drainEditFields(std::vector<FieldId>& out) override;
  
  bool rpcScale(float factor) override { return sendSimpleRpc("scale", {{"factor",factor}}); }
  bool rpcCenter()            override { return sendSimpleRpc("center", {}); }

private:
  // ZMQ server
  void serverLoop();
  bool sendSimpleRpc(const char* cmd, nlohmann::json payload);

  // 共有メモリ
  bool shmCreate(uint64_t N, bool withB, const std::string& name);
  void shmDestroy();

  // ヘルパ
  void mapSharedPointers(); // entries→shared_ へポインタ設定
  
private:
  Shared shared_{};
  BridgeParams params_{};

  std::atomic<bool> running_{false};
  mutable std::mutex mtx_;
  std::atomic<uint32_t> dirtyMask_{0}; 
  
  JupyterInfo nbInfo_; 
  ShmRegion shm_;

  // ZMQ
  std::thread zmqThread_;
  std::string endpoint_ = "tcp://127.0.0.1:5557";
};

PythonBridge* CreatePythonBridge(){ return new PythonBridgeImpl(); }

PythonBridgeImpl::~PythonBridgeImpl() = default;

// ==== ここから追記（PythonBridge.cpp の末尾に）====

#include <chrono>   // sleep用
#include <utility>  // std::move

bool PythonBridgeImpl::init(uint64_t N, bool /*withB*/, const std::string& shmNameHint){
  // /name 形式に正規化（POSIX shm は先頭スラッシュが必要）
  shm_.name = shmNameHint.empty() ? "/cppvis_pos" :
             (shmNameHint.front()=='/' ? shmNameHint : ("/" + shmNameHint));

  shared_ = {};
  shared_.N = N;

  if (!shmCreate(N, /*withB=*/false, shm_.name)) {
    return false;
  }
  mapSharedPointers();
  
  // ZMQ 受信スレッド起動
  running_ = true;
  zmqThread_ = std::thread(&PythonBridgeImpl::serverLoop, this);
  return true;
}

void PythonBridgeImpl::setSharedValid(bool v) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (shm_.hdr) {
        if (v) shm_.hdr->flags |= 0b10;
        else   shm_.hdr->flags &= ~0b10;
    }
}

void PythonBridgeImpl::shutdown(){
  running_ = false;
  if (zmqThread_.joinable()) zmqThread_.join();
  shmDestroy(); // 今は空実装
}

bool PythonBridgeImpl::launchNotebook(const std::string& workdir) {
  JupyterInfo info;
  if (!::launchJupyterNotebook(workdir, info))  // ← 既存の JupyterLauncher を呼ぶ
    return false;

  nbInfo_ = info;
  return true;
}

const JupyterInfo& PythonBridgeImpl::notebookInfo() const {
  return nbInfo_;
}

void PythonBridgeImpl::serverLoop(){
  zmq::context_t ctx(1);
  zmq::socket_t  rep(ctx, zmq::socket_type::rep);
  rep.bind(endpoint_); // 例: tcp://127.0.0.1:5557

  while (running_) {
    // タイムアウト付き poll
    zmq::pollitem_t items[] = { { static_cast<void*>(rep), 0, ZMQ_POLLIN, 0 } };
    zmq::poll(items, 1, std::chrono::milliseconds(50));
    if (!(items[0].revents & ZMQ_POLLIN)) continue;

    zmq::message_t req;
    if (!rep.recv(req, zmq::recv_flags::none)) continue;

    nlohmann::json j;
    try { j = nlohmann::json::parse(req.to_string()); }
    catch (...) {
      nlohmann::json err = {{"status","error"},{"msg","bad json"}};
      rep.send(zmq::buffer(err.dump()), zmq::send_flags::none);
      continue;
    }

    const std::string cmd = j.value("cmd", "");
    if (cmd == "edit") {
      std::vector<FieldId> dirty;
      for (auto& name : j.value("fields", std::vector<std::string>{})) {
	if      (name=="pos")  pushDirty(F_POS);
	else if (name=="vel")  pushDirty(F_VEL);
	else if (name=="dens") pushDirty(F_DENS);
	else if (name=="temp") pushDirty(F_TEMP);
	else if (name=="mass") pushDirty(F_MASS);
	else if (name=="hsml") pushDirty(F_HSML);
	else if (name=="val")  pushDirty(F_VAL);
	else if (name=="val2") pushDirty(F_VAL2);
	else if (name=="id")   pushDirty(F_ID);
	else if (name=="type") pushDirty(F_TYPE);
	else if (name=="origpos") pushDirty(F_ORIGPOS);
	else if (name=="flag") pushDirty(F_FLAG);
	else if (name=="mask") pushDirty(F_MASK);
      }

      // 変更範囲（任意：最適化用）
      const bool hasRange = j.contains("start") && j.contains("count");
      size_t start = 0, count = 0;
      if (hasRange) { start = j["start"]; count = j["count"]; }

      nlohmann::json ok = {{"status","ok"}};
      if (hasRange) ok["accepted_range"] = {{"start", start}, {"count", count}};
      rep.send(zmq::buffer(ok.dump()), zmq::send_flags::none);
      continue;
    } else if (cmd == "ping") {
      rep.send(zmq::buffer(std::string("{\"status\":\"pong\"}")), zmq::send_flags::none);
    } else {
      nlohmann::json ng = {{"status","error"},{"msg","unknown cmd"}};
      rep.send(zmq::buffer(ng.dump()), zmq::send_flags::none);
    }
  }

  try { rep.close(); ctx.close(); } catch(...) {}
}

bool PythonBridgeImpl::sendSimpleRpc(const char* /*cmd*/, nlohmann::json /*payload*/){
  // いまは C++→Python の経路なし（必要になったら REQ ソケットを追加）
  return false;
}

bool PythonBridgeImpl::shmCreate(uint64_t N, bool withB, const std::string& name){
  const std::string nm = name.empty() ? "/cppvis" :
      (name.front()=='/' ? name : "/"+name);
  return shm_create_region(nm.c_str(), N, withB, shm_);
}

void PythonBridgeImpl::shmDestroy(){
  shm_destroy_region(shm_);
}

void PythonBridgeImpl::mapSharedPointers(){
  if (!shm_.hdr || !shm_.ents) return;

  shared_.N = shm_.hdr->countN;

  auto* base = static_cast<uint8_t*>(shm_.base);
  auto fieldPtr = [&](uint32_t id) -> void* {
    const uint32_t n = shm_.hdr->n_fields;
    for (uint32_t i=0; i<n; ++i) {
      if (shm_.ents[i].field_id == id)
        return base + shm_.ents[i].offset;
    }
    return nullptr;
  };

  // 必要なフィールドだけ張る（最低限 POS）
  shared_.pos  = static_cast<float*>(fieldPtr(F_POS));
  shared_.vel  = static_cast<float*>(fieldPtr(F_VEL));
  shared_.dens = static_cast<float*>(fieldPtr(F_DENS));
  shared_.temp = static_cast<float*>(fieldPtr(F_TEMP));
  shared_.mass = static_cast<float*>(fieldPtr(F_MASS));
  shared_.hsml = static_cast<float*>(fieldPtr(F_HSML));
  shared_.val  = static_cast<float*>(fieldPtr(F_VAL));
  shared_.val2 = static_cast<float*>(fieldPtr(F_VAL2));
  shared_.id   = static_cast<int*>(fieldPtr(F_ID));
  shared_.type = static_cast<uint8_t*>(fieldPtr(F_TYPE));
  shared_.pos_scaled = static_cast<float*>(fieldPtr(F_ORIGPOS));

  shared_.flag = static_cast<uint8_t*>(fieldPtr(F_FLAG));
  shared_.mask = static_cast<uint8_t*>(fieldPtr(F_MASK));

  //if(withB_)
  //  shared_.B = static_cast<float*>(fieldPtr(F_B));
}

void PythonBridgeImpl::pushDirty(FieldId f) {
  const uint32_t bit = 1u << static_cast<uint32_t>(f);
  dirtyMask_.fetch_or(bit, std::memory_order_relaxed);
}

void PythonBridgeImpl::drainEditFields(std::vector<FieldId>& out) {
  const uint32_t bits = dirtyMask_.exchange(0, std::memory_order_acq_rel);
  out.clear();
  if (!bits) return;

  // 既知のフィールドだけ見る（ShmLayout.h の列挙に合わせる）
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
// ==== 追記ここまで ====
