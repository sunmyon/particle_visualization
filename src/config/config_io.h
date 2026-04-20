#pragma once
#include <string>

struct ConfigData;
bool LoadConfigFile(const std::string& filename, ConfigData& outConfig);
bool SaveConfigFile(const std::string& filename, const ConfigData& config);
