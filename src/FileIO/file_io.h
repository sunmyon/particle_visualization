#pragma once
// POSIX I/O 用
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unordered_map>
#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
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


struct FieldSpec {
  std::string label;        // "position" "velocity" "Bfield" ...
  DataType type;
  int count;                // 1 or 3 etc
  std::string displayName;  // HDF5 dataset 名 or Binary token 名

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

  static void SetDefaultDisplayName(FieldSpec& tok) {
    if      (tok.label == "position"        ) tok.displayName = candidatePosNames;
    else if (tok.label == "velocity"        ) tok.displayName = candidateVelNames;
    else if (tok.label == "mass"            ) tok.displayName = candidateMassNames;
    else if (tok.label == "ID"              ) tok.displayName = candidateIDNames;
    else if (tok.label == "density"         ) tok.displayName = candidateDensityNames;
    else if (tok.label == "temperature"     ) tok.displayName = candidateTemperatureNames;
    else if (tok.label == "H2fraction"      ) tok.displayName = candidateH2INames;    
    else if (tok.label == "electronfraction") tok.displayName = candidateElecNames;
    else if (tok.label == "Gamma"           ) tok.displayName = candidateGammaNames;    
    else if (tok.label == "internalenergy"  ) tok.displayName = candidateInternalEnergyNames;
    else if (tok.label == "value"           ) tok.displayName = candidateValNames;
    else if (tok.label == "value2"          ) tok.displayName = candidateVal2Names;
    else    tok.displayName = tok.label;    
  }
};

struct FieldLayout{
  FieldSpec spec;
  int offset;
};

static inline bool isAoSCoreLabel(const std::string& lbl){
  return (lbl == "position" ||
          lbl == "velocity" ||
          lbl == "mass" ||
          lbl == "density" ||
          lbl == "temperature" ||
          lbl == "value" ||
          lbl == "value2" ||
          lbl == "Hsml" ||
          lbl == "type" ||
          lbl == "ID");
}



enum class DestKind { AoSCore, AoSExt, SoA, Ignore };

struct FieldPlanItem {
  DestKind dest = DestKind::Ignore;
  size_t aosExtOffset = 0;          // AoSExt のとき
  std::string soaKey;               // SoA のとき（例 "Bfield"）
  DataType type{};
  int count = 1;
};

struct IOPlan {
  std::unordered_map<std::string, FieldPlanItem> plan; // label->item
  size_t aosExtStride = 0;                             // AoSExt stride
};

static IOPlan buildPlanFromToks(const std::vector<FieldSpec>& toks) {
  IOPlan plan;

  for (const auto& fs : toks) {
    FieldPlanItem item;
    item.type  = fs.type;
    item.count = fs.count;

    // 例：Bfield は SoA
    if (fs.label == "Bfield") {
      item.dest = DestKind::SoA;
      item.soaKey = "Bfield";
    }
    // それ以外は AoSCore（ParticleData に入れる）か Ignore
    else if (isAoSCoreLabel(fs.label)) {
      item.dest = DestKind::AoSCore;
    }
    else {
      // 将来の未知フィールドは、とりあえず読まない（Ignore）
      // 「未知も保持したい」なら SoA に落とすのが一番簡単
      item.dest = DestKind::Ignore;
    }

    plan.plan[fs.label] = std::move(item);
  }

  return plan;
}

// ---- AoSCoreへの代入（最低限。あなたの assignField をここに集約してもOK）----
template<class T>
static inline void assignCore(ParticleData& p, const std::string& label, const T* v){
  if(label=="position"){
    for(int k=0;k<3;k++){ p.pos[k]=float(v[k]); p.original_pos[k]=p.pos[k]; }
  } else if(label=="velocity"){
    for(int k=0;k<3;k++){ p.vel[k]=float(v[k]); }
  } else if(label=="mass"){
    p.mass=float(v[0]);
  } else if(label=="density"){
    p.density=float(v[0]);
  } else if(label=="temperature"){
    p.temperature=float(v[0]);
  } else if(label=="value"){
    p.val=float(v[0]);
  } else if(label=="value2"){
    p.val2=float(v[0]);
  } else if(label=="Hsml"){
    p.Hsml=float(v[0]); p.originalHsml=p.Hsml;
  } else if(label=="type"){
    p.type=int(v[0]);
  } else if(label=="ID"){
    p.ID=int(v[0]);
  }
}



