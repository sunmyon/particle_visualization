#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <cmath>

#include "data/data_type.h"

class SimulationElement {
public:
  SimulationElement() noexcept
    : position{0.0f, 0.0f, 0.0f},
      vel{0.0f, 0.0f, 0.0f},
      supportRadius(0.0f),
      density(0.0f),
      temperature(0.0f),
      mass(0.0f),
      type(0)
  {
  }
  
  float position[3]; // Coordinates read from the file.
  float vel[3];
  float supportRadius;
  float density;
  float temperature;
  float mass;            // mass
  uint8_t type;              // Particle type, 0 through 5.

  float getValue(const std::string &var) const{
    if (var == "x")
      return position[0];
    else if (var == "y")
      return position[1];
    else if (var == "z")
      return position[2];
    else if (var == "r") {
      const float r2 = position[0] * position[0] +
                       position[1] * position[1] +
                       position[2] * position[2];
      return std::sqrt(r2);
    }
    else if (var == "Density")
      return density;
    else if (var == "Temperature")
      return temperature;
    else if (var == "Hsml")
      return supportRadius;
    else if (var == "Mass")
      return mass;
    else {
      std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
      return 0.0f;
    }
  }
};

struct AoSExtensionBuffer {
  size_t stride = 0;               // bytes per element
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
  int comps = 1;                   // number of components per element
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
static constexpr const char* kGammaKey = "Gamma";
static constexpr const char* kVal1Key = "Val1";
static constexpr const char* kVal2Key = "Val2";
static constexpr const char* kParticleIdKey = "ParticleID";

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
  inline constexpr SoAView<float,1> Gamma{ kGammaKey };
  inline constexpr SoAView<float,1> Val1{ kVal1Key };
  inline constexpr SoAView<float,1> Val2{ kVal2Key };
  inline constexpr SoAView<int64_t,1> ParticleID{ kParticleIdKey };
}
