#include "ZmqServer.h"
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include <thread>
#include <atomic>

struct ZmqServer::Impl {
  std::thread th;
  std::atomic<bool> running{false};
  void loop(const std::string& ep, EditCallback onEdited, GetParamsFn getP, SetParamsFn setP,
            std::function<void(float)> scaleFn, std::function<void(void)> centerFn){
    zmq::context_t ctx{1};
    zmq::socket_t rep{ctx, ZMQ_REP};
    rep.bind(ep);
    running = true;
    while(running){
      zmq::message_t req;
      if(!rep.recv(req, zmq::recv_flags::none)) continue;
      nlohmann::json in = nlohmann::json::parse(req.to_string(), nullptr, false);
      nlohmann::json out{{"status","ok"}};
      std::string cmd = in.value("cmd", "");
      if (cmd=="begin_edit") { /* flagは呼び手側で管理したければここでOK */ }
      else if (cmd=="end_edit") {
        std::vector<FieldIdCpp> dirty;
        if (in.contains("dirty")){
          for(auto& s : in["dirty"]){
            auto k = s.get<std::string>();
            if (k=="pos") dirty.push_back(FieldIdCpp::POS);
            else if (k=="vel") dirty.push_back(FieldIdCpp::VEL);
            else if (k=="B")   dirty.push_back(FieldIdCpp::B);
            else if (k=="temp")dirty.push_back(FieldIdCpp::TEMP);
          }
        } else dirty.push_back(FieldIdCpp::POS);
        if(onEdited) onEdited(dirty);
      }
      else if (cmd=="get_params") {
        if(getP){ auto p = getP();
          out["params"] = {
            {"UnitLength_in_pc",p.UnitLength_in_pc},{"UnitMass_in_msolar",p.UnitMass_in_msolar},
            {"UnitVelocity_in_cgs",p.UnitVelocity_in_cgs},{"Hubble",p.Hubble},
            {"desiredMax",p.desiredMax},{"normalizationFactor",p.normalizationFactor},
            {"useComoving",p.useComoving}
          };
        }
      }
      else if (cmd=="set_params") {
        if(setP && in.contains("params")){
          BridgeParams p;
          auto& j = in["params"];
          auto set=[&](auto& x,const char* k){ if(j.contains(k)) x=j[k].get<std::decay_t<decltype(x)>>(); };
          set(p.UnitLength_in_pc,"UnitLength_in_pc"); set(p.UnitMass_in_msolar,"UnitMass_in_msolar");
          set(p.UnitVelocity_in_cgs,"UnitVelocity_in_cgs"); set(p.Hubble,"Hubble");
          set(p.desiredMax,"desiredMax"); set(p.normalizationFactor,"normalizationFactor");
          set(p.useComoving,"useComoving");
          setP(p);
        }
      }
      else if (cmd=="scale")  { if(scaleFn)  scaleFn( (float)in.value("factor",1.0) ); }
      else if (cmd=="center") { if(centerFn) centerFn(); }
      else { out["status"]="unknown-cmd"; }
      rep.send(zmq::buffer(out.dump()), zmq::send_flags::none);
    }
  }
};

bool ZmqServer::start(const std::string& ep, EditCallback onE, GetParamsFn g, SetParamsFn s,
                      std::function<void(float)> scaleFn, std::function<void(void)> centerFn){
  if(impl) return false;
  impl = new Impl();
  impl->th = std::thread(&Impl::loop, impl, ep, onE, g, s, scaleFn, centerFn);
  return true;
}
void ZmqServer::stop(){
  if(!impl) return;
  impl->running=false;
  // poke with dummy req could be added; here we sleep shortly:
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if(impl->th.joinable()) impl->th.join();
  delete impl; impl=nullptr;
}
