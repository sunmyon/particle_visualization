#include "FindClumps/find_clumps_IO.h"
#include "FindClumps/clump_chain.h"
#include "data/clump_loader.h"
#include "core/quantity.h"
#include "core/units.h"

void give_stellar_id_to_clumps(int initstep, int nsnapshots, int dstep, std::string fname){
  TrackingVector<TrackingVector<ClumpEvolutionInfo>> clumps_all(nsnapshots);
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    uint32_t mask = (L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET | L_CLUMP_STELLAR_COUNT);
    ClumpInfoIO in;
    
    bool flag = ClumpIO::readSnapshot(fname, fileindex, mask, in);
    if(flag == false)
      continue;    

    size_t nClumps = in.clump_id.size();
    clumps_all[isnap].resize(nClumps);

    bool flag_star = false;
    for(size_t i=0;i<in.clump_id.size();i++){      
      clumps_all[isnap][i].size = in.clump_size[i];
      clumps_all[isnap][i].offset = in.clump_offset[i];
      clumps_all[isnap][i].index = in.clump_id[i];
      if(i < in.clump_next_id.size())
	clumps_all[isnap][i].next_index = in.clump_next_id[i];
      
      clumps_all[isnap][i].stellar_count = in.clump_stellar_count[i];
      clumps_all[isnap][i].flag_star = false; //just initial flag
      clumps_all[isnap][i].stellar_id = -1; //initialization
      
      if(in.clump_stellar_count[i] > 0)
	flag_star = true;
    }
   
    if(flag_star){
      uint32_t mask_part = L_ALL_PARTICLE_FIELDS;
      bool flag = ClumpIO::readSnapshot(fname, fileindex, mask_part, in);

      if(flag == false)
	printf("Why this happen? We could not read the file, %s\n", fname.c_str());      
      
      for(size_t i=0;i<in.clump_id.size();i++){      
	if(in.clump_stellar_count[i] == 0)
	  continue;

	int ID_min = -1;
	bool flag_first = true;

	int index_init = in.clump_offset[i];
	int len = in.clump_size[i];
	for(int ip=0;ip < len;ip++){
	  int index = index_init + ip;
	  if(in.particle_type[index] < 3)
	    continue;

	  if(flag_first){
	    ID_min = in.particle_ids[index];
	    flag_first = false;
	  }
	    
	  if(ID_min > in.particle_ids[index])
	    ID_min = in.particle_ids[index];
	}

	clumps_all[isnap][i].stellar_id = ID_min;
      }      
    }      
  }  

  for(int isnap=0;isnap<nsnapshots;isnap++){
    for(size_t ic=0;ic<clumps_all[isnap].size();ic++){
      if(clumps_all[isnap][ic].stellar_id >= 0)
	continue;
      
      if(clumps_all[isnap][ic].flag_star)
	continue;

      int stellar_id = -1;
      int next_index = clumps_all[isnap][ic].next_index;
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	int stellar_count = clumps_all[jsnap][next_index].stellar_count;
	if(stellar_count > 0){
	  stellar_id = clumps_all[jsnap][next_index].stellar_id;
	  break;
	}
	
	next_index = clumps_all[jsnap][next_index].next_index;
      }

      clumps_all[isnap][ic].stellar_id = stellar_id;
      clumps_all[isnap][ic].flag_star = true;
      
      next_index = clumps_all[isnap][ic].next_index;      
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	clumps_all[jsnap][next_index].stellar_id = stellar_id;
	clumps_all[jsnap][next_index].flag_star = true;
	
	int stellar_count = clumps_all[jsnap][next_index].stellar_count;
	if(stellar_count > 0)
	  break;
		
	next_index = clumps_all[jsnap][next_index].next_index;
      }      
    }
  }
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    ClumpInfoIO out;
    size_t nClumps = clumps_all[isnap].size();
    out.clump_stellar_id.resize(nClumps);
    
    for(size_t i=0;i<clumps_all[isnap].size();i++)
      out.clump_stellar_id[i] = clumps_all[isnap][i].stellar_id;

    uint32_t mask_out = L_CLUMP_STELLAR_ID;
    ClumpIO::writeSnapshot(fname, fileindex, mask_out, out);    
  }
}


