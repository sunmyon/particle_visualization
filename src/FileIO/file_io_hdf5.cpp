#ifdef HAVE_HDF5
#include "main.h"
#include "FileIO/file_io.h"
#include "H5Cpp.h"

#include <hdf5.h>    // H5Lexists, H5P_DEFAULT

static bool groupExists(const H5::H5File &file, const std::string &groupPath) {
    // H5Lexists は C API。C++ の file.getId() で hid_t を取ってくる
    herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
    return (status > 0);
}

template<typename T>
bool readAttributeScalar(H5::Group &grp, const std::string &attrName, T &value)
{
    // 属性が存在しなければ false を返す
    if(!grp.attrExists(attrName)) {
      std::cerr << "Attribute '" << attrName << "' not found in /Header.\n";
      return false;
    }
    // 属性を開く
    H5::Attribute attr = grp.openAttribute(attrName);

    // ファイル側の型を取得
    H5::DataType dtype = attr.getDataType();
    // 読み込む (型変換はHDF5が自動で行う)
    attr.read(dtype, &value);

    return true;
};


template<typename T, size_t N>
  bool readAttributeArray(H5::Group &grp, const std::string &attrName, T (&arr)[N])
{
  if(!grp.attrExists(attrName)) {
    std::cerr << "Attribute '" << attrName << "' not found in /Header.\n";
    return false;
  }
  H5::Attribute attr = grp.openAttribute(attrName);
  
  // データスペースをチェック (要素数 N と一致するか確認)
  H5::DataSpace dspace = attr.getSpace();
  hssize_t totalElements = dspace.getSimpleExtentNpoints();
  if(totalElements != N) {
    std::cerr << "Attribute '" << attrName << "' has " << totalElements
	      << " elements, expected " << N << ".\n";
    return false;
  }

  H5::DataType dtype = attr.getDataType();
  attr.read(dtype, arr); // 配列全体を読み込み

  return true;
};

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


