#pragma once
// POSIX I/O 用
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unordered_map>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

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

enum class DataType : uint8_t {
  Float = 0,
  Int32 = 1,
  Int64 = 2,
  Double = 3
};

constexpr std::size_t dataTypeSize(DataType dt) noexcept {
  switch (dt) {
    case DataType::Float:  return sizeof(float);
    case DataType::Int32:  return sizeof(int32_t);
    case DataType::Int64:  return sizeof(int64_t);
    case DataType::Double: return sizeof(double);
  }

  return 0;
}

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
  
  bool flag_header = true;
  size_t size_header = sizeof(int) + sizeof(float);
  
  TrackingVector<struct FormatToken> tokens;
};


struct FormatToken {
  char label[32];
  DataType type;
  char displayName[32] = {};  // C++11 以降で全要素を '\0' で初期化
  int  count;
  
  // デフォルト／引数付きコンストラクタ
  FormatToken(const char* l = "dummy", DataType t = DataType::Float, int c = 1)
    : type(t), count(c)
  {
    std::strncpy(label, l, sizeof(label));
    label[sizeof(label)-1] = '\0';
    // displayName は in-class 初期化で既にゼロクリア済み
  }

  // C++17 の inline static を使ってヘッダ内で定義＋初期化
  inline static const char* candidatePosNames         = "Coordinates";
  inline static const char* candidateVelNames         = "Velocities";
  inline static const char* candidateMassNames        = "Masses";
  inline static const char* candidateIDNames          = "ParticleIDs";
  inline static const char* candidateDensityNames     = "Density";
  inline static const char* candidateTemperatureNames = "Temperature";
  inline static const char* candidateElecNames        = "ElectronAbundance";
  inline static const char* candidateH2INames         = "H2IAbundance";
  inline static const char* candidateGammaNames       = "Gamma";
  inline static const char* candidateInternalEnergyNames = "InternalEnergy";
  inline static const char* candidateValNames         = "Metallicity";
  inline static const char* candidateVal2Names        = "ElectronAbundance";

  static void SetDefaultDisplayName(FormatToken &tok) {
    if      (strcmp(tok.label, "position")    == 0) strcpy(tok.displayName, candidatePosNames);
    else if (strcmp(tok.label, "velocity")    == 0) strcpy(tok.displayName, candidateVelNames);
    else if (strcmp(tok.label, "mass")        == 0) strcpy(tok.displayName, candidateMassNames);
    else if (strcmp(tok.label, "ID")          == 0) strcpy(tok.displayName, candidateIDNames);
    else if (strcmp(tok.label, "density")     == 0) strcpy(tok.displayName, candidateDensityNames);
    else if (strcmp(tok.label, "temperature") == 0) strcpy(tok.displayName, candidateTemperatureNames);
    else if (strcmp(tok.label, "H2fraction") == 0) strcpy(tok.displayName, candidateH2INames);    
    else if (strcmp(tok.label, "electronfraction") == 0) strcpy(tok.displayName, candidateElecNames);
    else if (strcmp(tok.label, "Gamma") == 0) strcpy(tok.displayName, candidateGammaNames);    
    else if (strcmp(tok.label, "internalenergy") == 0) strcpy(tok.displayName, candidateInternalEnergyNames);
    else if (strcmp(tok.label, "value")       == 0) strcpy(tok.displayName, candidateValNames);
    else if (strcmp(tok.label, "value2")      == 0) strcpy(tok.displayName, candidateVal2Names);
    else {
      // デフォルトは label のまま
      std::strncpy(tok.displayName, tok.label, sizeof(tok.displayName));
      tok.displayName[sizeof(tok.displayName)-1] = '\0';
    }
  }

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
  size_t curIdx_ = 0;
  
public:
  explicit BinaryParticleReader(FormatInfo fmt)
    : fmt_(std::move(fmt)), buf_(fmt.recordSize)  {}

  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if(!file_) return false;

    if(fmt_.flag_header){
      float t; int n;
      file_.read(reinterpret_cast<char*>(&t), sizeof t);
      file_.read(reinterpret_cast<char*>(&n), sizeof n);
      header.time = t;
      header.npart = n;
    }else{
      header.time = 0.;
    }
    
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

