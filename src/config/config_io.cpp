#include "config_io.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "FileIO/file_io.h"
#include "particle_visual_config.h"
#include "app/ui_state.h"

// ------------------------------
static inline bool startsWith(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

static inline std::string trim(const std::string& s) {
  const char* ws = " \t\r\n";
  const size_t b = s.find_first_not_of(ws);
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(ws);
  return s.substr(b, e - b + 1);
}

static inline std::string dataTypeToString(DataType t) {
  switch (t) {
    case DataType::Float: return "float";
    case DataType::Int32: return "int32";
    case DataType::Int64: return "int64";
    default:              return "double";
  }
}

static inline DataType stringToDataType(const std::string& s) {
  if (s == "float") return DataType::Float;
  if (s == "int32" || s == "int") return DataType::Int32;
  if (s == "int64" || s == "long long") return DataType::Int64;
  return DataType::Double;
}

static void saveTokenList(std::ofstream& outfile,
                          const char* countKey,
                          const std::vector<FieldSpec>& tokens)
{
  outfile << countKey << "=" << tokens.size() << "\n";
  for (size_t i = 0; i < tokens.size(); i++) {
    outfile << GetFieldKeyDisplayName(tokens[i].key) << ","
            << dataTypeToString(tokens[i].type) << ","
            << tokens[i].count << "\n";
  }
}

static void loadTokenList(std::ifstream& infile,
                          int tokenCount,
                          std::vector<FieldSpec>& outTokens)
{
  outTokens.clear();
  outTokens.reserve(std::max(0, tokenCount));

  std::string line;
  for (int i = 0; i < tokenCount; i++) {
    if (!std::getline(infile, line)) break;

    std::istringstream iss(line);
    std::string tokenLabel, tokenType, tokenCountStr;
    if (!std::getline(iss, tokenLabel, ',')) continue;
    if (!std::getline(iss, tokenType, ',')) continue;
    if (!std::getline(iss, tokenCountStr)) continue;

    FieldSpec ft;
    ft.key   = GetFieldKeyFromDisplayName(trim(tokenLabel));
    ft.type  = stringToDataType(trim(tokenType));
    ft.count = std::stoi(trim(tokenCountStr));
    ft.sourceName = GetDefaultHDF5SourceName(ft.key);

    outTokens.push_back(ft);
  }
}

static const char* quantityToString(QuantityId q) {
  return QuantityLabel(q);
}

// 必要なら後で map 化
static QuantityId quantityFromString(const std::string& s) {
  for (int i = 0; i < 1024; ++i) {
    QuantityId q = static_cast<QuantityId>(i);
    const char* label = QuantityLabel(q);
    if (label && s == label) return q;
  }
  return QuantityId::Density;
}

// ------------------------------
bool loadConfig(const std::string& filename,
                ParticleArray* P,
                FileInfo* fileInfo,
                ParticleVisualConfig* visualCfg,
                MaskUIState* outMaskState)
{
  std::ifstream infile(filename);
  if (!infile.is_open()) return false;

  if (outMaskState) {
    *outMaskState = MaskUIState{};
  }

  std::string line;
  auto& src = fileInfo->editSource();
  while (std::getline(infile, line)) {
    line = trim(line);
    if (line.empty()) continue;

    if (startsWith(line, "FileFormat=")) {
      std::string val = line.substr(std::strlen("FileFormat="));
      std::strncpy(src.fileFormat, val.c_str(), sizeof(src.fileFormat) - 1);
      src.fileFormat[sizeof(src.fileFormat) - 1] = '\0';
    }
    else if (startsWith(line, "FolderPath=")) {
      std::string val = line.substr(std::strlen("FolderPath="));
      std::strncpy(src.folderPath, val.c_str(), sizeof(src.folderPath) - 1);
      src.folderPath[sizeof(src.folderPath) - 1] = '\0';
    }
    else if (startsWith(line, "TokenCount=")) {
      int tokenCount = std::stoi(line.substr(std::strlen("TokenCount=")));
      loadTokenList(infile, tokenCount, src.formatTokens);
    }
    else if (startsWith(line, "HDF5TokenCount=")) {
      int tokenCount = std::stoi(line.substr(std::strlen("HDF5TokenCount=")));
      loadTokenList(infile, tokenCount, src.formatTokens_hdf5);
    }
    else if (startsWith(line, "ParticleType")) {
      // ParticleType0_Size=...
      size_t pos = line.find('_');
      if (pos == std::string::npos) continue;

      std::string typeStr = line.substr(12, pos - 12);
      int type = std::stoi(typeStr);
      if (type < 0 || type >= 6) continue;

      std::string key = line.substr(pos + 1);
      auto& cfg = visualCfg->types[type];

      if (startsWith(key, "Size=")) {
        cfg.pointSize = std::stof(key.substr(5));
      } else if (startsWith(key, "Min=")) {
        cfg.colorMin = std::stof(key.substr(4));
      } else if (startsWith(key, "Max=")) {
        cfg.colorMax = std::stof(key.substr(4));
      } else if (startsWith(key, "Periodic=")) {
        cfg.periodicColorBar = (std::stoi(key.substr(9)) == 1);
      } else if (startsWith(key, "UseLog=")) {
        cfg.useLogScale = (std::stoi(key.substr(7)) == 1);
      } else if (startsWith(key, "Hide=")) {
        cfg.hideParticles = (std::stoi(key.substr(5)) == 1);
      } else if (startsWith(key, "ColormapIndex=")) {
        cfg.colormapIndex = std::stoi(key.substr(14));
      } else if (startsWith(key, "Quantity=")) {
        cfg.selectedQuantity = quantityFromString(key.substr(9));
      }
    }
    else if (startsWith(line, "Mask_EnableSphere=")) {
      if (outMaskState) {
        outMaskState->enableSphere = (std::stoi(line.substr(std::strlen("Mask_EnableSphere="))) != 0);
      }
    }
    else if (startsWith(line, "Mask_CenterX=")) {
      if (outMaskState) {
        outMaskState->center[0] = std::stod(line.substr(std::strlen("Mask_CenterX=")));
      }
    }
    else if (startsWith(line, "Mask_CenterY=")) {
      if (outMaskState) {
        outMaskState->center[1] = std::stod(line.substr(std::strlen("Mask_CenterY=")));
      }
    }
    else if (startsWith(line, "Mask_CenterZ=")) {
      if (outMaskState) {
        outMaskState->center[2] = std::stod(line.substr(std::strlen("Mask_CenterZ=")));
      }
    }
    else if (startsWith(line, "Mask_Radius=")) {
      if (outMaskState) {
        outMaskState->radius = std::stod(line.substr(std::strlen("Mask_Radius=")));
      }
    }
    else if (startsWith(line, "Mask_OutsideMode=")) {
      if (outMaskState) {
        int v = std::stoi(line.substr(std::strlen("Mask_OutsideMode=")));
        if (v == 0) outMaskState->outsideMode = MaskUIState::OutsideMode::Drop;
        else if (v == 1) outMaskState->outsideMode = MaskUIState::OutsideMode::Thin;
        else outMaskState->outsideMode = MaskUIState::OutsideMode::KeepAll;
      }
    }
    else if (startsWith(line, "Mask_OutsideStride=")) {
      if (outMaskState) {
        outMaskState->outsideStride =
          (unsigned long long)std::stoull(line.substr(std::strlen("Mask_OutsideStride=")));
      }
    }
    else if (startsWith(line, "Mask_Type")) {
      // Mask_Type0=0/1/2
      if (outMaskState) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
          int type = std::stoi(line.substr(9, eq - 9));
          if (type >= 0 && type < 6) {
            int v = std::stoi(line.substr(eq + 1));
            if (v == 0) outMaskState->typeMode[type] = MaskUIState::TypeMode::Off;
            else if (v == 1) outMaskState->typeMode[type] = MaskUIState::TypeMode::On_NoThin;
            else outMaskState->typeMode[type] = MaskUIState::TypeMode::On_ThinOK;
          }
        }
      }
    }
    else if (startsWith(line, "Mask_EnableMaxParticles=")) {
      if (outMaskState) {
        outMaskState->enableMaxParticles =
          (std::stoi(line.substr(std::strlen("Mask_EnableMaxParticles="))) != 0);
      }
    }
    else if (startsWith(line, "Mask_MaxParticles=")) {
      if (outMaskState) {
        outMaskState->maxParticles =
          std::stoi(line.substr(std::strlen("Mask_MaxParticles=")));
      }
    }
    else if (startsWith(line, "NormalizationFactor=")) {
      P->desiredMax = std::stof(line.substr(std::strlen("NormalizationFactor=")));
    }
    else if (startsWith(line, "skipStep=")) {
      src.skipStep = std::stoi(line.substr(std::strlen("skipStep=")));
    }
    else if (startsWith(line, "currentStep=")) {
      src.currentStep = std::stoi(line.substr(std::strlen("currentStep=")));
    }
    else if (startsWith(line, "UnitMass_in_g=")) {
      P->units.mass_g = std::stod(line.substr(std::strlen("UnitMass_in_g=")));
    }
    else if (startsWith(line, "UnitLength_in_cm=")) {
      P->units.length_cm = std::stod(line.substr(std::strlen("UnitLength_in_cm=")));
    }
    else if (startsWith(line, "UnitVelocity_in_cm_per_s=")) {
      P->units.velocity_cm_per_s = std::stod(line.substr(std::strlen("UnitVelocity_in_cm_per_s=")));
    }
    else if (startsWith(line, "Hubble=")) {
      P->units.hubble = std::stof(line.substr(std::strlen("Hubble=")));
    }
    else if (startsWith(line, "useComovingCoordinate=")) {
      P->units.useComovingCoordinate =
        std::stof(line.substr(std::strlen("useComovingCoordinate=")));
    }
  }

  infile.close();
  return true;
}