bool HDF5ParticleReader::open(const std::string& filename, HeaderInfo& hdr) {
  try {
    // 1) ファイルを開く
    file_ = H5::H5File(filename, H5F_ACC_RDONLY);

    // 2) /Header グループを開き、各属性を読み込む
    H5::Group headerGroup = file_.openGroup("/Header");
    readAttributeScalar(headerGroup, "Time",                   hdr.time);
    readAttributeScalar(headerGroup, "BoxSize",                hdr.boxSize);
    readAttributeScalar(headerGroup, "Omega0",                 hdr.Omega0);
    readAttributeScalar(headerGroup, "OmegaLambda",            hdr.OmegaLambda);
    readAttributeScalar(headerGroup, "HubbleParam",            hdr.HubbleParam);

    readAttributeScalar(headerGroup, "UnitLength_in_cm",       hdr.UnitLength_in_cm);
    readAttributeScalar(headerGroup, "UnitVelocity_in_cm_per_s", hdr.UnitVelocity_in_cm_per_s);
    readAttributeScalar(headerGroup, "UnitMass_in_g",          hdr.UnitMass_in_g);

    readAttributeArray (headerGroup, "NumPart_ThisFile",      hdr.NumPart_ThisFile);
    readAttributeArray (headerGroup, "MassTable",              hdr.massTable);

    // 3) /Parameters グループから追加フラグを読み込む
    H5::Group paramGroup = file_.openGroup("/Parameters");
    readAttributeScalar(paramGroup, "ComovingIntegrationOn",  hdr.flag_comoving);

    hdr.flag_hdf5 = true;

    // 4) 総粒子数を取得して配列を確保
    int64_t totalN = 0;
    readAttributeScalar(headerGroup, "NumParticles", totalN);
    hdr.npart = static_cast<int>(totalN);
    particles_.clear();
    particles_.reserve(hdr.npart);

    // 5) 各 PartType グループをループ処理
    for (int itype = 0; itype < 6; ++itype) {
      // 例として Type1, Type2 はスキップ
      if (itype == 1 || itype == 2) continue;

      // グループ名を組み立て
      char grpName[32];
      std::snprintf(grpName, sizeof(grpName), "/PartType%d", itype);

      // グループが存在しなければスキップ
      if (!groupExists(file_, grpName)) {
        std::cout << grpName << " does not exist. Skipping.\n";
        continue;
      }

      H5::Group grp = file_.openGroup(grpName);

      // 5-1) このタイプの粒子数だけ一時バッファに確保
      int count = hdr.NumPart_ThisFile[itype];
      TrackingVector<ParticleData> tmp(count);
      for (auto &p : tmp) p.type = itype;

      readDatasetHdf5<ParticleData, float, 3>(grp, posName_, tmp,
        [](ParticleData &p, const float xyz[3]) {
          p.pos[0] = xyz[0];  p.pos[1] = xyz[1];  p.pos[2] = xyz[2];
          p.original_pos[0] = xyz[0];  p.original_pos[1] = xyz[1];  p.original_pos[2] = xyz[2];
        },
        H5::PredType::NATIVE_FLOAT
      );

      readDatasetHdf5<ParticleData, float, 3>(grp, velName_, tmp,
        [](ParticleData &p, const float v[3]) {
          p.vel[0] = v[0];  p.vel[1] = v[1];  p.vel[2] = v[2];
        },
        H5::PredType::NATIVE_FLOAT
      );

      readDatasetHdf5<ParticleData, float, 1>(grp, massName_, tmp,
        [](ParticleData &p, const float m[1]) {
          p.mass = m[0];
        },
        H5::PredType::NATIVE_FLOAT
      );

      readDatasetHdf5<ParticleData, int32_t, 1>(grp, idName_, tmp,
        [](ParticleData &p, const int32_t id[1]) {
          p.ID = id[0];
        },
        H5::PredType::NATIVE_INT
      );

      if (itype == 0) {
        readDatasetHdf5<ParticleData, float, 1>(grp, densityName_, tmp,
          [](ParticleData &p, const float d[1]) {
            p.density = d[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, valName_, tmp,
          [](ParticleData &p, const float v[1]) {
            p.val = v[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, val2Name_, tmp,
          [](ParticleData &p, const float v[1]) {
            p.val2 = v[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, elecName_, tmp,
          [](ParticleData &p, const float u[1]) {
            p.temperature = XH + XHe + u[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, h2iName_, tmp,
          [](ParticleData &p, const float u[1]) {
            p.temperature -= u[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, gammaName_, tmp,
          [](ParticleData &p, const float gamma[1]) {
            p.temperature = (gamma[0] - 1.0) / p.temperature;
          },
          H5::PredType::NATIVE_FLOAT
        );

        readDatasetHdf5<ParticleData, float, 1>(grp, ieName_, tmp,
          [](ParticleData &p, const float u[1]) {
            p.temperature *= u[0];
          },
          H5::PredType::NATIVE_FLOAT
        );

        // 物理単位変換
        double fac_d = hdr.UnitMass_in_g /
                       std::pow(hdr.UnitLength_in_cm, 3) /
                       (1.2 * PROTONMASS);
        if (hdr.flag_comoving)
          fac_d *= std::pow(hdr.time, -3) * hdr.HubbleParam * hdr.HubbleParam;

        double fac_t = PROTONMASS / BOLTZMANN *
                       hdr.UnitVelocity_in_cm_per_s *
                       hdr.UnitVelocity_in_cm_per_s;

        for (auto &p : tmp) {
          p.originalHsml = std::pow(p.mass / p.density * 3.0 / (4.0 * M_PI), 1.0 / 3.0);

          p.temperature *= fac_t;
          p.density     *= fac_d;
        }
      }

      // 6) tmp を全体バッファに追加
      particles_.insert(particles_.end(), tmp.begin(), tmp.end());
    }

    // 7) 読み出しインデックス初期化
    curIndex_ = 0;
    return true;
  }
  catch (const H5::Exception &e) {
    std::cerr << "HDF5 open/read error: " << e.getDetailMsg() << "\n";
    return false;
  }
}


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