class IParticleReader {
public:
  virtual ~IParticleReader() = default;

  virtual bool open(const std::string& path, HeaderInfo& header) = 0;
  virtual void close() = 0;

  virtual size_t particleCount() const = 0;

  virtual bool readRange(ParticleBlock& out,
                         size_t begin, size_t count,
                         const std::vector<FieldSpec>& fields,
                         const IOPlan& plan) = 0;

  virtual bool is_binary() { return false; };
  
  // 便利関数：全読み
  bool readAll(ParticleBlock& out,
               const std::vector<FieldSpec>& fields,
               const IOPlan& plan)
  {
    return readRange(out, 0, particleCount(), fields, plan);
  }

  bool tryFixAndCheckBinary(const std::string& fullPath, HeaderInfo& hdr, std::vector<FieldSpec>& formatTokens){
    if(is_binary() == false){
      return true;
    }

    const int iter_max = 20;    
      
    auto testTokens = formatTokens;
    for(int iter=0;iter<iter_max;iter++){	
      IOPlan plan = buildPlanFromToks(testTokens);      
      bool flag_success = check(fullPath, hdr, testTokens, plan);
      if(flag_success){
	formatTokens = std::move(testTokens);
	return true;
      }

      bool has_dummy = false;
      for(size_t i=0;i<testTokens.size();i++){
	if(testTokens[i].label == "dummy"){
	  has_dummy = true;
	  if(iter == 0){
	    testTokens[i].count = 0;
	    break;
	  }else{
	    testTokens[i].count++;
	    break;
	  }	    
	}
      }

      if (!has_dummy) {
	std::cerr << "There is no label named dummy. No room for the adjustment\n";
	return false;
      }

      printf("iter=%d failed to read the file...\n", iter);	
    }

    printf("Too many iterations. Fialed to read the file %s\n", fullPath.c_str());
    return false;    
  }
  
