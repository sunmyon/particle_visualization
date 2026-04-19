#pragma once
#include <algorithm>
#include <iostream>
#include "core/tracking_vector.h"

class ClumpData {
public:
  int clumpID = -1;
  int nextClumpID = -1;

  int count = 0;
  int offset = 0;

  float originalPos[3] = {0.,0.,0.};
  float Pos[3] = {0.,0.,0.};
  float density = 0.;
  float temperature = 0.;
  float mass = 0.;

  int stellar_count = 0;
  int stellar_id = -1;
  float stellar_mass = 0.;
  
  TrackingVector<int> IDs;
  
  float getClumpValue(const std::string &var) const;

  void get_next_clump_position(const TrackingVector<ClumpData>& clump_in_next_snapshot, float *next_pos){
    struct ParticleInfo {
      int ID;
      int clumpID;
    };

    TrackingVector<struct ParticleInfo> particleIDs;

    for(size_t ic=0;ic< clump_in_next_snapshot.size();ic++){
      for(auto id : clump_in_next_snapshot[ic].IDs){
	struct ParticleInfo temp;
	temp.ID = id;
	temp.clumpID = ic;
	
	particleIDs.push_back(temp);      
      }
    }
  
    std::sort(particleIDs.begin(), particleIDs.end(),
	      [](const auto &a, const auto &b) {
		return a.ID < b.ID;
	      });
    
    TrackingVector<int> counts(clump_in_next_snapshot.size(), 0);    
    size_t i1=0, i2=0;
    while(i1 < IDs.size() && i2 < particleIDs.size()){
      if(IDs[i1] < particleIDs[i2].ID){
	i1++;
      }
      else if(IDs[i1] > particleIDs[i2].ID){
	i2++;
      }
      else {
	int clumpID = particleIDs[i2].clumpID;
	counts[clumpID]++;	
	i1++; i2++;
      }
    }
      
    int max_count = -1;
    int max_index = -1;
    for(size_t i=0;i < clump_in_next_snapshot.size();i++){
      if(counts[i] > max_count){
	max_count = counts[i];
	max_index = static_cast<int>(i);
      }
    }

    next_pos[0] = clump_in_next_snapshot[max_index].Pos[0];
    next_pos[1] = clump_in_next_snapshot[max_index].Pos[1];
    next_pos[2] = clump_in_next_snapshot[max_index].Pos[2];
  }

  float getValue(const std::string &var) const{
    if (var == "Density")
      return density;
    else if (var == "Temperature")
      return temperature;
    else if (var == "ClumpMass")
      return mass;
    else if (var == "StellarMass")
      return stellar_mass;
    else {
      std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
      return 0.0f;
    }
  }  
};
