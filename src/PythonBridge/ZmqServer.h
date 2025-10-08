#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

enum class FieldIdCpp : uint32_t {
  POS, VEL, B, DENS, TEMP, MASS, HSML, VAL, VAL2, ID, TYPE, ORIGPOS
};

struct BridgeParams {
  double UnitLength_in_pc=1, UnitMass_in_msolar=1, UnitVelocity_in_cgs=1e5, Hubble=1;
  double desiredMax=1, normalizationFactor=1;
  bool   useComoving=true;
};

class ZmqServer {
public:
  using EditCallback = std::function<void(const std::vector<FieldIdCpp>& dirty)>;
  using GetParamsFn  = std::function<BridgeParams()>;
  using SetParamsFn  = std::function<void(const BridgeParams&)>;

  bool start(const std::string& endpoint,
             EditCallback onEdited,
             GetParamsFn getParams,
             SetParamsFn setParams,
             // optional direct ops on shared arrays:
             std::function<void(float)> scaleFn,
             std::function<void(void)>  centerFn);

  void stop();
private:
  struct Impl; Impl* impl=nullptr;
};