  bool check(const std::string& path, HeaderInfo& header,
             const std::vector<FieldSpec>& fields,
             const IOPlan& plan,
             size_t ncheck = 100)
  {
    if (!open(path, header)) return false;

    const size_t n = std::min(ncheck, particleCount());
    ParticleBlock blk;
    bool ok = readRange(blk, 0, n, fields, plan);

    if (ok) {
      for (size_t i = 0; i < n; ++i) {
        const auto &p = blk.particles[i];
        if (p.type < 0 || p.type > 5) {
          ok = false;
          printf("Why? P[%zu].type = %d\n", i, p.type);
          break;
        }
      }
    }

    close();
    return ok;
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


// record 全体の配置情報
struct RecordLayout {
  size_t recordSize = 0;
  std::vector<FieldLayout> fields;
};

static inline RecordLayout buildRecordLayout(const std::vector<FieldSpec>& tokens) {
  RecordLayout rl;
  rl.fields.reserve(tokens.size());

  size_t off = 0;

  for (const auto& tok : tokens) {
    if (tok.count <= 0) {
      throw std::runtime_error("buildRecordLayout: token.count must be > 0");
    }

    const size_t elemSz = dataTypeSize(tok.type);
    const size_t bytes  = elemSz * static_cast<size_t>(tok.count);

    FieldLayout fl;
    fl.offset = off;
    fl.spec   = tok;

    rl.fields.push_back(fl);
    off += bytes;
  }

  rl.recordSize = off;
  return rl;
}


template<class T>
static inline void writeToDest(ParticleBlock& out, size_t i,
                               const FieldSpec& fs, const IOPlan& plan,
                               const T* vals)
{
  auto it = plan.plan.find(fs.label);
  if(it==plan.plan.end()) return;
  const auto& pi = it->second;

  if(pi.dest==DestKind::Ignore) return;

  if(pi.dest==DestKind::AoSCore){
    assignCore(out.particles[i], fs.label, vals);
    return;
  }

  if(pi.dest==DestKind::AoSExt){
    if(out.aosExt.stride==0) return;
    uint8_t* dst = out.aosExt.ptr(i) + pi.aosExtOffset;
    std::memcpy(dst, vals, (size_t)fs.count * sizeof(T));
    return;
  }

  if(pi.dest==DestKind::SoA){
    auto &f = out.soa[pi.soaKey];
    if(f.bytes.empty()){
      f.type = fs.type;
      f.comps = fs.count;
      f.resize(out.particles.size());
    }
    std::memcpy(f.ptr(i), vals, (size_t)fs.count * sizeof(T));
    return;
  }
}


class BinaryReader final : public IParticleReader {
  std::ifstream file_;
  size_t npart_ = 0;
  size_t data_offset_ = 0;
  
public:
  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if(!file_) return false;

    // ---- header 読み ----
    float t;
    int   n;
    file_.read(reinterpret_cast<char*>(&t), sizeof t);
    file_.read(reinterpret_cast<char*>(&n), sizeof n);
    if(!file_) return false;

    header.time  = t;
    header.npart = n;
    header.flag_hdf5 = false;

    npart_ = static_cast<size_t>(n);

    // ---- ここが重要 ----
    // この位置が「粒子 record の先頭」
    data_offset_ = file_.tellg();

    return true;
  }

  void close() override {
    file_.close();
  }

  bool is_binary() override {
    return true;
  }
  
  size_t particleCount() const override {
    return npart_;
  }
  
  bool readRange(ParticleBlock& out,
                 size_t begin, size_t count,
                 const std::vector<FieldSpec>& fields,
                 const IOPlan& plan) override
  {
    if(begin + count > npart_) return false;
    
    // record layout（FieldSpec順）
    const RecordLayout layout = buildRecordLayout(fields);

    out.aosExt.stride = plan.aosExtStride;
    out.resize(count);

    // ---- SoA 事前確保 ----
    for(const auto& fs : fields){
      auto it = plan.plan.find(fs.label);
      if(it!=plan.plan.end() && it->second.dest==DestKind::SoA){
        auto &f = out.soa[it->second.soaKey];
        f.type  = fs.type;
        f.comps = fs.count;
        f.resize(count);
      }
    }

    // ---- begin 番目の粒子まで seek ----
    const std::streamoff offset =
      data_offset_ + static_cast<std::streamoff>(begin * layout.recordSize);

    file_.seekg(offset, std::ios::beg);
    if(!file_) return false;

    std::vector<uint8_t> record(layout.recordSize);

    for(size_t i = 0; i < count; ++i){
      file_.read(reinterpret_cast<char*>(record.data()),
                 static_cast<std::streamsize>(record.size()));
      if(file_.gcount() != static_cast<std::streamsize>(record.size()))
        return false;

      // ---- record 内の各 field を振り分け ----
      for(const auto& fl : layout.fields){
        const uint8_t* src = record.data() + fl.offset;
        const FieldSpec& fs = fl.spec;

        switch(fs.type){
	case DataType::Float:
	  writeToDest(out, i, fs, plan,
		      reinterpret_cast<const float*>(src));
	  break;
	case DataType::Double:
	  writeToDest(out, i, fs, plan,
		      reinterpret_cast<const double*>(src));
	  break;
	case DataType::Int32:
	  writeToDest(out, i, fs, plan,
		      reinterpret_cast<const int32_t*>(src));
	  break;
	case DataType::Int64:
	  writeToDest(out, i, fs, plan,
		      reinterpret_cast<const int64_t*>(src));
	  break;
        }
      }
    }

    return true;
  }
};

#ifdef USE_MMAP
#include <sys/mman.h>
class MMapReader final : public IParticleReader {
  int fd_ = -1;
  uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t npart_ = 0;
  size_t data_offset_ = 0;
  
public:
  bool open(const std::string& path, HeaderInfo& header) override {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return false;

    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
      close();
      return false;
    }
    size_ = static_cast<size_t>(st.st_size);