    if(fmt_.typeOffset == -1)
      p.type = 0;

    ++curIdx_;
    
    return true;
  }

  void close() override {
    if (file_.is_open())
      file_.close();
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

#ifdef USE_MMAP
#include <sys/mman.h>
class MMapParticleReader : public IParticleReader {
  int           fd_          = -1;
  char*         data_        = nullptr;
  size_t        mappingSize_ = 0;
  size_t        curIdx_      = 0;
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

    //you always need header in this format
    float t; int n;
    if (::read(fd_, &t, sizeof t) != sizeof t ||
	::read(fd_, &n, sizeof n) != sizeof n) {
      std::cerr << "Header read error\n";
      ::close(fd_);
      return false;
    }
    header.time      = t;
    header.npart     = n;
    
    header.flag_hdf5 = false;
    
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

    if(fmt_.typeOffset == -1)
      p.type = 0;
    
    cur_ += fmt_.recordSize;
    ++curIdx_;
    
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
#endif


class BlockwiseParticleReader : public IParticleReader {
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

      size_t size_item = dataTypeSize(fmt_.tokens[i].type);      
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
	printf("%s: incorrect dummy size has been detected!\n"
	       , fmt_.tokens[i].label);
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
      
      DataType dtype = tk.type;
      size_t size_item = dataTypeSize(dtype);
      
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
  size_t                         blockSize_ = 1024;
  size_t                         blockLoadedIdx_ = 0;
  size_t                         currentBlockCount_ = 0;
  int                            npart_ = 0;
  FormatInfo                     fmt_;
  
  bool flag_skip_DM = false;
  bool flag_computeTemperature_ = false;

  double factor_temperature_;
  double factor_density_;
  double mass_type[6];
  size_t IndexStart[6];
  
  struct PartGroup {
    int                       type;       // PartType の番号
    int64_t                   count;      // 粒子数
    struct FieldSet {
      FieldType               fType;      // ParticleData のどのメンバに対応するか
      H5::DataSet             ds;         // データセットハンドル
      H5::PredType            dType;      // ネイティブ型
      int                     dim;        // 1 or 2 (1→スカラ, 2→ベクトル)
      H5::DataSpace filespace;     // open() で取得・保持
      H5::DataSpace memspace;      // open() で作成 (2D: {blockSize, dim})
      std::vector<char> rawBuf;    // blockSize×dim×sizeof(type)
      char name[255];
    };
    std::vector<FieldSet>     fields;     // この PartType に含まれるすべてのトークン
  };

  std::vector<PartGroup> parts_;
  
public:
  HDF5ParticleReader(FormatInfo fmt) : fmt_(std::move(fmt))
  {}
  
  bool open(const std::string &path, HeaderInfo &header) override;

  bool readNext(ParticleData &p) override{
    if (curIndex_ >= npart_) return false;
    
    // 4) curIndex_ → (groupIndex, localIdx)
    size_t ptype = 0;
    while(ptype + 1 < 6 && curIndex_ >= IndexStart[ptype+1])
      ++ptype;
    size_t localIdx = curIndex_ - IndexStart[ptype];
    auto &pg = parts_[ptype];

    p.type = ptype;
    if(mass_type[ptype] > 0.)
      p.mass = mass_type[ptype];	  

    if(curIndex_%10000 == 0 || ptype >= 3)
      printf("i=%zu type=%zu npart_=%d\n", curIndex_, ptype, npart_);
        
    if (localIdx < blockLoadedIdx_ ||
	localIdx >= blockLoadedIdx_ + currentBlockCount_) {
      loadBlock(ptype);
    }
        
    double electronFrac = -1., H2Frac = -1., gammaVal = -1., intEnergy = -1.;
    for(auto &fs : pg.fields) {
      size_t dim = fs.dim;
      size_t elemIdx = (localIdx - blockLoadedIdx_) * dim;      // 要素番号
      size_t typeSize = fs.dType.getSize();

      char *base = fs.rawBuf.data() + elemIdx * typeSize;
      
      // 4) 型ごとに一時バッファを用意して読み込み

      if(curIndex_ < 10 || ptype>=3)
	printf("ftype=%d\n", fs.fType);
      
      if(fs.dType == H5::PredType::NATIVE_FLOAT){
	float* buf = reinterpret_cast<float*>(base);

	if(curIndex_<10 && fs.fType == FieldType::Velocity)
	  printf("velocity is float!\n");
	
	switch (fs.fType) {
	case FieldType::ElectronFraction:
	  electronFrac = buf[0];
	  break;
	case FieldType::H2Fraction:
	  H2Frac = buf[0];
	  break;
	case FieldType::Gamma:
	  gammaVal = buf[0];
	  break;
	case FieldType::InternalEnergy:
	  intEnergy = buf[0];
	  break;
	default:
	  assignField<float>(p, fs.fType, buf, fs.dim);
	  if(curIndex_<10 && fs.fType == FieldType::Velocity)
	    printf("Vel=%g %g %g buf=%g %g %g\n", p.vel[0], p.vel[1], p.vel[2], buf[0], buf[1], buf[2]);

	  if(ptype>=5 && fs.fType == FieldType::Mass)
	    printf("Mass=%g buf=%g\n", p.mass, buf[0]);
	  
	  break;
	}
      }else if(fs.dType == H5::PredType::NATIVE_INT32 || fs.dType == H5::PredType::NATIVE_UINT){
	int* buf = reinterpret_cast<int*>(base);
	assignField<int32_t>(p, fs.fType, buf, fs.dim);
      }else if(fs.dType == H5::PredType::NATIVE_LLONG || fs.dType == H5::PredType::NATIVE_ULLONG){
	long long* buf = reinterpret_cast<long long*>(base);
	std::vector<int> tmp(fs.dim);
	for (int i = 0; i < fs.dim; ++i)
	  tmp[i] = static_cast<int>(buf[i]);
	assignField<int32_t>(p, fs.fType, tmp.data(), fs.dim);
      }else if(fs.dType == H5::PredType::NATIVE_DOUBLE){
	double* buf = reinterpret_cast<double*>(base);

	if(curIndex_<10 && fs.fType == FieldType::Velocity)
	  printf("velocity is double!\n");
	
	switch (fs.fType) {
	case FieldType::ElectronFraction:
	  electronFrac = buf[0];
	  break;
	case FieldType::H2Fraction:
	  H2Frac = buf[0];
	  break;
	case FieldType::Gamma:
	  gammaVal = buf[0];
	  break;
	case FieldType::InternalEnergy:
	  intEnergy = buf[0];
	  break;
	default:
	  std::vector<float> tmp(fs.dim);
	  for (int i = 0; i < fs.dim; ++i)
	    tmp[i] = static_cast<float>(buf[i]);

	  assignField<float>(p, fs.fType, tmp.data(), fs.dim);
	  if(curIndex_<10 && fs.fType == FieldType::Velocity)
	    printf("Vel=%g %g %g buf=%g %g %g\n", p.vel[0], p.vel[1], p.vel[2], buf[0], buf[1], buf[2]);

	  if(ptype>=3 && fs.fType == FieldType::Mass)
	    printf("Mass=%g buf=%g\n", p.mass, buf[0]);
	  
	  break;
	}
      }else{
	if(localIdx < 10)
	  printf("Unknown data type for this field... label:%s\n", fs.name);
      }            
    }
    
    // 5) 温度の後処理（必要なら）
    if (flag_computeTemperature_) {
      p.temperature = computeTemperature_(electronFrac, H2Frac, gammaVal, intEnergy);
      p.temperature *= factor_temperature_;
    }
    
    p.originalHsml = std::pow(p.mass / p.density * 3.0 / (4.0 * M_PI), 1.0 / 3.0);    
    p.density     *= factor_density_;
   
    ++curIndex_;
    return true;
  }
  
  void close() override {
    particles_.clear();
    curIndex_ = 0;
    file_.close();
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

  if(curIndex_ < 10)
    printf("gamma=%g Eint=%g denom=%g val=%g\n", gamma, Eint, denom, (gamma - 1.0f) * Eint / denom);
  
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
  
bool test_dataset(int t, FieldType fType)
{
  switch (fType) {
  case FieldType::Position:
    return true;
    break;
  case FieldType::Velocity:
    return true;
    break;    
  case FieldType::Mass:
    if(mass_type[t] > 0.)
      return false;
    else
      return true;
    break;    
  case FieldType::ID:
    return true;
    break;
  case FieldType::Hsml:
    return (t==0?true:false);
    break;
  case FieldType::Density:    
    return (t==0?true:false);
    break;
  case FieldType::Temperature:
    return (t==0?true:false);
    break;
  case FieldType::InternalEnergy:
    return (t==0?true:false);
    break;
  case FieldType::ElectronFraction:
    return (t==0?true:false);
    break;
  case FieldType::H2Fraction:
    return (t==0?true:false);
    break;
  case FieldType::Gamma:
    return (t==0?true:false);
    break;
  default:
    return true;
    break;
  }
}

void loadBlock(size_t gIdx) {
  auto &pg = parts_[gIdx];
  size_t localStart = curIndex_ - IndexStart[gIdx];
  size_t rem = static_cast<size_t>(pg.count - localStart);
  size_t count = std::min(blockSize_, rem);

  hsize_t offset[2] = { static_cast<hsize_t>(localStart), 0 };

  for (auto &fs : pg.fields) {
    H5::DataSpace fileSpace = fs.ds.getSpace();
    int rank = fileSpace.getSimpleExtentNdims();

    if (rank == 1) {
      // ---- 1D データセット（例：Mass, Density などスカラ配列）----
      hsize_t block1[1] = { static_cast<hsize_t>(count) };
      hsize_t off1[1]   = { static_cast<hsize_t>(localStart) };
      fileSpace.selectHyperslab(H5S_SELECT_SET, block1, off1);

      // メモリ側も 1D（長さ count）をその都度作る（rawBuf 先頭に詰める）
      H5::DataSpace mem1(1, block1);

      fs.ds.read(fs.rawBuf.data(), fs.dType, mem1, fileSpace);
    } else {
      // ---- 2D データセット（例：Position/Velocity の Nx3 など）----
      hsize_t block2[2] = {
	static_cast<hsize_t>(count),
	static_cast<hsize_t>(fs.dim)   // fs.dim は “実成分数”
      };
      hsize_t off2[2] = {
	static_cast<hsize_t>(localStart),
	0
      };
      fileSpace.selectHyperslab(H5S_SELECT_SET, block2, off2);

      // memspace は (blockSize_, fs.dim)。ここから (count, fs.dim) を切り出す。
      H5::DataSpace mem2(fs.memspace);
      hsize_t offMem[2] = { 0, 0 };
      mem2.selectHyperslab(H5S_SELECT_SET, block2, offMem);

      fs.ds.read(fs.rawBuf.data(), fs.dType, mem2, fileSpace);
    }    
  }
  
  currentBlockCount_ = count;
  blockLoadedIdx_   = localStart;
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

  double UnitLength_in_cm;
  double UnitMass_in_g;
  double UnitVelocity_in_cm_per_s;
  double Hubble;
  
public:
  FileInfo(CameraContext& cam):
    camCtx(cam)
  {
    initDefaultFormatTokens();
  }

  void setUnit(ParticleArray *P){
    UnitLength_in_cm = P->UnitLength_in_cm;
    UnitMass_in_g = P->UnitMass_in_g;
    UnitVelocity_in_cm_per_s = P->UnitVelocity_in_cgs;
    Hubble = P->Hubble;
  }
  
  void loadNewSnapshot(int newindex, ParticleArray* P);
  void loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P);
  void generateTestData(ParticleArray *P);
  
#ifdef HAVE_HDF5
  void ShowHDF5FieldMappingDialog();
  void showHDF5Dialog(void){    
    showHDF5MappingDialog = true;
    formatTokensEdit = formatTokens;
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
