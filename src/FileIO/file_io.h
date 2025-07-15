#pragma once
// POSIX I/O 用
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// mmap + madvise 用
#include <sys/mman.h>
#ifdef HAVE_HDF5
#include "H5Cpp.h"
#endif
#include <fstream>


enum class FileFormat {
  Auto,       // 拡張子から自動判別 (従来の動作)
  HDF5,
  Binary,
  Gadget,
  Framed,      // Fortran‐style framed binary
  _Count
};


class IParticleReader {
public:
  virtual ~IParticleReader() {}
  virtual bool check(const std::string& path, HeaderInfo& header) {
    if (!open(path, header)) {
      return false;
    }

    int iter = 0;
    bool flag_success = true;
    ParticleData p;
    while (readNext(p) && iter < 100) {
      if(p.type < 0 || p.type > 5){
	flag_success = false;
	printf("Why? P[%d].type = %d\n", iter, p.type);
	break;
      }

      iter++;
    }
        
    close();
    return flag_success;
  }
  
  // ファイルを開いてヘッダ情報を返す
  virtual bool  open(const std::string& path, HeaderInfo& header) = 0;
  // 次の粒子を読み込む（false: EOF or error）
  virtual bool  readNext(ParticleData& p) = 0;
  // クローズ  
  virtual void  close() = 0;
};

struct FormatInfo {
  int recordSize    = 0;
  int posOffset     = -1;
  int velOffset     = -1;
  int densityOffset = -1;
  int tempOffset    = -1;
  int valOffset     = -1;
  int val2Offset    = -1;
  int hsmlOffset    = -1;
  int massOffset    = -1;
  int typeOffset    = -1;
  int IDOffset      = -1;

  TrackingVector<struct FormatToken> tokens;
};


struct FormatToken {
  char label[32];  // 例: "position", "dummy", "value", "type"
  char type;       // 'f' (float) または 'i' (int)
  int count;
};


enum class FieldType {
  Position,
  Velocity,
  Hsml,
  Mass,
  Density,
  Temperature,
  Value,
  Value2,
  Type,
  ID,
  Dummy,
  InternalEnergy,
  ElectronFraction,
  H2Fraction,
  Gamma,
  Unknown
};

// 2) ラベル文字列 → FieldType 変換マップ
static const std::unordered_map<std::string,FieldType> labelToField = {
  {"position",         FieldType::Position},
  {"velocity",         FieldType::Velocity},
  {"Hsml",             FieldType::Hsml},
  {"mass",             FieldType::Mass},
  {"density",          FieldType::Density},
  {"temperature",      FieldType::Temperature},
  {"value",            FieldType::Value},
  {"value2",           FieldType::Value2},
  {"type",             FieldType::Type},
  {"ID",               FieldType::ID},
  {"dummy",            FieldType::Dummy},
  {"internalenergy",   FieldType::InternalEnergy},
  {"electronfraction", FieldType::ElectronFraction},
  {"H2fraction",       FieldType::H2Fraction},
  {"Gamma",            FieldType::Gamma}
};


class BinaryParticleReader : public IParticleReader {
  std::ifstream file_;
  FormatInfo    fmt_;
  TrackingVector<char> buf_;
  
public:
  explicit BinaryParticleReader(FormatInfo fmt)
    : fmt_(std::move(fmt)), buf_(fmt.recordSize)  {}

  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if(!file_) return false;

    float t; int n;
    file_.read(reinterpret_cast<char*>(&t), sizeof t);
    file_.read(reinterpret_cast<char*>(&n), sizeof n);
    header.time = t;
    header.npart = n; 
    header.flag_hdf5 = false;

    return true;
  }

  bool readNext(ParticleData& p) override {
    file_.read(buf_.data(), buf_.size());
    if(!file_) return false;

    const char* base = buf_.data();

    readField(base, fmt_.posOffset,     p.pos,           3);
    readField(base, fmt_.posOffset,     p.original_pos,  3);
    readField(base, fmt_.velOffset,     p.vel,           3);
    readField(base, fmt_.densityOffset, &p.density,      1);
    readField(base, fmt_.tempOffset,    &p.temperature,  1);
    readField(base, fmt_.valOffset,     &p.val,          1);
    readField(base, fmt_.val2Offset,    &p.val2,         1);
    readField(base, fmt_.hsmlOffset,    &p.Hsml,         1);
    readField(base, fmt_.hsmlOffset,    &p.originalHsml, 1);
    readField(base, fmt_.massOffset,    &p.mass,         1);
    readField(base, fmt_.typeOffset,    &p.type,         1);
    readField(base, fmt_.IDOffset,      &p.ID,           1);
        
    return true;
  }

  void close() override {
    if (file_.is_open()) file_.close();
  }