    data_ = static_cast<uint8_t*>(::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (data_ == MAP_FAILED) {
      data_ = nullptr;
      close();
      return false;
    }

    // ---- header 読み（先頭に time(float), npart(int)）----
    if (size_ < sizeof(float) + sizeof(int)) {
      close();
      return false;
    }

    float t;
    int n;
    std::memcpy(&t, data_, sizeof(float));
    std::memcpy(&n, data_ + sizeof(float), sizeof(int));

    header.time  = t;
    header.npart = n;
    header.flag_hdf5 = false;

    if (n < 0) { // 念のため
      close();
      return false;
    }
    npart_ = static_cast<size_t>(n);

    // ---- ここが重要：粒子データはこの位置から始まる ----
    data_offset_ = sizeof(float) + sizeof(int);

    return true;
  }

  void close() override {
    if(data_){ ::munmap(data_, size_); data_=nullptr; }
    if(fd_>=0){ ::close(fd_); fd_=-1; }
    size_=0; npart_=0; data_offset_=0;
  }

  bool is_binary() override {
    return true;
  }
  
  size_t particleCount() const override {
    return npart_;
  }

  bool readRange(ParticleBlock& out,
                 size_t begin, size_t count,
                 const std::vector<FieldSpec>& fields,
                 const IOPlan& plan) override
  {
    const RecordLayout layout = buildRecordLayout(fields);

    if (layout.recordSize == 0) return false;
    if (begin + count > npart_) return false;

    // ファイルサイズ整合性チェック（重要）
    const size_t needBytes = data_offset_ + npart_ * layout.recordSize;
    if (needBytes > size_) {
      // header の npart と実ファイルサイズが矛盾している
      return false;
    }

    out.aosExt.stride = plan.aosExtStride;
    out.resize(count);

    // SoA 事前確保
    for(const auto& fs : fields){
      auto it = plan.plan.find(fs.label);
      if(it!=plan.plan.end() && it->second.dest==DestKind::SoA){
        auto &f = out.soa[it->second.soaKey];
        f.type = fs.type;
        f.comps = fs.count;
        f.resize(count);
      }
    }

    const uint8_t* base = data_ + data_offset_ + begin * layout.recordSize;
    const uint8_t* end = base + count * layout.recordSize;
    if(end > data_ + size_) return false;

    for(size_t i=0;i<count;i++){
      const uint8_t* rec = base + i * layout.recordSize;

      for(const auto& fl : layout.fields){
        const uint8_t* src = rec + fl.offset;
        const auto& fs = fl.spec;

        switch(fs.type){
	case DataType::Float:
	  writeToDest(out, i, fs, plan, reinterpret_cast<const float*>(src));
	  break;
	case DataType::Double:
	  writeToDest(out, i, fs, plan, reinterpret_cast<const double*>(src));
	  break;
	case DataType::Int32:
	  writeToDest(out, i, fs, plan, reinterpret_cast<const int32_t*>(src));
	  break;
	case DataType::Int64:
	  writeToDest(out, i, fs, plan, reinterpret_cast<const int64_t*>(src));
	  break;
        }
      }
    }
    return true;
  }
};
#endif


#ifdef HAVE_HDF5
#include <H5Cpp.h>
#include "hdf5_utils.h"

class HDF5Reader final : public IParticleReader {
  H5::H5File file_;

  size_t npart_ = 0;
  size_t blockSize_ = 1 << 16; // chunk read

