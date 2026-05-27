#include "config_io.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "config_data.h"

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
            << tokens[i].count << ","
            << tokens[i].sourceName << ","
            << static_cast<unsigned int>(tokens[i].typeMask) << "\n";
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
    std::string tokenLabel, tokenType, tokenCountStr, sourceName, typeMaskStr;
    if (!std::getline(iss, tokenLabel, ',')) continue;
    if (!std::getline(iss, tokenType, ',')) continue;
    if (!std::getline(iss, tokenCountStr, ',')) continue;
    std::getline(iss, sourceName, ',');
    std::getline(iss, typeMaskStr);

    FieldSpec ft;
    ft.key   = GetFieldKeyFromDisplayName(trim(tokenLabel));
    ft.type  = stringToDataType(trim(tokenType));
    ft.count = std::stoi(trim(tokenCountStr));
    sourceName = trim(sourceName);
    ft.sourceName = sourceName.empty()
      ? GetDefaultHDF5SourceName(ft.key)
      : sourceName;
    typeMaskStr = trim(typeMaskStr);
    if (!typeMaskStr.empty()) {
      try {
        ft.typeMask =
          static_cast<std::uint8_t>(std::stoul(typeMaskStr) & 0x3fu);
      } catch (...) {
        ft.typeMask = 0x3fu;
      }
    }
    if ((ft.typeMask & 0x3fu) == 0) {
      ft.typeMask = 0x3fu;
    }

    outTokens.push_back(ft);
  }
}

static void saveOutputFieldList(std::ofstream& outfile,
                                const SnapshotOutputFormatConfig& output)
{
  outfile << "OutputFormatEnabled=" << (output.enabled ? 1 : 0) << "\n";
  outfile << "OutputFieldCount=" << output.fields.size() << "\n";
  for (const SnapshotOutputFieldSpec& field : output.fields) {
    outfile << GetFieldKeyDisplayName(field.key) << ","
            << dataTypeToString(field.type) << ","
            << field.count << ","
            << field.outputName << ","
            << static_cast<int>(field.missingPolicy) << ","
            << field.typeMask << ","
            << field.sourceName << ",";
    for (std::size_t i = 0; i < field.defaultValues.size(); ++i) {
      if (i > 0) outfile << ";";
      outfile << field.defaultValues[i];
    }
    outfile << "\n";
  }
}

static void loadOutputFieldList(std::ifstream& infile,
                                int fieldCount,
                                std::vector<SnapshotOutputFieldSpec>& outFields)
{
  outFields.clear();
  outFields.reserve(std::max(0, fieldCount));

  std::string line;
  for (int i = 0; i < fieldCount; ++i) {
    if (!std::getline(infile, line)) break;

    std::istringstream iss(line);
    std::string keyText, typeText, countText, outputName, policyText, maskText;
    std::string restText;
    if (!std::getline(iss, keyText, ',')) continue;
    if (!std::getline(iss, typeText, ',')) continue;
    if (!std::getline(iss, countText, ',')) continue;
    if (!std::getline(iss, outputName, ',')) continue;
    if (!std::getline(iss, policyText, ',')) continue;
    if (!std::getline(iss, maskText, ',')) continue;
    std::getline(iss, restText);

    SnapshotOutputFieldSpec field;
    field.key = GetFieldKeyFromDisplayName(trim(keyText));
    field.type = stringToDataType(trim(typeText));
    field.count = std::max(1, std::stoi(trim(countText)));
    field.outputName = trim(outputName);
    const int policy = std::stoi(trim(policyText));
    if (policy >= static_cast<int>(SnapshotOutputMissingPolicy::Omit) &&
        policy <= static_cast<int>(SnapshotOutputMissingPolicy::Require)) {
      field.missingPolicy = static_cast<SnapshotOutputMissingPolicy>(policy);
    }
    field.typeMask = static_cast<unsigned int>(std::stoul(trim(maskText))) & 0x3fu;

    std::string defaultsText = restText;
    const std::size_t comma = restText.find(',');
    if (comma != std::string::npos) {
      field.sourceName = trim(restText.substr(0, comma));
      defaultsText = restText.substr(comma + 1);
    }

    std::stringstream defaults(defaultsText);
    std::string valueText;
    while (std::getline(defaults, valueText, ';')) {
      valueText = trim(valueText);
      if (!valueText.empty()) {
        field.defaultValues.push_back(std::stod(valueText));
      }
    }
    outFields.push_back(std::move(field));
  }
}

static const char* quantityToString(QuantityId q) {
  return QuantityLabel(q);
}

// Convert to a map later if needed.
static QuantityId quantityFromString(const std::string& s) {
  for (int i = 0; i < 1024; ++i) {
    QuantityId q = static_cast<QuantityId>(i);
    const char* label = QuantityLabel(q);
    if (label && s == label) return q;
  }
  return QuantityId::Density;
}

