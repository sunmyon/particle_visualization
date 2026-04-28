#pragma once

#include <vector>
#include "FileIO/file_format_types.h"
#include "data/particle_block.h"

class ParticleMask;

class IParticleReader {
public:
  virtual ~IParticleReader() = default;

  virtual bool open(const std::string& path, HeaderInfo& header) = 0;
  virtual void close() = 0;

  virtual size_t particleCount() const = 0;

  virtual bool readRange(ParticleBlock& out,
                         size_t begin, size_t count,
                         const std::vector<FieldSpec>& fields,
			 ParticleMask* mask = nullptr) = 0;

  virtual bool is_binary() { return false; };
  
  // Convenience function for reading the whole file.
  bool readAll(ParticleBlock& out,
               const std::vector<FieldSpec>& fields)
  {
    return readRange(out, 0, particleCount(), fields, nullptr);
  }

  bool tryFixAndCheckBinary(const std::string& fullPath, HeaderInfo& hdr, std::vector<FieldSpec>& formatTokens){
    if(is_binary() == false){
      return true;
    }

    const int iter_max = 20;    
      
    auto testTokens = formatTokens;
    for(int iter=0;iter<iter_max;iter++){	
      bool flag_success = check(fullPath, hdr, testTokens);
      if(flag_success){
	formatTokens = std::move(testTokens);
	return true;
      }

      bool has_dummy = false;
      for(size_t i=0;i<testTokens.size();i++){
	if(testTokens[i].key == FieldKey::Dummy){
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
             size_t ncheck = 100)
  {
    if (!open(path, header)) return false;

    const size_t n = std::min(ncheck, particleCount());
    ParticleBlock blk;
    bool ok = readRange(blk, 0, n, fields);

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