  // Gadget 系を想定：ptype=0..5
  double mass_type_[6]{};   // MassTable
  size_t count_[6]{};       // NumPart_Total/ThisFile
  size_t IndexStart_[7]{};  // prefix sum

  bool   flag_skip_DM_ = false;            // 必要なら
  bool   flag_computeTemperature_ = false; // 以前と同じ後処理
  double factor_temperature_ = 1.0;
  double factor_density_ = 1.0;

  // ---- temperature formula (元の式) ----
  double computeTemperature_(double f_elec, double f_H2, double gamma, double Eint) const {
    double denom = 1.2;
    if (f_elec > 0.) denom += f_elec;
    if (f_H2   > 0.) denom -= f_H2;
    if (gamma  < 0.) gamma = 5.0/3.0;
    return (gamma - 1.0) * Eint / denom;
  }

  static std::string partPath_(int ptype, const std::string& dsName) {
    return "/PartType" + std::to_string(ptype) + "/" + dsName;
  }

  // ---- attribute helpers ----
  static bool hasAttr_(H5::H5Object& obj, const char* name) {
    // H5Cpp には exists の便利関数が弱いので例外で判定
    try { (void)obj.openAttribute(name); return true; }
    catch (...) { return false; }
  }

  // ---- dataset rank ----
  static int getRank_(const H5::DataSet& ds) {
    H5::DataSpace sp = ds.getSpace();
    return sp.getSimpleExtentNdims();
  }

  // ---- infer particle count from dataset dims ----
  static size_t inferCountFromDataset_(H5::H5File& f, int ptype, const std::string& dsName) {
    try {
      H5::DataSet ds = f.openDataSet(partPath_(ptype, dsName));
      H5::DataSpace sp = ds.getSpace();
      int nd = sp.getSimpleExtentNdims();
      if (nd < 1) return 0;
      std::vector<hsize_t> dims((size_t)nd, 0);
      sp.getSimpleExtentDims(dims.data());
      return (size_t)dims[0];
    } catch (...) {
      return 0;
    }
  }

public:
  HDF5Reader() = default;

  bool is_binary() override { return false; }
  size_t particleCount() const override { return npart_; }

  bool open(const std::string& path, HeaderInfo& header) override {
    // 初期化
    npart_ = 0;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
    IndexStart_[6]=0;

    // open file
    file_ = H5::H5File(path, H5F_ACC_RDONLY);

    // /Header の属性を読む
    bool hasHeader = false;
    try {
      H5::Group hg = file_.openGroup("/Header");
      hasHeader = true;

      double time = 0.0;
      (void)readAttributeScalar(hg, "Time", time);
      header.time = (float)time;

      // MassTable double[6]
      double mt[6]{};
      if (readAttributeArray(hg, "MassTable", mt)) {
        for (int t=0;t<6;++t) mass_type_[t] = mt[t];
      } else {
        for (int t=0;t<6;++t) mass_type_[t] = 0.0;
      }

      // NumPart_Total (uint64 or uint32)
      bool okNum = false;
      {
        unsigned long long n64[6]{};
        unsigned int       n32[6]{};

        if (readAttributeArray(hg, "NumPart_Total", n64)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n64[t];
          okNum = true;
        } else if (readAttributeArray(hg, "NumPart_Total", n32)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
          okNum = true;
        }
      }

      if (!okNum) {
        unsigned long long n64[6]{};
        unsigned int       n32[6]{};

        if (readAttributeArray(hg, "NumPart_ThisFile", n64)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n64[t];
          okNum = true;
        } else if (readAttributeArray(hg, "NumPart_ThisFile", n32)) {
          for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
          okNum = true;
        }
      }

      // okNum=false のときは後で dataset から推定

    } catch (...) {
      hasHeader = false;
      header.time = 0.0f;
      for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; }
    }

    // /Header から count が取れていない場合のフォールバック
    bool allZero = true;
    for (int t=0;t<6;++t) if (count_[t] > 0) { allZero=false; break; }