// ------------------------------
bool LoadConfigFile(const std::string& filename, ConfigData& outConfig)
{
  std::ifstream infile(filename);
  if (!infile.is_open()) return false;

  outConfig = ConfigData{};

  std::string line;
  while (std::getline(infile, line)) {
    line = trim(line);
    if (line.empty()) continue;

    if (startsWith(line, "FileFormat=")) {
      outConfig.persistent.fileFormatPattern =
        line.substr(std::strlen("FileFormat="));
    }
    else if (startsWith(line, "FolderPath=")) {
      outConfig.persistent.folderPath =
        line.substr(std::strlen("FolderPath="));
    }
    else if (startsWith(line, "ReadFormat=")) {
      const int format = std::stoi(line.substr(std::strlen("ReadFormat=")));
      if (format >= 0 && format < static_cast<int>(FileFormat::_Count)) {
        outConfig.persistent.readFormat = static_cast<FileFormat>(format);
      }
    }
    else if (startsWith(line, "TokenCount=")) {
      int tokenCount = std::stoi(line.substr(std::strlen("TokenCount=")));
      loadTokenList(infile, tokenCount, outConfig.persistent.formatTokens);
    }
    else if (startsWith(line, "HDF5TokenCount=")) {
      int tokenCount = std::stoi(line.substr(std::strlen("HDF5TokenCount=")));
      loadTokenList(infile, tokenCount, outConfig.persistent.formatTokensHdf5);
    }
    else if (startsWith(line, "GadgetTokenCount=")) {
      int tokenCount = std::stoi(line.substr(std::strlen("GadgetTokenCount=")));
      loadTokenList(infile, tokenCount, outConfig.persistent.formatTokensGadget);
    }
    else if (startsWith(line, "CustomScalarLabel")) {
      const size_t eq = line.find('=');
      if (eq != std::string::npos) {
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const std::string prefix = "CustomScalarLabel";
        try {
          const int index = std::stoi(key.substr(prefix.size())) - 1;
          if (index >= 0 && index < kCustomScalarFieldCount) {
            outConfig.persistent.customScalarLabels[
              static_cast<std::size_t>(index)] = value;
          }
        } catch (...) {
        }
      }
    }
    else if (startsWith(line, "OutputFormatEnabled=")) {
      outConfig.persistent.outputFormat.enabled =
        std::stoi(line.substr(std::strlen("OutputFormatEnabled="))) != 0;
    }
    else if (startsWith(line, "OutputFieldCount=")) {
      int fieldCount = std::stoi(line.substr(std::strlen("OutputFieldCount=")));
      loadOutputFieldList(infile,
                          fieldCount,
                          outConfig.persistent.outputFormat.fields);
    }
    else if (startsWith(line, "InputDensityUnit=")) {
      const int unit = std::stoi(line.substr(std::strlen("InputDensityUnit=")));
      if (unit >= static_cast<int>(InputDensityUnit::CodeMassDensity) &&
          unit <= static_cast<int>(InputDensityUnit::MassDensityCgs)) {
        outConfig.persistent.inputDensityUnit =
          static_cast<InputDensityUnit>(unit);
      }
    }
    else if (startsWith(line, "InputTemperatureUnit=")) {
      const int unit =
        std::stoi(line.substr(std::strlen("InputTemperatureUnit=")));
      if (unit >= static_cast<int>(InputTemperatureUnit::Kelvin) &&
          unit <= static_cast<int>(InputTemperatureUnit::CodeInternalEnergy)) {
        outConfig.persistent.inputTemperatureUnit =
          static_cast<InputTemperatureUnit>(unit);
      }
    }
    else if (startsWith(line, "InputMagneticFieldUnit=")) {
      const int unit =
        std::stoi(line.substr(std::strlen("InputMagneticFieldUnit=")));
      if (unit >= static_cast<int>(InputMagneticFieldUnit::Gauss) &&
          unit <= static_cast<int>(InputMagneticFieldUnit::CodeMagneticField)) {
        outConfig.persistent.inputMagneticFieldUnit =
          static_cast<InputMagneticFieldUnit>(unit);
      }
    }
    else if (startsWith(line, "ParticleType")) {
      // ParticleType0_Size=...
      size_t pos = line.find('_');
      if (pos == std::string::npos) continue;

      std::string typeStr = line.substr(12, pos - 12);
      int type = std::stoi(typeStr);
      if (type < 0 || type >= 6) continue;

      std::string key = line.substr(pos + 1);
      auto& cfg = outConfig.persistent.visual.types[type];

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
      outConfig.persistent.mask.enableSphere =
        (std::stoi(line.substr(std::strlen("Mask_EnableSphere="))) != 0);
    }
    else if (startsWith(line, "Mask_CenterX=")) {
      outConfig.persistent.mask.center[0] =
        std::stod(line.substr(std::strlen("Mask_CenterX=")));
    }
    else if (startsWith(line, "Mask_CenterY=")) {
      outConfig.persistent.mask.center[1] =
        std::stod(line.substr(std::strlen("Mask_CenterY=")));
    }
    else if (startsWith(line, "Mask_CenterZ=")) {
      outConfig.persistent.mask.center[2] =
        std::stod(line.substr(std::strlen("Mask_CenterZ=")));
    }
    else if (startsWith(line, "Mask_Radius=")) {
      outConfig.persistent.mask.radius =
        std::stod(line.substr(std::strlen("Mask_Radius=")));
    }
    else if (startsWith(line, "Mask_OutsideMode=")) {
      int v = std::stoi(line.substr(std::strlen("Mask_OutsideMode=")));
      if (v == 0) outConfig.persistent.mask.outsideMode = ParticleMaskConfig::OutsideMode::Drop;
      else if (v == 1) outConfig.persistent.mask.outsideMode = ParticleMaskConfig::OutsideMode::Thin;
      else outConfig.persistent.mask.outsideMode = ParticleMaskConfig::OutsideMode::KeepAll;
    }
    else if (startsWith(line, "Mask_OutsideStride=")) {
      outConfig.persistent.mask.outsideStride =
        static_cast<int>(std::stoull(line.substr(std::strlen("Mask_OutsideStride="))));
    }
    else if (startsWith(line, "Mask_Type")) {
      // Mask_Type0=0/1/2
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        int type = std::stoi(line.substr(9, eq - 9));
        if (type >= 0 && type < 6) {
          int v = std::stoi(line.substr(eq + 1));
          if (v == 0) outConfig.persistent.mask.typeMode[type] = ParticleMaskConfig::TypeMode::Off;
          else if (v == 1) outConfig.persistent.mask.typeMode[type] = ParticleMaskConfig::TypeMode::On_NoThin;
          else outConfig.persistent.mask.typeMode[type] = ParticleMaskConfig::TypeMode::On_ThinOK;
        }
      }
    }
    else if (startsWith(line, "Mask_EnableMaxParticles=")) {
      outConfig.persistent.mask.enableMaxParticles =
        (std::stoi(line.substr(std::strlen("Mask_EnableMaxParticles="))) != 0);
    }
    else if (startsWith(line, "Mask_MaxParticles=")) {
      outConfig.persistent.mask.maxParticles =
        std::stoi(line.substr(std::strlen("Mask_MaxParticles=")));
    }
    else if (startsWith(line, "NormalizationFactor=")) {
      outConfig.persistent.desiredMax =
        std::stof(line.substr(std::strlen("NormalizationFactor=")));
    }
    else if (startsWith(line, "initialIndex=")) {
      outConfig.session.initialIndex = std::stoi(line.substr(std::strlen("initialIndex=")));
    }
    else if (startsWith(line, "currentFileIndex=")) {
      outConfig.session.currentFileIndex = std::stoi(line.substr(std::strlen("currentFileIndex=")));
    }
    else if (startsWith(line, "skipStep=")) {
      outConfig.session.skipStep = std::stoi(line.substr(std::strlen("skipStep=")));
    }
    else if (startsWith(line, "currentStep=")) {
      outConfig.session.currentStep = std::stoi(line.substr(std::strlen("currentStep=")));
    }
    else if (startsWith(line, "batchSize=")) {
      outConfig.session.batchSize = std::stoi(line.substr(std::strlen("batchSize=")));
    }
    else if (startsWith(line, "UnitMass_in_g=")) {
      outConfig.persistent.units.mass_g =
        std::stod(line.substr(std::strlen("UnitMass_in_g=")));
    }
    else if (startsWith(line, "UnitLength_in_cm=")) {
      outConfig.persistent.units.length_cm =
        std::stod(line.substr(std::strlen("UnitLength_in_cm=")));
    }
    else if (startsWith(line, "UnitVelocity_in_cm_per_s=")) {
      outConfig.persistent.units.velocity_cm_per_s =
        std::stod(line.substr(std::strlen("UnitVelocity_in_cm_per_s=")));
    }
    else if (startsWith(line, "Hubble=")) {
      outConfig.persistent.units.hubble =
        std::stof(line.substr(std::strlen("Hubble=")));
    }
    else if (startsWith(line, "useComovingCoordinate=")) {
      outConfig.persistent.units.useComovingCoordinate =
        std::stof(line.substr(std::strlen("useComovingCoordinate=")));
    }
  }

  return true;
}

