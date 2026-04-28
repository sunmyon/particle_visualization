#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <glm/glm.hpp>

#include "data/data_type.h"

class ParticleData {
public:
  ParticleData() noexcept {}
  
  float pos[3];          // Normalized coordinates for rendering.
  float original_pos[3]; // Original coordinates read from the file, used as normalization input.
  float vel[3];
  float originalHsml;
  float Hsml;
  float density;
  float temperature;
  float val_show;
  float mass;            // mass
  uint8_t   type;            // Particle type, 0 through 5.
  uint8_t   flag_stress;
  int   ID;

  float getValue(const std::string &var) const{
    if (var == "x")
      return pos[0];
    else if (var == "y")
      return pos[1];
    else if (var == "z")
      return pos[2];
    else if (var == "r") {
      // Compute r as distance from the origin; this can use another center if needed.
      return glm::length(glm::vec3(pos[0], pos[1], pos[2]));
    }
    else if (var == "Density")
      return density;
    else if (var == "Temperature")
      return temperature;
    else if (var == "Hsml")
      return Hsml;
    else if (var == "Mass")
      return mass;
    else {
      std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
      return 0.0f;
    }
  }
};

struct AoSExtensionBuffer {
  size_t stride = 0;               // bytes per particle
  std::vector<uint8_t> bytes;      // size = stride * N

  void resize(size_t n) {
    bytes.resize(n * stride);
  }

  uint8_t* ptr(size_t i) {
    return bytes.data() + i * stride;
  }

  const uint8_t* ptr(size_t i) const {
    return bytes.data() + i * stride;
  }
};

struct SoAField {
  DataType type{};
  int comps = 1;                   // number of components per particle
  std::vector<uint8_t> bytes;      // raw data buffer

  void resize(size_t n) {
    bytes.resize(n * (size_t)comps * dataTypeSize(type));
  }

  uint8_t* ptr(size_t i) {
    return bytes.data() + i * (size_t)comps * dataTypeSize(type);
  }

  const uint8_t* ptr(size_t i) const {
    return bytes.data() + i * (size_t)comps * dataTypeSize(type);
  }
};

static constexpr const char* kBfieldKey = "Bfield";
static constexpr const char* kMetallicityKey = "Metallicity";
static constexpr const char* kElectronAbundanceKey = "ElectronAbundance";
static constexpr const char* kH2AbundanceKey = "H2Abundance";
static constexpr const char* kHDAbundanceKey = "HDAbundance";
static constexpr const char* kJ21Key = "J21";
static constexpr const char* kVal1Key = "Val1";
static constexpr const char* kVal2Key = "Val2";

template<typename T, int N>
struct SoAView {
  const char* key;
};

namespace soa_views {
  inline constexpr SoAView<float,3> Bfield{ kBfieldKey };
  inline constexpr SoAView<float,1> Metallicity{ kMetallicityKey };
  inline constexpr SoAView<float,1> ElectronAbundance{ kElectronAbundanceKey };
  inline constexpr SoAView<float,1> H2Abundance{ kH2AbundanceKey };
  inline constexpr SoAView<float,1> HDAbundance{ kHDAbundanceKey };
  inline constexpr SoAView<float,1> J21{ kJ21Key };
  inline constexpr SoAView<float,1> Val1{ kVal1Key };
  inline constexpr SoAView<float,1> Val2{ kVal2Key };
}