void ClumpChain::makeEvolutionChains(int initstep, int nsnapshots, int dstep, const std::string& fname){
  chains_.clear();
  clumpLists_.clear();
  clumpLists_.resize(nsnapshots);
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET
		     | L_CLUMP_STELLAR_COUNT | L_CLUMP_STELLAR_ID
		     | L_CLUMP_STELLAR_MASS | L_CLUMP_STELLAR_MASS_MAXIMUM | L_CLUMP_POSITION | L_CLUMP_DENSITY
		     | L_CLUMP_TEMPERATURE | L_CLUMP_TEMP_DENSITY_WEIGHTED | L_CLUMP_MASS);
    ClumpInfoIO in;
    
    bool flag = ClumpIO::readSnapshot(fname, fileindex, mask, in);

    if(flag == false)
      continue;    

    size_t nClumps = in.clump_id.size();
    clumpLists_[isnap].resize(nClumps);
    
    for(size_t i=0;i<in.clump_id.size();i++){      
      clumpLists_[isnap][i].size = in.clump_size[i];
      clumpLists_[isnap][i].offset = in.clump_offset[i];

      clumpLists_[isnap][i].index = in.clump_id[i];
      if(in.clump_next_id.size())
	clumpLists_[isnap][i].next_index = in.clump_next_id[i];
      
      clumpLists_[isnap][i].stellar_count = in.clump_stellar_count[i];
      clumpLists_[isnap][i].stellar_id = in.clump_stellar_id[i];

      clumpLists_[isnap][i].pos[0] = in.clump_position[3*i+0];
      clumpLists_[isnap][i].pos[1] = in.clump_position[3*i+1];
      clumpLists_[isnap][i].pos[2] = in.clump_position[3*i+2];
      
      clumpLists_[isnap][i].mass = in.clump_mass[i];
      clumpLists_[isnap][i].density = in.clump_density[i];
      clumpLists_[isnap][i].temperature = in.clump_temperature[i];
      clumpLists_[isnap][i].temperature_d = in.clump_temperature_density_weighted[i];

      clumpLists_[isnap][i].stellar_mass = in.clump_stellar_mass[i];
      clumpLists_[isnap][i].stellar_mass_maximum = in.clump_stellar_mass_maximum[i];
      
      clumpLists_[isnap][i].flag_star = false; //just flag

      clumpLists_[isnap][i].snapindex = isnap;
      clumpLists_[isnap][i].time = in.time;
    } 
  }  
 
  int global_id = 0;
  for(int isnap=0;isnap<nsnapshots;isnap++){   
    for(auto &clump : clumpLists_[isnap]){
      if(clump.flag_star)
	continue;

      chains_[global_id].clear();
      
      TrackingVector<ClumpEvolutionInfo *> clump_chain;
      clump_chain.push_back(&clump);
      clump.flag_star = true;
      
      int next_index = clump.next_index;
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	ClumpEvolutionInfo& clump_next = clumpLists_[jsnap][next_index];
	if(clump_next.flag_star){
	  break; // this is merged branch
	}	
	
	clump_chain.push_back(&clump_next);
	clump_next.flag_star = true;
	
	next_index = clump_next.next_index;
      }

      for(auto &clump_member : clump_chain)
	clump_member->global_id = global_id;

      chains_.push_back(clump_chain);
      global_id++;
    }
  }

  chainComputed_ = true;
}


void ClumpChain::calcChainProperties(){
  if(!chainComputed_)
    return;
  
  props_.clear();
  
  for(size_t i=0;i< chains_.size();i++){
    auto &chain_i = chains_[i];
    
    if(chain_i.size() < LENGTH_MINIMUM_CHAIN && chain_i.front()->stellar_id == -1){
      //printf("skipped: global_id=%d\n", chain.front()->global_id);
      continue;
    }

    int next_index_last = chain_i.back()->next_index;
    if(next_index_last >= 0)
      continue;
    
    ClumpChainProperties ch;    
    ch.first_snapshot = chain_i.front()->snapindex;
    ch.last_snapshot = chain_i.back()->snapindex;
    
    ch.first_time = chain_i.front()->time;
    ch.last_time = chain_i.back()->time;
    
    ch.global_id = chain_i.front()->global_id;
    ch.stellar_id = chain_i.front()->stellar_id;

    int nstar = 0;
    float mstar = 0.;
    float temperature = 0., temperature_d = 0.;
    float density = 0.;
    float mstar_maximum = 0., mass_maximum = 0.;
    bool flagfirst = true;
    
    for(size_t j=0;j<chain_i.size();j++){
      ClumpEvolutionInfo *chain = chain_i[j];
      
      if(flagfirst && chain->stellar_count > 0){	
	ch.SF_snapshot = chain->snapindex;
	ch.SF_time = chain->time;
	flagfirst = false;
      }

      if(chain->stellar_count > nstar)
	nstar = chain->stellar_count;

      if(chain->stellar_mass > mstar)
	mstar = chain->stellar_mass;

      if(chain->stellar_mass_maximum > mstar_maximum)
	mstar_maximum = chain->stellar_mass_maximum;

      if(chain->mass > mass_maximum)
	mass_maximum = chain->mass;

      if(flagfirst == true){
	temperature = chain->temperature;
	temperature_d = chain->temperature_d;
	density = chain->density;
      }
    }

    ch.nstar = nstar;
    ch.mstar = mstar;
    ch.mstar_maximum = mstar_maximum;
    ch.mass_maximum = mass_maximum;
    ch.density = density;
    ch.temperature = temperature;
    ch.temperature_d = temperature_d;
    
    props_.push_back(ch);
  }

  propsComputed_ = true;
}

void ClumpChain::build(int initstep,
		       int nsnapshots,
		       int dstep,
		       const std::string& fname,
		       const UnitSystem& units,
		       double scaleFactor)
{
  makeEvolutionChains(initstep, nsnapshots, dstep, fname);
  calcChainProperties();

  QuantityUnitConverter converter;
  converter.rebuild(units, scaleFactor);
  const UnitSpace space = units.useComovingCoordinate
    ? UnitSpace::Comoving
    : UnitSpace::Physical;
  const double unit_mass_in_msun = converter.factor(QuantityId::Mass, space);

  for (auto& clump : props_) {
    clump.mstar *= unit_mass_in_msun;
    clump.mstar_maximum *= unit_mass_in_msun;
    clump.mass_maximum *= unit_mass_in_msun;
  }
}