    if (!hasHeader || allZero) {
      for (int t=0;t<6;++t) {
        size_t n = inferCountFromDataset_(file_, t, "Coordinates");
        if (n==0) n = inferCountFromDataset_(file_, t, "Velocities");
        if (n==0) n = inferCountFromDataset_(file_, t, "ParticleIDs");
        count_[t] = n;
      }
    }

    // prefix sum
    IndexStart_[0] = 0;
    for (int t=0;t<6;++t) IndexStart_[t+1] = IndexStart_[t] + count_[t];
    npart_ = IndexStart_[6];

    header.npart     = (int)npart_;
    header.flag_hdf5 = true;

    // ここで unit 由来の係数を設定したいならここで
    // factor_density_ = ...
    // factor_temperature_ = ...

    return true;
  }

  void close() override {
    try { file_.close(); } catch (...) {}
    npart_ = 0;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
    IndexStart_[6]=0;
  }

  bool readRange(ParticleBlock& out,
                 size_t begin, size_t count,
                 const std::vector<FieldSpec>& fields,
                 const IOPlan& plan) override
  {
    if (begin + count > npart_) return false;

    out.resize(count);

    // SoA allocate (planで指定されたものだけ)
    for (const auto& fs : fields) {
      auto pit = plan.plan.find(fs.label);
      if (pit != plan.plan.end() && pit->second.dest == DestKind::SoA) {
        auto &f = out.soa[pit->second.soaKey];
        f.type  = fs.type;
        f.comps = fs.count;
        f.resize(count);
      }
    }

    // 温度後処理用（元コード踏襲）
    std::vector<double> tmp_e(count, -1.0), tmp_h2(count, -1.0), tmp_g(count, -1.0), tmp_u(count, -1.0);

    const size_t globalBegin = begin;
    const size_t globalEnd   = begin + count;

    for (int ptype=0; ptype<6; ++ptype) {
      if (flag_skip_DM_ && ptype==1) continue;

      const size_t pBeg = IndexStart_[ptype];
      const size_t pEnd = IndexStart_[ptype+1];
      if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

      const size_t subBegG  = std::max(globalBegin, pBeg);
      const size_t subEndG  = std::min(globalEnd,   pEnd);
      const size_t subCount = subEndG - subBegG;

      const size_t localStart = subBegG - pBeg;
      const size_t outStart   = subBegG - globalBegin;

      // type/mass の固定値（元コード同様）
      for (size_t i=0;i<subCount;++i) {
        out.particles[outStart+i].type = ptype;
        if (mass_type_[ptype] > 0.0) out.particles[outStart+i].mass = (float)mass_type_[ptype];
      }

      // fields を読む
      for (const auto& fs : fields) {
        auto pit = plan.plan.find(fs.label);
        if (pit == plan.plan.end() || pit->second.dest == DestKind::Ignore) continue;

	const std::string dsName = fs.displayName.empty() ? fs.label : fs.displayName;
	
        H5::DataSet ds;
        try {
          ds = file_.openDataSet(partPath_(ptype, dsName));
        } catch (...) {
          // dataset無い場合はスキップ（MassTableなどで埋まるものはそれでOK）
          continue;
        }

        H5::PredType dtype =
          (fs.type == DataType::Float)  ? H5::PredType::NATIVE_FLOAT  :
          (fs.type == DataType::Double) ? H5::PredType::NATIVE_DOUBLE :
          (fs.type == DataType::Int32)  ? H5::PredType::NATIVE_INT32  :
                                          H5::PredType::NATIVE_LLONG;

        const int comps = fs.count;
        const size_t elemSz = dataTypeSize(fs.type);

        size_t done = 0;
        std::vector<uint8_t> buf;

        while (done < subCount) {
          const size_t chunk = std::min(blockSize_, subCount - done);

          const int rank = getRank_(ds);
          if (!(rank==1 || rank==2)) return false;

	  readHyperslabBytes(ds, dtype,
			     (hsize_t)(localStart + done),
			     (hsize_t)chunk,
			     comps, buf, elemSz);

          const size_t bytesPerParticle = (size_t)comps * elemSz;

          if (pit->second.dest == DestKind::SoA) {
            auto &f = out.soa[pit->second.soaKey];
            std::memcpy(f.bytes.data() + (outStart + done) * bytesPerParticle,
                        buf.data(),
                        chunk * bytesPerParticle);
          } else {
            for (size_t j=0;j<chunk;++j) {
              const size_t oi = outStart + done + j;
              const uint8_t* one = buf.data() + j * bytesPerParticle;

              // AoSCore へ（あなたの既存 writeToDest を使用）
              switch (fs.type) {
                case DataType::Float:  writeToDest(out, oi, fs, plan, reinterpret_cast<const float*>(one)); break;
                case DataType::Double: writeToDest(out, oi, fs, plan, reinterpret_cast<const double*>(one)); break;
                case DataType::Int32:  writeToDest(out, oi, fs, plan, reinterpret_cast<const int32_t*>(one)); break;
                case DataType::Int64:  writeToDest(out, oi, fs, plan, reinterpret_cast<const int64_t*>(one)); break;
              }

              // 温度補正用に拾う（fs.label はあなたの label 設計に合わせてください）
              if (flag_computeTemperature_) {
                auto get0 = [&](void)->double{
                  if (fs.type == DataType::Double) return reinterpret_cast<const double*>(one)[0];
                  if (fs.type == DataType::Float)  return (double)reinterpret_cast<const float*>(one)[0];
                  if (fs.type == DataType::Int32)  return (double)reinterpret_cast<const int32_t*>(one)[0];
                  if (fs.type == DataType::Int64)  return (double)reinterpret_cast<const int64_t*>(one)[0];
                  return -1.0;
                };
                const double v0 = get0();

                if      (fs.label == "electronfraction" || fs.label == "ElectronFraction") tmp_e[oi]  = v0;
                else if (fs.label == "H2fraction"       || fs.label == "H2Fraction")       tmp_h2[oi] = v0;
                else if (fs.label == "Gamma")                                           tmp_g[oi]  = v0;
                else if (fs.label == "internalenergy"   || fs.label == "InternalEnergy") tmp_u[oi]  = v0;
              }
            }
          }

          done += chunk;
        } // while done
      } // for fields
    } // for ptype

    // 後処理：temperature（元コードの式）
    if (flag_computeTemperature_) {
      for (size_t i=0;i<count;++i) {
        const double T = computeTemperature_(tmp_e[i], tmp_h2[i], tmp_g[i], tmp_u[i]);
        out.particles[i].temperature = (float)(T * factor_temperature_);
      }
    }

    // density factor（元コード踏襲するならここで）
    if (factor_density_ != 1.0) {
      for (size_t i=0;i<count;++i) out.particles[i].density = (float)((double)out.particles[i].density * factor_density_);
    }

    return true;
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
  std::vector<FieldSpec> formatTokens;

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
  TrackingVector<ParticleBlock> batchParticleBlocks; // バッチ内の各ファイルの粒子データ

#ifdef HAVE_HDF5
  bool showHDF5MappingDialog = false;
#endif
  
  bool showFormatDialog = false;
  std::vector<FieldSpec> formatTokensEdit; // 編集用一時コピー
  
  // 更新タイミングでのみ使うmutex
  std::mutex g_dataMutex;
    
  void syncLoadFirstFile(int targetFile, ParticleArray *P);
  void asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep);

  bool loadSingleFile(int fileNumber, ParticleBlock& particles);

#ifdef HAVE_HDF5
  TrackingVector<ParticleData> loadParticlesFromHDF5(const std::string& filename, HeaderInfo& hdr);
#endif
  
  void initDefaultFormatTokens();

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
