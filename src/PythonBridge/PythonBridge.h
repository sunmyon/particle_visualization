// PythonBridge.h is the only part visible from main.
#pragma once
#include <functional>
#include <string>
#include <cstdint>

#include "PythonBridge/JupyterLauncher.h"
#include "ShmLayout.h"

struct BridgeParams {
  double UnitLength_in_pc=1, UnitMass_in_msolar=1, UnitVelocity_in_cgs=1e5, Hubble=1;
  double desiredMax=1, normalizationFactor=1;
  bool   useComoving=true;
};

class PythonBridge {
public:
  struct Shared {
    uint64_t N=0;
    // Raw SoA pointers. nullptr is allowed. Types are fixed:
    // POS/VEL/B/ORIGPOS are float[3*N], scalars are float[N],
    // ID is uint32[N], TYPE is uint8[N].
    float   *pos=nullptr, *vel=nullptr, *B=nullptr, *dens=nullptr, *temp=nullptr,
            *mass=nullptr, *hsml=nullptr, *val=nullptr, *val2=nullptr, *origpos=nullptr;
    uint32_t* id=nullptr;
    uint8_t* type=nullptr, *flag=nullptr, *mask=nullptr;    
  };

  // Lifecycle.
  virtual bool init(uint64_t N, bool withB, const std::string& shmNameHint)=0;
  virtual void shutdown()=0;
  virtual void setSharedValid(bool v) = 0;
  
  // Launch Jupyter asynchronously. Returns true on success.
  virtual bool launchNotebook(const std::string& workdir) = 0;
  virtual const JupyterInfo& notebookInfo() const = 0;
  virtual const std::string& lastError() const = 0;
  
  // Reference to the zero-copy shared arrays.
  virtual const Shared& shared() const=0;
  
  // Parameter API.
  virtual BridgeParams getParams() const=0;
  virtual void setParams(const BridgeParams&)=0;

  // Python edit notification, called on end_edit.
  virtual void drainEditFields(std::vector<FieldId>& out) = 0; 
  
  virtual ~PythonBridge() = default;
};

// Factory. Implemented in .cpp to hide dependencies via PIMPL.
PythonBridge* CreatePythonBridge();  // Returns a newly allocated instance.
