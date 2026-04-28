#pragma once
#include <iostream>
#include "core/tracking_vector.h"

class HaloData {
public:
  int   id;

  int   GroupLen;
  int   GroupLenType[6];
  float GroupMass;
  float GroupMassType[6];
  
  float GroupPos[3];
  float GroupVel[3];
  float radius;
  
  float GroupMetallicity[2];

  float getHaloValue(const std::string &var) const{
    if (var == "Mass")
      return GroupMass;
    else if (var == "GasMass")
      return GroupMassType[0];
    else if (var == "StellarMass")
      return GroupMassType[3] + GroupMassType[4] + GroupMassType[5];
    else if (var == "GasMetallicity")
      return GroupMetallicity[0];
    else if (var == "StellarMetallicity")
      return GroupMetallicity[1];
    else {
      std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
      return 0.0f;
    }
  }
};

struct HaloCatalog {
  TrackingVector<HaloData> haloes;
  std::vector<std::vector<uint64_t>> haloIDs;   // Same length as haloes.
};
