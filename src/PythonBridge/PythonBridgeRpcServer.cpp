#include "PythonBridge/PythonBridgeRpcServer.h"

#include <chrono>
#include <future>
#include <vector>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

PythonBridgeRpcServer::~PythonBridgeRpcServer()
{
  stop();
}

bool PythonBridgeRpcServer::start(const std::string& endpoint,
                                  DirtyCallback onDirty,
                                  std::string* errorMessage)
{
  if (running()) {
    if (errorMessage) *errorMessage = "RPC server is already running";
    return false;
  }

  std::promise<bool> startedPromise;
  std::promise<std::string> errorPromise;
  auto startedFuture = startedPromise.get_future();
  auto errorFuture = errorPromise.get_future();

  running_.store(true, std::memory_order_release);
  thread_ = std::thread(&PythonBridgeRpcServer::serverLoop,
                        this,
                        endpoint,
                        std::move(onDirty),
                        std::move(startedPromise),
                        std::move(errorPromise));

  if (!startedFuture.get()) {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    if (errorMessage) *errorMessage = errorFuture.get();
    return false;
  }

  return true;
}

void PythonBridgeRpcServer::stop()
{
  running_.store(false, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool PythonBridgeRpcServer::fieldNameToId(const std::string& name, FieldId& out)
{
  if      (name == "pos")     out = F_POS;
  else if (name == "vel")     out = F_VEL;
  else if (name == "B")       out = F_B;
  else if (name == "dens")    out = F_DENS;
  else if (name == "temp")    out = F_TEMP;
  else if (name == "mass")    out = F_MASS;
  else if (name == "hsml")    out = F_HSML;
  else if (name == "val")     out = F_VAL;
  else if (name == "val2")    out = F_VAL2;
  else if (name == "id")      out = F_ID;
  else if (name == "type")    out = F_TYPE;
  else if (name == "origpos") out = F_ORIGPOS;
  else if (name == "flag")    out = F_FLAG;
  else if (name == "mask")    out = F_MASK;
  else return false;
  return true;
}

void PythonBridgeRpcServer::serverLoop(std::string endpoint,
                                       DirtyCallback onDirty,
                                       std::promise<bool> started,
                                       std::promise<std::string> startError)
{
  bool startReported = false;
  try {
    zmq::context_t ctx(1);
    zmq::socket_t rep(ctx, zmq::socket_type::rep);
    rep.bind(endpoint);

    started.set_value(true);
    startError.set_value({});
    startReported = true;

    while (running()) {
      zmq::pollitem_t items[] = { { static_cast<void*>(rep), 0, ZMQ_POLLIN, 0 } };
      zmq::poll(items, 1, std::chrono::milliseconds(50));
      if (!(items[0].revents & ZMQ_POLLIN)) continue;

      zmq::message_t req;
      if (!rep.recv(req, zmq::recv_flags::none)) continue;

      nlohmann::json j;
      try {
        j = nlohmann::json::parse(req.to_string());
      } catch (...) {
        nlohmann::json err = {{"status", "error"}, {"msg", "bad json"}};
        rep.send(zmq::buffer(err.dump()), zmq::send_flags::none);
        continue;
      }

      const std::string cmd = j.value("cmd", "");
      if (cmd == "edit") {
        for (const auto& name : j.value("fields", std::vector<std::string>{})) {
          FieldId id{};
          if (fieldNameToId(name, id) && onDirty) {
            onDirty(id);
          }
        }

        nlohmann::json ok = {{"status", "ok"}};
        if (j.contains("start") && j.contains("count")) {
          ok["accepted_range"] = {{"start", j["start"]}, {"count", j["count"]}};
        }
        rep.send(zmq::buffer(ok.dump()), zmq::send_flags::none);
      } else if (cmd == "ping") {
        rep.send(zmq::buffer(std::string("{\"status\":\"pong\"}")),
                 zmq::send_flags::none);
      } else {
        nlohmann::json ng = {{"status", "error"}, {"msg", "unknown cmd"}};
        rep.send(zmq::buffer(ng.dump()), zmq::send_flags::none);
      }
    }
  } catch (const std::exception& e) {
    if (!startReported) {
      started.set_value(false);
      startError.set_value(e.what());
    }
  } catch (...) {
    if (!startReported) {
      started.set_value(false);
      startError.set_value("unknown RPC server error");
    }
  }

  running_.store(false, std::memory_order_release);
}