private:
  template<typename T>
  inline void readField(const char* base, int offset, T* dest, size_t count)
  {
    if (offset >= 0){ 
      std::memcpy(dest, base + offset, count * sizeof(T));
    }else {
      for (size_t i = 0; i < count; ++i) 
	dest[i] = T{};      
    }
  }
};

class MMapParticleReader : public IParticleReader {
  int           fd_          = -1;
  char*         data_        = nullptr;
  size_t        mappingSize_ = 0;
  const char*   cur_         = nullptr;
  const char*   end_         = nullptr;
  FormatInfo    fmt_;

public:
  explicit MMapParticleReader(const FormatInfo& fmt)
    : fmt_(fmt) {}

  // ───────────────────────────────────────────────────────────────────
  inline bool open(const std::string& path, HeaderInfo& header) override {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) { perror("open"); return false; }
    struct stat sb;
    if (fstat(fd_, &sb) == -1) { perror("fstat"); ::close(fd_); return false; }

    float t; int n;
    if (::read(fd_, &t, sizeof t) != sizeof t ||
	::read(fd_, &n, sizeof n) != sizeof n) {
      std::cerr << "Header read error\n";
      ::close(fd_);
      return false;
    }
    header.time      = t;
    header.flag_hdf5 = false;
    header.npart     = n;

    // 3) mmap
    size_t headerSize   = sizeof(float) + sizeof(int);
    mappingSize_        = headerSize + fmt_.recordSize * size_t(n);
    
