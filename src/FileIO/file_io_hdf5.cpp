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


template<typename T, int D>
void readHyperslab(H5::DataSet &ds,
                   T *out,        // バッファ (count==1 のときは length=T[1], D==2なら T[3])
                   size_t idx)    // 先頭インデックス
{
  // 1) ファイル空間
  H5::DataSpace fs = ds.getSpace();
  hsize_t dims[D];
  fs.getSimpleExtentDims(dims);

  // 2) hyperslab のオフセット／ブロック
  hsize_t offset[D] = {};
  hsize_t count[D];
  for (int i = 0; i < D; ++i) count[i] = (i==0 ? 1 : dims[i]);
  offset[0] = idx;
  fs.selectHyperslab(H5S_SELECT_SET, count, offset);

  // 3) メモリ空間
  H5::DataSpace ms(D, count);

  // 4) 読み込み
  ds.read(out,
          std::is_same<T,float>::value   ? H5::PredType::NATIVE_FLOAT
        : std::is_same<T,int32_t>::value ? H5::PredType::NATIVE_INT32
        :                                   H5::PredType::NATIVE_DOUBLE,
          ms, fs);
}


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

  readAttributeArray (headerGroup, "NumPart_ThisFile",       hdr.NumPart_ThisFile);
  readAttributeArray (headerGroup, "MassTable",              hdr.massTable);

  npart_ = 0;
  for(int k=0;k<6;k++){
    mass_type[k] = hdr.massTable[k];
    npart_ += hdr.NumPart_ThisFile[k];
  }
  
  // 3) /Parameters グループから追加フラグを読み込む
  try{
    H5::Group paramGroup = file_.openGroup("/Parameters");
    readAttributeScalar(paramGroup, "ComovingIntegrationOn",  hdr.flag_comoving);
  }catch (const H5::Exception &e) {
    printf("Can't fine group /Parameters\n");
    hdr.flag_comoving = 0;
  }

  factor_density_ = hdr.UnitMass_in_g / std::pow(hdr.UnitLength_in_cm, 3) / (1.2 * PROTONMASS) * hdr.HubbleParam * hdr.HubbleParam;
  if (hdr.flag_comoving && hdr.time > 0.)
    factor_density_ *= std::pow(hdr.time, -3);
  
  factor_temperature_ = PROTONMASS / BOLTZMANN * hdr.UnitVelocity_in_cm_per_s * hdr.UnitVelocity_in_cm_per_s;    

  printf("factors=%g %g Unit=%g %g %g\n", factor_density_, factor_temperature_, hdr.UnitLength_in_cm, hdr.UnitMass_in_g, hdr.UnitVelocity_in_cm_per_s);
  
  hdr.flag_hdf5 = true;

  flag_computeTemperature_ = true;
  bool flag_read_internal_energy = false;
  
  size_t globalOff = 0;
  for(int t=0; t<6; ++t) {
    //if (flag_skip_DM && (t == 1 || t == 2)) continue;
      
    std::string gname = "/PartType" + std::to_string(t);
    if (!groupExists(file_, gname))
      continue;

    H5::Group grp = file_.openGroup(gname);
    int64_t cnt = hdr.NumPart_ThisFile[t];
      
    PartGroup pg;
    pg.type  = t;
    pg.count = cnt;

    // 3) トークンごとに FieldSet を作成
    for(size_t i=0; i<fmt_.tokens.size(); ++i) {
      auto &tk = fmt_.tokens[i];
      if (strcmp(tk.label, "dummy") == 0)
	continue;      
      
      int dim = tk.count;

      // ラベル→FieldType
      auto it = labelToField.find(tk.label);
      FieldType fType = (it!=labelToField.end()
			 ? it->second
			 : FieldType::Unknown);

      bool flag_exist = test_dataset(t, fType);
      if(flag_exist == false)
	continue;
      
      // データセットを open
      try{		
	H5::DataSet ds = grp.openDataSet(tk.displayName);
	//H5::PredType dtype = ds.getDataType();
	H5::DataType baseType = ds.getDataType();
	//H5::PredType& dtype = static_cast<H5::PredType&>(baseType);
	hid_t native_tid = H5Tget_native_type(baseType.getId(), H5T_DIR_DEFAULT);

	H5::PredType dtype = H5::PredType::NATIVE_FLOAT;  // デフォルト
	if      (H5Tequal(native_tid, H5T_NATIVE_FLOAT))  dtype = H5::PredType::NATIVE_FLOAT;
	else if (H5Tequal(native_tid, H5T_NATIVE_DOUBLE)) dtype = H5::PredType::NATIVE_DOUBLE;
	else if (H5Tequal(native_tid, H5T_NATIVE_INT))    dtype = H5::PredType::NATIVE_INT;
	else if (H5Tequal(native_tid, H5T_NATIVE_INT32))  dtype = H5::PredType::NATIVE_INT32;
	else if (H5Tequal(native_tid, H5T_NATIVE_UINT))   dtype = H5::PredType::NATIVE_UINT;
	else if (H5Tequal(native_tid, H5T_NATIVE_LLONG))  dtype = H5::PredType::NATIVE_LLONG;
	else if (H5Tequal(native_tid, H5T_NATIVE_ULLONG)) dtype = H5::PredType::NATIVE_ULLONG;

	H5::DataSpace fspace = ds.getSpace();
	int    rank = fspace.getSimpleExtentNdims(); 
	std::vector<hsize_t> dims(rank);
	fspace.getSimpleExtentDims(dims.data());	
	int components = (rank>=2 ? int(dims[1]) : 1);
	
	// 3) その hid_t から PredType オブジェクトを作る	
	PartGroup::FieldSet fs { fType, ds, dtype, components };
	
	fs.filespace = fs.ds.getSpace();
	std::strncpy(fs.name,tk.displayName, sizeof(fs.name)-1);
	
	hsize_t blk[2] = { blockSize_, static_cast<hsize_t>(dim) };
	fs.memspace  = H5::DataSpace(2, blk);
	
	size_t typeSize = fs.dType.getSize();
	fs.rawBuf.resize(blockSize_ * dim * typeSize);
	
	pg.fields.push_back(std::move(fs));
	
	if(t==0 && strcmp(tk.label,"temperature") == 0)
	  flag_computeTemperature_ = false;

	if(t==0 && strcmp(tk.label,"internalenergy") == 0)
	  flag_read_internal_energy = true;

	printf("[%d] label=%s dtype=%d\n", i, tk.label, dtype.getClass());
      }catch (const H5::Exception &e) {
	printf("Type%d dataset%zu label%s name%s not found.\n", t, i, tk.label, tk.displayName);
      }
    }

    parts_.push_back(std::move(pg));
    IndexStart[t] = globalOff;
    globalOff += cnt;
  }

  printf("flag_computeTemperature_=%d\n", flag_computeTemperature_);
  
  if(flag_read_internal_energy == false)
    flag_computeTemperature_ = false;
  
  curIndex_ = 0;
  return true;
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
