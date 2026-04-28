#include "core/tracking_vector.h"
#include "data/clump_data.h"

#include "FileIO/clump_io.h"
#include "analysis/clump/structure_nodes.h"

#ifdef CLUMP_DATA_READ
#include "H5Cpp.h"
#include "FileIO/hdf5_utils.h"

namespace ClumpIO {
  template<typename T>
  static void readDatasetIf(uint32_t maskFlag,
			    uint32_t mask,
			    H5::Group& group,
			    const char* dsetName,
			    TrackingVector<T>& container)
  {
    if ((mask & maskFlag) == 0) {
      return;
    }

    HDF5Utils::readDataset1D(group, dsetName, container);
  }

  template<typename T>
  static void writeDatasetIf(uint32_t maskFlag,
			     uint32_t mask,
			     H5::Group& group,
			     const char* dsetName,
			     const TrackingVector<T>& container)
  {
    if ((mask & maskFlag) == 0) {
      return;
    }

    HDF5Utils::writeDataset1D(group, dsetName, container);
  }
  
  bool readSnapshot(const std::string& fname, int snapshotID, uint32_t mask, ClumpInfoIO& out){
    try {
      H5::H5File file(fname, H5F_ACC_RDONLY);
      std::string snapName = "/snapshot_" + std::to_string(snapshotID);
    
      H5::Group snapGroup = file.openGroup(snapName);
    
      if (mask & L_TIME) {
	if (snapGroup.attrExists("time")) {
	  H5::Attribute timeAttr = snapGroup.openAttribute("time");
	  timeAttr.read(H5::PredType::NATIVE_FLOAT, &out.time);
	  timeAttr.close();
	} else {
	  out.time = 0.0f;
	}
      }
    
      if (mask & L_ALL_CLUMP_FIELDS) {
	if (!HDF5Utils::datasetExists(snapGroup, "clumps")) {
	  return false;
	}
      
	H5::Group clumpsGroup = snapGroup.openGroup("clumps");
	
	readDatasetIf<int>  (L_CLUMP_ID,                     mask, clumpsGroup, "clump_id",             out.clump_id);
	readDatasetIf<int>  (L_CLUMP_NEXT_ID,                mask, clumpsGroup, "clump_next_id",        out.clump_next_id);
	readDatasetIf<int>  (L_CLUMP_OFFSET,                 mask, clumpsGroup, "clump_offset",         out.clump_offset);
	readDatasetIf<int>  (L_CLUMP_SIZE,                   mask, clumpsGroup, "clump_size",           out.clump_size);
	readDatasetIf<int>  (L_CLUMP_STELLAR_COUNT,          mask, clumpsGroup, "clump_stellar_count",  out.clump_stellar_count);
	readDatasetIf<int>  (L_CLUMP_STELLAR_ID,             mask, clumpsGroup, "clump_stellar_id",     out.clump_stellar_id);
	readDatasetIf<float>(L_CLUMP_POSITION,               mask, clumpsGroup, "clump_position",                out.clump_position);
	readDatasetIf<float>(L_CLUMP_DENSITY,                mask, clumpsGroup, "clump_density",                 out.clump_density);
	readDatasetIf<float>(L_CLUMP_TEMPERATURE,            mask, clumpsGroup, "clump_temperature",             out.clump_temperature);
	readDatasetIf<float>(L_CLUMP_TEMP_DENSITY_WEIGHTED,  mask, clumpsGroup, "clump_temperature_density_weighted", out.clump_temperature_density_weighted);
	readDatasetIf<float>(L_CLUMP_MASS,                   mask, clumpsGroup, "clump_mass",                    out.clump_mass);
	readDatasetIf<float>(L_CLUMP_STELLAR_MASS,           mask, clumpsGroup, "clump_stellar_mass",            out.clump_stellar_mass);
	readDatasetIf<float>(L_CLUMP_STELLAR_MASS_MAXIMUM,   mask, clumpsGroup, "clump_stellar_mass_maximum",    out.clump_stellar_mass_maximum);
	
	clumpsGroup.close();
      }
      
      if ((mask & L_PARTICLE_IDS) || (mask & L_PARTICLE_TYPE)) {
	if (HDF5Utils::datasetExists(snapGroup, "particles")) {
	  H5::Group partsGroup = snapGroup.openGroup("particles");
	  readDatasetIf<int>  (L_PARTICLE_IDS,  mask, partsGroup, "sorted_particle_id", out.particle_ids);
	  readDatasetIf<char> (L_PARTICLE_TYPE, mask, partsGroup, "type",               out.particle_type);
	  partsGroup.close();
	}
      }
          
      snapGroup.close();
      file.close();
    
      return true;
    }
    catch (const H5::Exception& e) {
      std::cerr << "[ClumpIO::readSnapshot] HDF5 Exception: " << e.getCDetailMsg() << "\n";
      return false;
    }
    catch (...) {
      std::cerr << "[ClumpIO::readSnapshot] Unknown error\n";
      return false;
    }
  }
  