// ------------------------------
bool SaveConfigFile(const std::string& filename, const ConfigData& config)
{
  std::ofstream outfile(filename);
  if (!outfile.is_open()) return false;

  outfile << "FileFormat=" << config.persistent.fileFormatPattern << "\n";
  outfile << "FolderPath=" << config.persistent.folderPath << "\n";
  outfile << "ReadFormat=" << static_cast<int>(config.persistent.readFormat) << "\n";

  saveTokenList(outfile, "TokenCount", config.persistent.formatTokens);
  saveTokenList(outfile, "HDF5TokenCount", config.persistent.formatTokensHdf5);
  saveTokenList(outfile, "GadgetTokenCount", config.persistent.formatTokensGadget);
  for (int i = 0; i < kCustomScalarFieldCount; ++i) {
    outfile << "CustomScalarLabel" << (i + 1) << "="
            << config.persistent.customScalarLabels[static_cast<std::size_t>(i)]
            << "\n";
  }
  saveOutputFieldList(outfile, config.persistent.outputFormat);
  outfile << "InputDensityUnit="
          << static_cast<int>(config.persistent.inputDensityUnit) << "\n";
  outfile << "InputTemperatureUnit="
          << static_cast<int>(config.persistent.inputTemperatureUnit) << "\n";
  outfile << "InputMagneticFieldUnit="
          << static_cast<int>(config.persistent.inputMagneticFieldUnit) << "\n";

  for (int i = 0; i < 6; i++) {
    const auto& cfg = config.persistent.visual.types[i];
    outfile << "ParticleType" << i << "_Size=" << cfg.pointSize << "\n";
    outfile << "ParticleType" << i << "_Min=" << cfg.colorMin << "\n";
    outfile << "ParticleType" << i << "_Max=" << cfg.colorMax << "\n";
    outfile << "ParticleType" << i << "_Periodic=" << (cfg.periodicColorBar ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_UseLog=" << (cfg.useLogScale ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_Hide=" << (cfg.hideParticles ? 1 : 0) << "\n";
    outfile << "ParticleType" << i << "_ColormapIndex=" << cfg.colormapIndex << "\n";
    outfile << "ParticleType" << i << "_Quantity=" << quantityToString(cfg.selectedQuantity) << "\n";
  }

  const auto& maskState = config.persistent.mask;
  outfile << "Mask_EnableSphere=" << (maskState.enableSphere ? 1 : 0) << "\n";
  outfile << "Mask_CenterX=" << maskState.center[0] << "\n";
  outfile << "Mask_CenterY=" << maskState.center[1] << "\n";
  outfile << "Mask_CenterZ=" << maskState.center[2] << "\n";
  outfile << "Mask_Radius=" << maskState.radius << "\n";

  int outsideMode = 2;
  if (maskState.outsideMode == ParticleMaskConfig::OutsideMode::Drop) outsideMode = 0;
  else if (maskState.outsideMode == ParticleMaskConfig::OutsideMode::Thin) outsideMode = 1;
  outfile << "Mask_OutsideMode=" << outsideMode << "\n";
  outfile << "Mask_OutsideStride=" << maskState.outsideStride << "\n";

  for (int t = 0; t < 6; ++t) {
    int tm = 1;
    if (maskState.typeMode[t] == ParticleMaskConfig::TypeMode::Off) tm = 0;
    else if (maskState.typeMode[t] == ParticleMaskConfig::TypeMode::On_ThinOK) tm = 2;
    outfile << "Mask_Type" << t << "=" << tm << "\n";
  }

  outfile << "Mask_EnableMaxParticles=" << (maskState.enableMaxParticles ? 1 : 0) << "\n";
  outfile << "Mask_MaxParticles=" << maskState.maxParticles << "\n";

  outfile << "NormalizationFactor=" << config.persistent.desiredMax << "\n";
  outfile << "initialIndex=" << config.session.initialIndex << "\n";
  outfile << "currentFileIndex=" << config.session.currentFileIndex << "\n";
  outfile << "skipStep=" << config.session.skipStep << "\n";
  outfile << "currentStep=" << config.session.currentStep << "\n";
  outfile << "batchSize=" << config.session.batchSize << "\n";
  outfile << "UnitMass_in_g=" << config.persistent.units.mass_g << "\n";
  outfile << "UnitLength_in_cm=" << config.persistent.units.length_cm << "\n";
  outfile << "UnitVelocity_in_cm_per_s=" << config.persistent.units.velocity_cm_per_s << "\n";
  outfile << "Hubble=" << config.persistent.units.hubble << "\n";
  outfile << "useComovingCoordinate=" << config.persistent.units.useComovingCoordinate << "\n";

  return true;
}