    data_ = static_cast<char*>(mmap(nullptr, mappingSize_, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (data_ == MAP_FAILED) {
      perror("mmap");
      ::close(fd_);
      return false;
    }
    cur_ = data_ + headerSize;
    end_ = data_ + mappingSize_;
    madvise(data_, mappingSize_, MADV_SEQUENTIAL | MADV_WILLNEED);

    return true;
  }

  // ───────────────────────────────────────────────────────────────────
  inline bool readNext(ParticleData& p) override {
    if (cur_ + fmt_.recordSize > end_) return false;

    const char* base = cur_;

    readField(base, fmt_.posOffset,     p.pos,           3);
    readField(base, fmt_.posOffset,     p.original_pos,  3);
    readField(base, fmt_.velOffset,     p.vel,           3);
    readField(base, fmt_.densityOffset, &p.density,      1);
    readField(base, fmt_.tempOffset,    &p.temperature,  1);
    readField(base, fmt_.valOffset,     &p.val,          1);
    readField(base, fmt_.val2Offset,    &p.val2,         1);
    readField(base, fmt_.hsmlOffset,    &p.Hsml,         1);
    readField(base, fmt_.hsmlOffset,    &p.originalHsml, 1);
    readField(base, fmt_.massOffset,    &p.mass,         1);
    readField(base, fmt_.typeOffset,    &p.type,         1);
    readField(base, fmt_.IDOffset,      &p.ID,           1);

    cur_ += fmt_.recordSize;
    return true;
  }

  // ───────────────────────────────────────────────────────────────────
  inline void close() override {
    if (data_) {
      madvise(data_, mappingSize_, MADV_DONTNEED);
      munmap(data_, mappingSize_);
      data_ = nullptr;
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

private:
  template<typename T>
  inline void readField(const char* base, int offset, T* dest, size_t count) {
    if (offset >= 0) {
      std::memcpy(dest, base + offset, count * sizeof(T));
    } else {
      for (size_t i = 0; i < count; ++i)
	dest[i] = T{};
    }
  }
};



class BlockwiseParticleReader : public IParticleReader {
  enum class DataType : uint8_t {
    Float = 0,
    Int32 = 1,
    Double = 2
  };

  struct StreamInfo {
    std::ifstream  stm;
    DataType dataType;
    FieldType fieldType;
    size_t size_item;
    size_t componentCount;
    bool flag_read[6];
  };
  
  struct header_Gadget{
    int npart[6];                        /*!< number of particles of each type in this file */
    double mass[6];                      /*!< mass of particles of each type. If 0, then the masses are explicitly */
    double time;                         /*!< time of snapshot file */
    double redshift;                     /*!< redshift of snapshot file */
    int flag_sfr;                        /*!< flags whether the simulation was including star formation */
    int flag_feedback;                   /*!< flags whether feedback was included (obsolete) */
    unsigned int npartTotal[6];          /*!< total number of particles of each type in this snapshot. This can bedifferent from npart if one is dealing with a multi-file snapshot. */
    int flag_cooling;                    /*!< flags whether cooling was included  */
    int num_files;                       /*!< number of files in multi-file snapshot */
    double BoxSize;                      /*!< box-size of simulation in case periodic boundaries were used */
    double Omega0;                       /*!< matter density in units of critical density */
    double OmegaLambda;                  /*!< cosmological constant parameter */
    double HubbleParam;                  /*!< Hubble parameter in units of 100 km/sec/Mpc */
    int flag_stellarage;                 /*!< flags whether the file contains formation times of star particles */
    int flag_metals;                     /*!< flags whether the file contains metallicity values for gas and star particles */
    unsigned int npartTotalHighWord[6];  /*!< High word of the total number of particles of each type */
    int  flag_entropy_instead_u;         /*!< flags that IC-file contains entropy instead of u */
    int  flag_splv;                      /*!< flags whether the simulation was including star formation */
    int  flag_doulbe;                    /*!< flags whether the simulation was including star formation */
    char fill[52];                       /*!< fills to 256 Bytes */
  } header_gadget;

  std::ifstream file_;
  std::vector<StreamInfo>  streams_;
  FormatInfo               fmt_;
  size_t                   npart_, curIdx_ = 0;
  size_t                   npart_mass_read_;
  
  bool                     flag_computeTemperature_;
  bool                     flag_mass_read[6];
  
  std::vector<size_t>      fieldOffset_;
  std::vector<size_t>      fieldStride_;
  
public:
  BlockwiseParticleReader(FormatInfo fmt) : fmt_(std::move(fmt))
  {  }

  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if (!file_) return false;

    fieldOffset_.resize(fmt_.tokens.size());
    fieldStride_.resize(fmt_.tokens.size());
    
    int32_t rec1, rec2;
    file_.read(reinterpret_cast<char*>(&rec1), sizeof(rec1));
    file_.read(reinterpret_cast<char*>(&header_gadget), sizeof(header_gadget));
    file_.read(reinterpret_cast<char*>(&rec2), sizeof(rec2));

    if (!file_ || rec1 != rec2) return false;
    
    npart_ = 0;    
    npart_mass_read_ = 0;    
    for(int t=0; t<6; ++t){
      npart_ += header_gadget.npart[t];
      flag_mass_read[t] = false;
      if(header_gadget.npart[t] > 0 && header_gadget.mass[t] == 0.){
	npart_mass_read_ += header_gadget.npart[t];
	flag_mass_read[t] = true;
      }

      printf("header.mass[%d]=%g npart=%d boxsize=%g\n", t, header_gadget.mass[t], header_gadget.npart[t], header_gadget.BoxSize);
    }    
    
    // ヘッダ部の終わり位置を記録
    size_t headerEnd = static_cast<size_t>(file_.tellg());

    size_t pos = headerEnd;
    for (size_t i = 0; i < fmt_.tokens.size(); ++i) {
      file_.clear();
      file_.seekg(pos, std::ios::beg);

      if(npart_mass_read_ == 0 && std::strcmp(fmt_.tokens[i].label, "mass") == 0)
	continue;
      
      int32_t blkBytes = 0;
      file_.read(reinterpret_cast<char*>(&blkBytes), sizeof(blkBytes));
      
      size_t dummy1 = blkBytes;
      size_t dataStart = static_cast<size_t>(file_.tellg());
      fieldOffset_[i] = dataStart;

      size_t size_item;
      if(fmt_.tokens[i].type == 'f')
	size_item = sizeof(float);
      if(fmt_.tokens[i].type == 'i')
	size_item = sizeof(int);
      if(fmt_.tokens[i].type == 'd')
	size_item = sizeof(double);
      
      size_t bytePer = fmt_.tokens[i].count * size_item;
      fieldStride_[i] = bytePer;

      auto it = labelToField.find(fmt_.tokens[i].label);
      FieldType fType = (it != labelToField.end()
			 ? it->second
			 : FieldType::Unknown);

      bool flag_read[6];
      getReadBlock(flag_read, fType);
      
      int np = 0;
      for(int k=0;k<6;k++){
	if(flag_read[k])
	  np += header_gadget.npart[k];
      }

      size_t pos_dummy2 = dataStart + bytePer * np;
      file_.seekg(pos_dummy2, std::ios::beg);
      int32_t dummy2;
      file_.read(reinterpret_cast<char*>(&dummy2), sizeof(dummy2));

      if(dummy1 != dummy2){
	printf("incorrect dummy size has been detected!\n");
      }	
      
      size_t size_record = sizeof(blkBytes) + bytePer * np;      
      pos = dataStart + size_record;      
    }

    // 各フィールドのブロック先頭バイト offset が fmt_.fieldOffsets[] に
    bool flag_temperature = false;
    for (size_t i = 0; i < fmt_.tokens.size(); ++i) {
      const auto &tk = fmt_.tokens[i];
      if(std::strcmp(tk.label,"dummy") == 0)
	continue;

      if(npart_mass_read_ == 0 && std::strcmp(tk.label,"mass") == 0){
	streams_.emplace_back();
	auto &si = streams_.back();
	si.fieldType = FieldType::Mass;
	for(int k=0;k<6;k++)
	  si.flag_read[k] = false;
	
	continue;
      }
	
      if(std::strcmp(tk.label, "temperature") == 0)
	flag_temperature = true;
	
      // 1) まず文字列ラベルから FieldType を取得
      auto it = labelToField.find(tk.label);
      FieldType fType = (it != labelToField.end()
			 ? it->second
			 : FieldType::Unknown);
      
      DataType dtype;
      size_t size_item;
      if(tk.type == 'f'){
	dtype = DataType::Float;
	size_item = sizeof(float);
      }if(tk.type == 'i'){
	dtype = DataType::Int32;
	size_item = sizeof(int);
      }if(tk.type == 'd'){
	dtype = DataType::Double;
	size_item = sizeof(double);
      }
      
      streams_.emplace_back();
      auto &si = streams_.back();
      si.stm.open(path, std::ios::binary);
      si.stm.seekg(fieldOffset_[i], std::ios::beg);

      si.dataType = dtype;
      si.fieldType = fType;
      si.size_item = size_item;
      si.componentCount = tk.count;

      bool flag_read[6];
      getReadBlock(flag_read, fType);
      for(int k=0;k<6;k++)
	si.flag_read[k] = flag_read[k];
    }

    flag_computeTemperature_ = flag_temperature;
    
    // 準備完了
    header.flag_hdf5 = false;
    return true;
  }

  bool readNext(ParticleData& p) override {
    if (curIdx_ >= npart_) return false;

    int ptype = 0;
    for(int np=0, k=0;k<6;k++){
      np += header_gadget.npart[k];
      if(curIdx_ < np){
	ptype = k;
	break;
      }
    }

    p.type = ptype;
    
    double electronFrac = -1., H2Frac = -1., gammaVal = -1., intEnergy = -1.;
    for (size_t i = 0; i < streams_.size(); ++i) {
      auto &si      = streams_[i];

      if(si.fieldType == FieldType::Mass){
	if(flag_mass_read[ptype] == false)
	  p.mass = header_gadget.mass[ptype];
      }	

      if(si.flag_read[ptype]){
	size_t comps  = si.componentCount;
	size_t bytes  = comps * si.size_item;
      
	std::vector<uint8_t> raw(bytes);      
	si.stm.read(reinterpret_cast<char*>(raw.data()), bytes);
	if (!si.stm) return false;
      
           
	switch (si.dataType) {
	case DataType::Float: { // float
	  auto *vals = reinterpret_cast<float*>(raw.data());
	  switch (si.fieldType) {
	  case FieldType::ElectronFraction:
	    electronFrac = vals[0];
	    break;
	  case FieldType::H2Fraction:
	    H2Frac = vals[0];
	    break;
	  case FieldType::Gamma:
	    gammaVal = vals[0];
	    break;
	  case FieldType::InternalEnergy:
	    intEnergy = vals[0];
	    break;
	  default:
	    assignField<float>(p, si.fieldType, vals, comps);
	    break;
	  }
	  break;
	}
	case DataType::Int32: { // int32_t
	  auto *ivals = reinterpret_cast<int32_t*>(raw.data());
	  assignField<int>(p, si.fieldType, ivals, comps);
	  break;
	}
	case DataType::Double: { // double
	  auto *dvals = reinterpret_cast<double*>(raw.data());
	  switch (si.fieldType) {
	  case FieldType::ElectronFraction:
	    electronFrac = dvals[0];
	    break;
	  case FieldType::H2Fraction:
	    H2Frac = dvals[0];
	    break;
	  case FieldType::Gamma:
	    gammaVal = dvals[0];
	    break;
	  case FieldType::InternalEnergy:
	    intEnergy = dvals[0];
	    break;
	  default:
	    assignField<double>(p, si.fieldType, dvals, comps);
	    break;
	  }
	}
	default:
	  break;
	
	}
      }
    }
    
    // 5) 温度の後処理（必要なら）
    if (flag_computeTemperature_) 
      p.temperature = computeTemperature_(electronFrac, H2Frac, gammaVal, intEnergy);

    if(curIdx_% 1000 == 0)
      printf("%zu mass=%g type=%d pos=%g %g %g vel=%g %g %g ID=%d header.npart=%d %d mass=%g %g\n"
	     , curIdx_, p.mass, p.type, p.pos[0], p.pos[1], p.pos[2], p.vel[0], p.vel[1], p.vel[2], p.ID
	     , header_gadget.npart[0], header_gadget.npart[1], header_gadget.mass[0], header_gadget.mass[1]);
    
    ++curIdx_;
    return true;
  }

  void close() override {
    for (auto &si : streams_){
      if (si.stm.is_open())
	si.stm.close();
    }
  }

private:
double computeTemperature_(double f_elec, double f_H2, double gamma, double Eint){
  float denom = 1.2;
  if(f_elec > 0.)
    denom += f_elec;

  if(f_H2 > 0.)
    denom -= f_H2;

  if(gamma < 0.)
    gamma = 5./3.;
  
  return (gamma - 1.0f) * Eint / denom;
}

template<typename T>
void assignField(ParticleData &p,
                 FieldType    fType,
                 const T     *vals,
                 size_t       comps)
{
  switch (fType) {
  case FieldType::Position:
    for (int i = 0; i < 3; ++i) {
      p.pos[i]          = static_cast<float>(vals[i]);
      p.original_pos[i] = p.pos[i];
    }
    break;

  case FieldType::Velocity:
    for (int i = 0; i < 3; ++i)
      p.vel[i] = static_cast<float>(vals[i]);
    break;

  case FieldType::Hsml:
    p.Hsml = p.originalHsml = static_cast<float>(vals[0]);
    break;

  case FieldType::Mass:
    p.mass = static_cast<float>(vals[0]);
    break;

  case FieldType::Density:
    p.density = static_cast<float>(vals[0]);
    break;

  case FieldType::Value:
    p.val = static_cast<float>(vals[0]);
    break;

  case FieldType::Value2:
    p.val2 = static_cast<float>(vals[0]);
    break;

  case FieldType::Temperature:
    p.temperature = static_cast<float>(vals[0]);
    break;

  case FieldType::ID:
    if constexpr (std::is_integral_v<T>) {
      p.ID = static_cast<int>(vals[0]);
    } else {
      p.ID = static_cast<int>(std::lround(vals[0]));
    }
    break;
  default:
    break;
  }
}


void getReadBlock(bool *flag, FieldType fType)
{
  /** Minimal setting for Gadget format **/
  for (int i = 0; i < 6; ++i) 
    flag[i] = 0;    

  switch (fType) {
  case FieldType::Position:
    for (int i = 0; i < 6; ++i) 
      flag[i] = 1;    
    break;
    
  case FieldType::Velocity:
    for (int i = 0; i < 6; ++i)
      flag[i] = 1;
    break;
    
  case FieldType::Mass:
    for (int i = 0; i < 6; ++i)
      flag[i] = flag_mass_read[i];
    break;
    
  case FieldType::ID:
    for (int i = 0; i < 6; ++i)
      flag[i] = 1;
    break;
  default:
    flag[0] = 1;
    break;
  }
}
};

struct particle_data_read {
  double time;
  double Pos[3];
  double r;
  double Mass;
};

class FramedBinaryParticleReader : public IParticleReader {
  FILE*          fp_    = nullptr;
  size_t         npart_ = 0;
  size_t         read_  = 0;
public:
  FramedBinaryParticleReader() = default;

  bool open(const std::string &path, HeaderInfo &hdr) override {
    fp_ = std::fopen(path.c_str(), "rb");
    if (!fp_) {
      std::perror("fopen");
      return false;
    }

    int n;
    float dummy;
    if (std::fread(&dummy, sizeof(dummy), 1, fp_) != 1) return false;
    if (std::fread(&n, sizeof(n), 1, fp_) != 1) return false;
    if (std::fread(&dummy, sizeof(dummy), 1, fp_) != 1) return false;

    printf("n=%d data aray...\n", n);
    
    hdr.time      = 0.;
    hdr.flag_hdf5 = false;
    hdr.npart     = 0;
    npart_        = static_cast<size_t>(n);
    read_         = 0;
    return true;
  }

  bool readNext(ParticleData &p) override {
    if (read_ >= npart_) return false;

    float dummy;
    if (std::fread(&dummy, sizeof(dummy), 1, fp_) != 1) {      
      close();
      return false;
    }
    
    particle_data_read raw;
    if (std::fread(&raw, sizeof(raw), 1, fp_) != 1) return false;
    if (std::fread(&dummy, sizeof(dummy), 1, fp_) != 1) return false;

    // copy into your ParticleData
    for (int k = 0; k < 3; ++k) {
      p.pos[k]          = static_cast<float>(raw.Pos[k]);
      p.original_pos[k] = p.pos[k];
      p.vel[k]          = 0.;
    }
    p.mass             = static_cast<float>(raw.Mass);
    p.type             = 0;
    p.ID               = 0;

    ++read_;
    return true;
  }

  void close() override {
    if (fp_) {
      std::fclose(fp_);
      fp_ = nullptr;
    }
  }
};


#ifdef HAVE_HDF5
class HDF5ParticleReader : public IParticleReader {
  H5::H5File                     file_;
  TrackingVector<ParticleData>   particles_;
  size_t                         curIndex_ = 0;

  // デフォルトのデータセット名
  std::string posName_, velName_, massName_, idName_;
  std::string densityName_, tempName_, valName_, val2Name_;
  std::string elecName_, h2iName_, gammaName_, ieName_;
  
public:
    HDF5ParticleReader(
    const std::string& _posName,
    const std::string& _velName,
    const std::string& _massName,
    const std::string& _idName,
    const std::string& _densityName,
    const std::string& _tempName,
    const std::string& _valName,
    const std::string& _val2Name,
    const std::string& _elecName,
    const std::string& _h2iName,
    const std::string& _gammaName,
    const std::string& _ieName
  )
    : posName_(_posName)
    , velName_(_velName)
    , massName_(_massName)
    , idName_(_idName)
    , densityName_(_densityName)
    , tempName_(_tempName)
    , valName_(_valName)
    , val2Name_(_val2Name)
    , elecName_(_elecName)
    , h2iName_(_h2iName)
    , gammaName_(_gammaName)
    , ieName_(_ieName)
  {}

  bool open(const std::string &path, HeaderInfo &header) override;

  bool readNext(ParticleData &p) override {
    if (curIndex_ >= particles_.size()) return false;
    p = particles_[curIndex_++];
    return true;
  }

  void close() override {
    particles_.clear();
    curIndex_ = 0;
    file_.close();
  }
};
#endif


// ------------------------------
// 連番ファイル読み込み用グローバル変数
// ------------------------------
class FileInfo{
public:
  int initialIndex = 0;
  int currentFileIndex;
  int batchSize = 1;
  int skipStep = 1;
  int currentStep = 0;      // **現在のステップ（n）**
  bool isLoading = false;   // **ロード中かどうか**
  
  char fileFormat[255] = "output_%04d.dat"; // 例: "output_%04d.dat"
  char folderPath[255] = "./example/";              // 末尾に "/" を付加する
  char filePath[512] = "./example/output_0000.dat";              // 末尾に "/" を付加する

  int currentBatchStart = initialIndex;  // 現在のバッチの開始ファイル番号

  // ------------------------------
  // データフォーマット編集用ダイアログ用変数
  // ------------------------------

#ifdef HAVE_HDF5
  bool useHDF5 = false;
#endif
  TrackingVector<FormatToken> formatTokens;

  void setFormatMode(FileFormat form){
    readFileFormat = form;
  };

  int getFormatMode(void){
    return static_cast<int>(readFileFormat);
  };
  
private:
  CameraContext& camCtx;
  FileFormat readFileFormat = FileFormat::Auto;
  
  // バッチ管理用変数
  TrackingVector<TrackingVector<ParticleData>> batchParticles; // バッチ内の各ファイルの粒子データ
  TrackingVector<HeaderInfo> headerBatch; // バッチ内の各ファイルの粒子データ

#ifdef HAVE_HDF5
  char candidatePosNames[256]="Coordinates";
  char candidateVelNames[256]="Velocities";
  char candidateMassNames[256]="Masses";
  char candidateIDNames[256]="ParticleIDs";
  char candidateDensityNames[256]="Density";
  char candidateTemperatureNames[256];
  char candidateElecNames[256]="ElectronAbundance";
  char candidateH2INames[256]="H2IAbundance";
  char candidateGammaNames[256]="Gamma";
  char candidateInternalEnergyNames[256]="InternalEnergy";
  char candidateValNames[256]="Metallicity";
  char candidateVal2Names[256]="ElectronAbundance";

  bool showHDF5MappingDialog = false;
#endif
  
  bool showFormatDialog = false;
  TrackingVector<FormatToken> formatTokensEdit; // 編集用一時コピー
  
  // 更新タイミングでのみ使うmutex
  std::mutex g_dataMutex;
    
  void syncLoadFirstFile(int targetFile, ParticleArray *P);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep);

  bool loadSingleFile(int fileNumber, TrackingVector<ParticleData>& particles, HeaderInfo &header_return);

#ifdef HAVE_HDF5
  TrackingVector<ParticleData> loadParticlesFromHDF5(const std::string& filename, HeaderInfo& hdr);
#endif
  
  void initDefaultFormatTokens();
  bool computeFormatInfo(const TrackingVector<FormatToken>& tokens, FormatInfo &info);  
  void load_particle_from_buffer(const char* base, int npart, int recordSize, FormatInfo &info, TrackingVector<ParticleData>& particles);
    
public:
  FileInfo(CameraContext& cam):
    camCtx(cam)
  {
    initDefaultFormatTokens();
  }

  
  void loadNewSnapshot(int newindex, ParticleArray* P);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P);

#ifdef HAVE_HDF5
  void ShowHDF5FieldMappingDialog();
  void showHDF5Dialog(void){
    showHDF5MappingDialog = true;
  };
#endif
  
  void DrawFormatDialog();
  void showDialog(void){
    showFormatDialog = true;
    formatTokensEdit = formatTokens;
  };

#ifdef HAVE_HDF5
  TrackingVector<HaloData> readHaloFromHDF5(char *fbuf, int snapshotNumber);
  
  bool groupExists(const H5::H5File &file, const std::string &groupPath)
  {
    // file.getId() でC APIの hid_t が得られるので、H5Lexists などを呼べる
    herr_t status = H5Lexists(file.getId(), groupPath.c_str(), H5P_DEFAULT);
    // status > 0 なら存在、0 なら存在しない、 <0 ならエラー
    return (status > 0);
  };
#endif

  TrackingVector<int> getStarParticleID(int indexFile);
};

extern FileInfo *gFileInfo;