  bool writeSnapshot(const std::string& fname, int snapshotID, uint32_t mask, const ClumpInfoIO& data){
    try {
      H5::H5File file;
      try {
	file.openFile(fname, H5F_ACC_RDWR);
      }
      catch (...) {
	file = H5::H5File(fname, H5F_ACC_TRUNC);
      }

      std::string snapName = "/snapshot_" + std::to_string(snapshotID);
      H5::Group snapGroup;

      // 2) Open or create the "/snapshot_<snapshotID>" group.
      if (H5Lexists(file.getId(), snapName.c_str(), H5P_DEFAULT) > 0) {
	snapGroup = file.openGroup(snapName);
      } else {
	snapGroup = file.createGroup(snapName);
      }
      
      // 3) Write the time attribute, replacing it if it already exists.
      if (mask & L_TIME) {
	if (snapGroup.attrExists("time")) {
	  snapGroup.removeAttr("time");
	}
	hsize_t dims = 1;
	H5::DataSpace attr_space(1, &dims);
	H5::Attribute timeAttr = snapGroup.createAttribute("time", H5::PredType::NATIVE_FLOAT, attr_space);
	float t = data.time;
	timeAttr.write(H5::PredType::NATIVE_FLOAT, &t);
	timeAttr.close();
      }

      // 3) Write the density threshold attribute, replacing it if it already exists.
      if (mask & L_DENSITY_THRESHOLD) {
	if (snapGroup.attrExists("density_threshold")) {
	  snapGroup.removeAttr("density_threshold");
	}
	hsize_t dims = 1;
	H5::DataSpace attr_space(1, &dims);
	H5::Attribute dthAttr = snapGroup.createAttribute("density_threshold", H5::PredType::NATIVE_FLOAT, attr_space);
	float dth = data.density_threshold;
	dthAttr.write(H5::PredType::NATIVE_FLOAT, &dth);
	dthAttr.close();
      }

      bool needClumpsGroup = (mask & L_ALL_CLUMP_FIELDS) != 0;
      H5::Group clumpsGroup;
      if (needClumpsGroup) {
	if (HDF5Utils::datasetExists(snapGroup, "clumps")) {
	  clumpsGroup = snapGroup.openGroup("clumps");
	} else {
	  clumpsGroup = snapGroup.createGroup("clumps");
	}

        writeDatasetIf<int>  (L_CLUMP_ID,                       mask, clumpsGroup, "clump_id",                      data.clump_id);
        writeDatasetIf<int>  (L_CLUMP_NEXT_ID,                  mask, clumpsGroup, "clump_next_id",                 data.clump_next_id);
        writeDatasetIf<int>  (L_CLUMP_OFFSET,                   mask, clumpsGroup, "clump_offset",                  data.clump_offset);
        writeDatasetIf<int>  (L_CLUMP_SIZE,                     mask, clumpsGroup, "clump_size",                    data.clump_size);
        writeDatasetIf<int>  (L_CLUMP_STELLAR_COUNT,            mask, clumpsGroup, "clump_stellar_count",           data.clump_stellar_count);
	writeDatasetIf<int>  (L_CLUMP_STELLAR_ID,               mask, clumpsGroup, "clump_stellar_id",              data.clump_stellar_id);
        writeDatasetIf<float>(L_CLUMP_POSITION,                 mask, clumpsGroup, "clump_position",                data.clump_position);
        writeDatasetIf<float>(L_CLUMP_DENSITY,                  mask, clumpsGroup, "clump_density",                 data.clump_density);
        writeDatasetIf<float>(L_CLUMP_TEMPERATURE,              mask, clumpsGroup, "clump_temperature",             data.clump_temperature);
	writeDatasetIf<float>(L_CLUMP_TEMP_DENSITY_WEIGHTED,    mask, clumpsGroup, "clump_temperature_density_weighted", data.clump_temperature_density_weighted);
        writeDatasetIf<float>(L_CLUMP_MASS,                     mask, clumpsGroup, "clump_mass",                    data.clump_mass);
        writeDatasetIf<float>(L_CLUMP_STELLAR_MASS,             mask, clumpsGroup, "clump_stellar_mass",            data.clump_stellar_mass);
        writeDatasetIf<float>(L_CLUMP_STELLAR_MASS_MAXIMUM,     mask, clumpsGroup, "clump_stellar_mass_maximum",    data.clump_stellar_mass_maximum);
	
	clumpsGroup.close();
      }

      bool needPartsGroup = ((mask & L_PARTICLE_IDS) != 0) || ((mask & L_PARTICLE_TYPE) != 0);
      if (needPartsGroup) {
	H5::Group partsGroup;
	if (H5Lexists(snapGroup.getId(), "particles", H5P_DEFAULT) > 0) {
	  partsGroup = snapGroup.openGroup("particles");
	} else {
	  partsGroup = snapGroup.createGroup("particles");
	}
	
        writeDatasetIf<int>  (L_PARTICLE_IDS,  mask, partsGroup, "sorted_particle_id", data.particle_ids);
        writeDatasetIf<char> (L_PARTICLE_TYPE, mask, partsGroup, "type",               data.particle_type);
	
	partsGroup.close();
      }

      snapGroup.close();
      file.close();
      return true;
    }
    catch (const H5::Exception& e) {
      std::cerr << "[ClumpIO::writeSnapshot] HDF5 Exception: " << e.getCDetailMsg() << "\n";
      return false;
    }
    catch (...) {
      std::cerr << "[ClumpIO::writeSnapshot] Unknown error\n";
      return false;
    }
  }
} // namespace ClumpIO

