#pragma once
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

  float getHaloValue(const std::string &var) const;
};

struct HaloCatalog {
  TrackingVector<HaloData> haloes;
  std::vector<std::vector<uint64_t>> haloIDs;   // haloes.size() と同じ
};