// ------------------------------
bool saveConfig(const std::string& filename,
                const ParticleArray* P,
                const FileInfo* fileInfo,
                const ParticleVisualConfig* visualCfg,
                const MaskUIState* maskState)
{
  std::ofstream outfile(filename);
  if (!outfile.is_open()) return false;

  auto& src = fileInfo->getSource();
  outfile << "FileFormat=" << src.fileFormat << "\n";
  outfile << "FolderPath=" << src.folderPath << "\n";

  saveTokenList(outfile, "TokenCount", src.formatTokens);
  saveTokenList(outfile, "HDF5TokenCount", src.formatTokens_hdf5);

  for (int i = 0; i < 6; i++) {
    const auto& cfg = visualCfg->types[i];
    outfile << "ParticleType" << i << "_Size=" << cfg.pointSize << "\n";
    outfile << "ParticleType" << i << "_Min=" << cfg.colorMin << "\n";
    outfile << "ParticleType" << i << "_Max=" << cfg.colorMax << "\n";
    outfile << "ParticleType" << i << "_Periodic=" << (cfg.periodicColorBar ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_UseLog=" << (cfg.useLogScale ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_Hide=" << (cfg.hideParticles ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_ColormapIndex=" << cfg.colormapIndex << "\n";
    outfile << "ParticleType" << i << "_Quantity=" << quantityToString(cfg.selectedQuantity) << "\n";
  }

  if (maskState) {
    outfile << "Mask_EnableSphere=" << (maskState->enableSphere ? 1 : 0) << "\n";
    outfile << "Mask_CenterX=" << maskState->center[0] << "\n";
    outfile << "Mask_CenterY=" << maskState->center[1] << "\n";
    outfile << "Mask_CenterZ=" << maskState->center[2] << "\n";
    outfile << "Mask_Radius=" << maskState->radius << "\n";

    int outsideMode = 2;
    if (maskState->outsideMode == MaskUIState::OutsideMode::Drop) outsideMode = 0;
    else if (maskState->outsideMode == MaskUIState::OutsideMode::Thin) outsideMode = 1;
    outfile << "Mask_OutsideMode=" << outsideMode << "\n";
    outfile << "Mask_OutsideStride=" << maskState->outsideStride << "\n";

    for (int t = 0; t < 6; ++t) {
      int tm = 1;
      if (maskState->typeMode[t] == MaskUIState::TypeMode::Off) tm = 0;
      else if (maskState->typeMode[t] == MaskUIState::TypeMode::On_ThinOK) tm = 2;
      outfile << "Mask_Type" << t << "=" << tm << "\n";
    }

    outfile << "Mask_EnableMaxParticles=" << (maskState->enableMaxParticles ? 1 : 0) << "\n";
    outfile << "Mask_MaxParticles=" << maskState->maxParticles << "\n";
  }

  outfile << "NormalizationFactor=" << P->desiredMax << "\n";
  outfile << "skipStep=" << src.skipStep << "\n";
  outfile << "currentStep=" << src.currentStep << "\n";
  outfile << "UnitMass_in_g=" << P->units.mass_g << "\n";
  outfile << "UnitLength_in_cm=" << P->units.length_cm << "\n";
  outfile << "UnitVelocity_in_cm_per_s=" << P->units.length_cm << "\n";
  outfile << "Hubble=" << P->units.hubble << "\n";
  outfile << "useComovingCoordinate=" << P->units.useComovingCoordinate << "\n";

  outfile.close();
  return true;
}