float readClumpTime(const std::string& fname, int snapshotIndex){
  uint32_t mask = L_TIME;
  ClumpInfoIO in;    
  bool flag = ClumpIO::readSnapshot(fname, snapshotIndex, mask, in);
  
  if(flag == false){
    return 0.;
  }

  return in.time;
}


void readClumpEvolution(const std::string& fname, int snapshotInit, int snapshotEnd, int dsnapshot, int clumpID_init,
			TrackingVector<float>& times, TrackingVector<ClumpData>& clumps){
  clumps = {};
  times = {};
  
  int snapshot = snapshotInit;
  int clumpID = clumpID_init;
  while(snapshot <= snapshotEnd){
    uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_POSITION | L_CLUMP_DENSITY | L_CLUMP_TEMPERATURE | L_CLUMP_MASS);
    ClumpInfoIO in;    
    bool flag = ClumpIO::readSnapshot(fname, snapshot, mask, in);

    if(flag == false){
      snapshot += dsnapshot;
      continue;
    }

    ClumpData cd;
    bool flag_find_next_clump = false;
    for (size_t i = 0; i < in.clump_id.size(); i++) {
      if(in.clump_id[i] != clumpID)
	continue;

      flag_find_next_clump = true;
      
      cd.clumpID = in.clump_id[i];
      cd.density = in.clump_density[i];
      cd.temperature = in.clump_temperature[i];    
      cd.mass = in.clump_mass[i];
      break;
    }

    if(!flag_find_next_clump)
      break;
    
    times.push_back(in.time);
    clumps.push_back(cd);

    clumpID = cd.clumpID;
    snapshot += dsnapshot;
  }
}

void addNextClumpIDtoHDF5(const TrackingVector<StructureNode *>& nodes,
			  const std::string &filename, int snapshotIndex)
{
  size_t count = 0;
  for (auto& node : nodes) {
    if(node->isLeaf())
      count++;    
  }

  size_t nClumps = count;
  TrackingVector<int> clump_next_id(nClumps);

  for(size_t i=0, count=0;i<nodes.size();i++){
    StructureNode *node = nodes[i];
    if(!node->isLeaf())
      continue;
    
    clump_next_id[count] = node->clumpID_in_next_snapshot;
    count++;
  }
    
  ClumpInfoIO out;
  out.clump_next_id.resize(nClumps);
    
  for(size_t i=0;i<nClumps;i++)
    out.clump_next_id[i] = clump_next_id[i];

  uint32_t mask_out = L_CLUMP_NEXT_ID;
  ClumpIO::writeSnapshot(filename, snapshotIndex, mask_out, out);  
}
#endif
