#pragma once

#include <string>
#include <iostream>

#include <vector>
#include "data/clump_data.h"

struct UnitSystem;
struct ClumpEvolutionInfo {
  int size = 0;
  int offset = 0;

  int index = -1;
  int next_index = -1;
  int stellar_count = 0;
  int stellar_id = -1;
  int global_id = -1;

  float stellar_mass = 0.0f;
  float stellar_mass_maximum = 0.0f;

  float mass = 0.0f;
  float density = 0.0f;
  float temperature = 0.0f;
  float temperature_d = 0.0f;
  float pos[3] = {0.0f, 0.0f, 0.0f};

  int snapindex = -1;
  float time = 0.0f;
  bool flag_star = false;

  float getValue(const std::string& var) const {
    if (var == "Density") return density;
    if (var == "Temperature") return temperature;
    if (var == "ClumpMass") return mass;
    if (var == "StellarMass") return stellar_mass;

    std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0.\n";
    return 0.0f;
  }
};

struct ClumpChainProperties {
  int first_snapshot = -1;
  int last_snapshot = -1;

  float first_time = 0.0f;
  float last_time = 0.0f;

  float density = 0.0f;
  float temperature = 0.0f;
  float temperature_d = 0.0f;

  int SF_snapshot = -1;
  float SF_time = 0.0f;

  int nstar = 0;
  float mstar = 0.0f;
  float mstar_maximum = 0.0f;
  float mass_maximum = 0.0f;

  int stellar_id = -1;
  int global_id = -1;
};

class ClumpChain {
public:
  static constexpr int LENGTH_MINIMUM_CHAIN = 5;

  ClumpChain() = default;

  void clear() {
    clumpLists_.clear();
    chains_.clear();
    props_.clear();
    chainComputed_ = false;
    propsComputed_ = false;
  }

  bool computed() const { return chainComputed_ && propsComputed_; }

  const std::vector<std::vector<ClumpEvolutionInfo*>>& chains() const { return chains_; }
  std::vector<std::vector<ClumpEvolutionInfo*>>& chains() { return chains_; }

  const std::vector<ClumpChainProperties>& props() const { return props_; }
  std::vector<ClumpChainProperties>& props() { return props_; }

  const std::vector<ClumpEvolutionInfo*>& chain(int i) const { return chains_[i]; }
  std::vector<ClumpEvolutionInfo*>& chain(int i) { return chains_[i]; }

  const ClumpChainProperties& prop(int i) const { return props_[i]; }
  ClumpChainProperties& prop(int i) { return props_[i]; }

  const std::vector<bool>& plot() const {return plotClumps_; }
  std::vector<bool>& plot() {return plotClumps_; }

  void ensurePlotSize() {
    if (plotClumps_.size() != props_.size()) {
      plotClumps_.resize(props_.size(), false);
    }
  }
  
  void build(int initstep,
	     int nsnapshots,
	     int dstep,
	     const std::string& fname,
	     const UnitSystem& units,
	     double scaleFactor = 1.0);

private:
  std::vector<std::vector<ClumpEvolutionInfo>> clumpLists_;
  std::vector<std::vector<ClumpEvolutionInfo*>> chains_;
  std::vector<ClumpChainProperties> props_;
  bool chainComputed_ = false;
  bool propsComputed_ = false;

  std::vector<bool> plotClumps_;
  
  void makeEvolutionChains(int initstep,
			   int nsnapshots,
			   int dstep,
			   const std::string& fname);

  void calcChainProperties(void);
};
