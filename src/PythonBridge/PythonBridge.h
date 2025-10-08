// PythonBridge.h  ← main から見えるのはこれだけ
#pragma once
#include <functional>
#include <string>
#include <cstdint>

#include "main.h"
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
    // SoA生ポインタ（nullptr可、型は固定：POS/VEL/Bはfloat[3*N]、スカラはfloat[N]、IDはuint64[N]、TYPEはuint8[N]）
    float   *pos=nullptr, *vel=nullptr, *B=nullptr, *dens=nullptr, *temp=nullptr,
            *mass=nullptr, *hsml=nullptr, *val=nullptr, *val2=nullptr, *pos_scaled=nullptr;
    int* id=nullptr;
    uint8_t* type=nullptr, *flag=nullptr, *mask=nullptr;    
  };

  // ライフサイクル
  virtual bool init(uint64_t N, bool withB, const std::string& shmNameHint)=0;
  virtual void shutdown()=0;
  virtual void setSharedValid(bool v) = 0;
  
  // Jupyter 起動（非同期）。成功なら true
  virtual bool launchNotebook(const std::string& workdir) = 0;
  virtual const JupyterInfo& notebookInfo() const = 0;
  
  // 共有配列（ゼロコピー）の参照
  virtual const Shared& shared() const=0;
  
  // パラメータAPI
  virtual BridgeParams getParams() const=0;
  virtual void setParams(const BridgeParams&)=0;

  // Python編集の反映通知（end_edit時に呼ばれる）
  virtual void drainEditFields(std::vector<FieldId>& out) = 0; 
  
  // 任意RPC（必要なら）
  virtual bool rpcScale(float factor)=0;
  virtual bool rpcCenter()=0;

  virtual ~PythonBridge() = default;
};

// ファクトリ（実装は .cpp 内、依存を隠蔽：PIMPL）
PythonBridge* CreatePythonBridge();  // new 返却

