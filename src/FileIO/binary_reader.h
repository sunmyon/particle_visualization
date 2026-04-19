#pragma once
#pragma once
#include "FileIO/particle_reader.h"
#include "FileIO/file_mask.h"
#include "core/PerfTimer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <fstream>
#include "FileIO/file_layout.h"

class BinaryReader final : public IParticleReader {
  std::ifstream file_;
  std::vector<char> ioBuf_;
  size_t npart_ = 0;
  size_t data_offset_ = 0;
  
public:
  bool open(const std::string& path, HeaderInfo& header) override {
    file_.open(path, std::ios::binary);
    if(!file_) return false;

    ioBuf_.resize(32 * 1024 * 1024);
    file_.rdbuf()->pubsetbuf(ioBuf_.data(),
                             static_cast<std::streamsize>(ioBuf_.size()));
    
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
		 ParticleMask* mask = nullptr) override
  {
    TIME_SCOPE("BinaryReader readRange total");
    
    if(begin + count > npart_) return false;
    
    // record layout (ordered by FieldSpec)
    const BinaryReadLayout layout = buildBinaryReadLayout(fields);

    if (layout.recordSize == 0) return false;
      
    out.aosExt.stride = 0;
    out.resize(count);    
    
    for (const auto& fl : layout.fields) {
      if (fl.dest != DestKind::SoA) continue;
      auto& f = out.soa[fl.soaKey];
      f.type = fl.spec.type;
      f.comps = fl.spec.count;
      f.resize(count);
    }
    
    // ---- begin 番目の粒子まで seek ----
    const std::streamoff offset =
      data_offset_ + static_cast<std::streamoff>(begin * layout.recordSize);

    file_.seekg(offset, std::ios::beg);
    if(!file_) return false;

    const size_t recSz = layout.recordSize;
    if (recSz == 0) return false;

    const size_t targetBytes = 32u * 1024u * 1024u;
    
    size_t chunkRecs = targetBytes / recSz;
    chunkRecs = std::max<size_t>(chunkRecs, 1024);
    chunkRecs = std::min<size_t>(chunkRecs, 1'000'000);
    
    std::vector<uint8_t> buf(recSz * chunkRecs);

    size_t done = 0;
    while (done < count) {
      const size_t n = std::min(chunkRecs, count - done);
      const size_t bytes = recSz * n;

      {
	TIME_SCOPE("file read");
	file_.read(reinterpret_cast<char*>(buf.data()),
		   static_cast<std::streamsize>(bytes));
      }
      
      if (file_.gcount() != static_cast<std::streamsize>(bytes))
        return false;

      {
	TIME_SCOPE("dispatch fields");
	for (size_t j = 0; j < n; ++j) {
	  const size_t i = done + j;
	  const uint8_t* rec = buf.data() + j * recSz;
	  ParticleData& p = out.particles[i];

	  for (const auto& fl : layout.fields) {
	    const uint8_t* src = rec + fl.offset;

	    if (fl.dest == DestKind::AoSCore && fl.store) {
	      fl.store(p, src);
	      continue;
	    }

	    switch (fl.spec.type) {
            case DataType::Float:
              writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const float*>(src));
              break;
            case DataType::Double:
              writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const double*>(src));
              break;
            case DataType::Int32:
              writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int32_t*>(src));
              break;
            case DataType::Int64:
              writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int64_t*>(src));
              break;
	    }
	  }
	}
      }

      done += n;
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
		 ParticleMask* mask = nullptr) override
  {
    TIME_SCOPE("MMapReader readRange total");

    const BinaryReadLayout layout = buildBinaryReadLayout(fields);
    
    if (layout.recordSize == 0) return false;
    if (begin + count > npart_) return false;

    // ファイルサイズ整合性チェック（重要）
    const size_t needBytes = data_offset_ + npart_ * layout.recordSize;
    if (needBytes > size_) {
      // header の npart と実ファイルサイズが矛盾している
      return false;
    }
    
    {
      TIME_SCOPE("resize + SoA prealloc");

      out.aosExt.stride = 0;
      out.resize(count);

      for (const auto& fl : layout.fields) {
	if (fl.dest != DestKind::SoA) continue;
	auto& f = out.soa[fl.soaKey];
	f.type = fl.spec.type;
	f.comps = fl.spec.count;
	f.resize(count);
      }
    }
    
    {
      TIME_SCOPE("dispatch loop");
      const uint8_t* base = data_ + data_offset_ + begin * layout.recordSize;
      const uint8_t* end = base + count * layout.recordSize;
      if(end > data_ + size_) return false;

      for (size_t i = 0; i < count; ++i) {
	const uint8_t* rec = base + i * layout.recordSize;
	ParticleData& p = out.particles[i];

	for (const auto& fl : layout.fields) {
	  const uint8_t* src = rec + fl.offset;

	  if (fl.dest == DestKind::AoSCore && fl.store) {
	    fl.store(p, src);
	    continue;
	  }

	  switch (fl.spec.type) {
	  case DataType::Float:
	    writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const float*>(src));
	    break;
	  case DataType::Double:
	    writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const double*>(src));
	    break;
	  case DataType::Int32:
	    writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int32_t*>(src));
	    break;
	  case DataType::Int64:
	    writeFieldToParticleBlock(out, i, fl, reinterpret_cast<const int64_t*>(src));
	    break;
	  }
	}
      }      
    }
    return true;
  }
};
#endif
