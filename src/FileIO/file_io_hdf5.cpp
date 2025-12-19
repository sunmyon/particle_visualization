#ifdef HAVE_HDF5
#include "main.h"
#include "FileIO/file_io.h"
#include "H5Cpp.h"

#include <hdf5.h>    // H5Lexists, H5P_DEFAULT

template<typename T, typename U, size_t NCOMP, typename Setter>
void readDatasetHdf5(H5::Group &group,
                     const std::string &name,
                     TrackingVector<T> &particles,
                     Setter setter,
                     H5::PredType memType){
  // データセットを開く
  H5::DataSet ds = group.openDataSet(name);

  // 次元を取得
  H5::DataSpace dsp = ds.getSpace();

  hsize_t dims[2];
  dsp.getSimpleExtentDims(dims, nullptr);
  // dims[0] = 粒子数N, dims[1] =  NCOMP(のはず)
    
  int ndims = dsp.getSimpleExtentNdims();
  if(ndims == 1) {
    // もし1次元なら NCOMPは1 であることを確認
    if(NCOMP != 1) 
      throw std::runtime_error("Dataset " + name + " is 1D but NCOMP != 1");      
  } else if(ndims == 2) {
    if(dims[1] != NCOMP) 
      throw std::runtime_error("Dataset " + name + " second dimension is not equal to NCOMP");      

    if(dims[0] != particles.size())
      throw std::runtime_error("Dimension mismatch in " + name);	 
  } else {
    throw std::runtime_error("Dataset " + name + " has unsupported rank: " + std::to_string(ndims));
  }
    
  // HDF5から読み出し用のバッファを確保
  TrackingVector<U> buf(dims[0] * NCOMP);
  ds.read(buf.data(), memType);

  // 読み込んだバッファを構造体メンバへコピー
  for(size_t i=0; i<dims[0]; i++){
    U tmp[NCOMP];
    for (size_t j = 0; j < NCOMP; j++) 
      tmp[j] = buf[NCOMP * i + j];
      
    setter(particles[i], tmp);
  }
};


//-----------------------------------------------------------
// readHaloFromHDF5():
//   - snapshot番号を受け取り、"fof_tab_%03d.hdf5" を開く
//   - 指定した halo ID の行を検索し、mass / center / radius などを取得して outData に格納
//   - ファイル構造は実際のHDF5のデータセットに合わせて書き換えてください
//-----------------------------------------------------------
TrackingVector<HaloData> FileInfo::readHaloFromHDF5(char *fbuf, int snapshotNumber)
{
  // 1) ファイル名を組み立て
  char fname[128];
  std::snprintf(fname, sizeof(fname), "%s/fof_tab_%03d.hdf5", fbuf, snapshotNumber);

  try {
    // 2) HDF5ファイルを開く
    H5::H5File file(fname, H5F_ACC_RDONLY);

    char buf[255];
    snprintf(buf, sizeof(buf), "/Group");   

    if(!groupExists(file, buf)){
      std::cout << buf << " does not exist. Skipping.\n";
    }
    
    // ファイルを開く
    // PartType0グループ
    H5::Group grp = file.openGroup(buf);    
      
    //-----------------------------
    // (a) 粒子数 N を決める
    //     例として Coordinates の shape=(N,3) から取得
    //-----------------------------
    // まず、halo 数を決めるため、たとえば "GroupPos" の shape を参照（(N,3) と仮定）
    H5::DataSet posDS = grp.openDataSet("GroupPos");
    H5::DataSpace posSpace = posDS.getSpace();
    hsize_t posDims[2];
    posSpace.getSimpleExtentDims(posDims, nullptr);
    size_t nHalos = posDims[0];
    
    // HaloData 配列を確保
    TrackingVector<HaloData> haloes(nHalos);

    //-----------------------------
    // (b) Coordinates → p[i].pos
    //-----------------------------
    {
      // 先に readVector3 でバッファを読み込み
      // setter(lambda) 内で pos[] にコピー
      readDatasetHdf5<HaloData, float, 3>(
					  grp, "GroupPos", haloes,
					  [](HaloData &p, const float xyz[3]){
					    p.GroupPos[0] = xyz[0];
					    p.GroupPos[1] = xyz[1];
					    p.GroupPos[2] = xyz[2];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }
    
    //-----------------------------
    // (c) Velocities → p[i].vel
    //-----------------------------
    {
      readDatasetHdf5<HaloData, float, 3>(
					  grp, "GroupVel", haloes,
					  [](HaloData &p, const float v[3]){
					    p.GroupVel[0] = v[0];
					    p.GroupVel[1] = v[1];
					    p.GroupVel[2] = v[2];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }

    //-----------------------------
    // (g) Mass → p[i].mass
    //-----------------------------
    {
      readDatasetHdf5<HaloData, float, 1>(
					  grp, "GroupMass", haloes,
					  [](HaloData &p, const float m[1]){
					    p.GroupMass = m[0];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }

    
    //-----------------------------
    // (g) Mass → p[i].mass
    //-----------------------------
    {
      readDatasetHdf5<HaloData, float, 6>(
					  grp, "GroupMassType", haloes,
					  [](HaloData &p, const float m[6]){
					    p.GroupMassType[0] = m[0];
					    p.GroupMassType[1] = m[1];
					    p.GroupMassType[2] = m[2];
					    p.GroupMassType[3] = m[3];
					    p.GroupMassType[4] = m[4];
					    p.GroupMassType[5] = m[5];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }

    //-----------------------------
    // (g) Mass → p[i].mass
    //-----------------------------
    {
      readDatasetHdf5<HaloData, int, 1>(
					  grp, "GroupLen", haloes,
					  [](HaloData &p, const int m[1]){
					    p.GroupLen = m[0];
					  },
					  H5::PredType::NATIVE_INT
					  );	
    }

    
    //-----------------------------
    // (g) Len → p[i].mass
    //-----------------------------
    {
      readDatasetHdf5<HaloData, int, 6>(
					  grp, "GroupLenType", haloes,
					  [](HaloData &p, const int m[6]){
					    p.GroupLenType[0] = m[0];
					    p.GroupLenType[1] = m[1];
					    p.GroupLenType[2] = m[2];
					    p.GroupLenType[3] = m[3];
					    p.GroupLenType[4] = m[4];
					    p.GroupLenType[5] = m[5];
					  },
					  H5::PredType::NATIVE_INT
					  );	
    }

    //-----------------------------
    // (g) Mass → p[i]GroupMetallicity
    //-----------------------------
    {
      readDatasetHdf5<HaloData, float, 1>(
					  grp, "GroupGasMetallicity", haloes,
					  [](HaloData &p, const float m[1]){
					    p.GroupMetallicity[0] = m[0];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }

        //-----------------------------
    // (g) Mass → [i]GroupStellarMetallicity
    //-----------------------------
    {
      readDatasetHdf5<HaloData, float, 1>(
					  grp, "GroupStarMetallicity", haloes,
					  [](HaloData &p, const float m[1]){
					    p.GroupMetallicity[1] = m[0];
					  },
					  H5::PredType::NATIVE_FLOAT
					  );	
    }
    

    file.close();
    return haloes;
  }
  catch(const H5::Exception &err) {
    std::fprintf(stderr, "HDF5 Error in readHaloFromHDF5: %s\n", err.getCDetailMsg());
    return TrackingVector<HaloData>();
  }
}
#endif
