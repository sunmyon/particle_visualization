#pragma once

#include "config/config_data.h"

#include <string>
#include <vector>

struct ConfigValidationIssue {
  enum class Severity {
    Warning,
    Error
  };

  Severity severity = Severity::Warning;
  std::string field;
  std::string message;
};

using ConfigValidationIssues = std::vector<ConfigValidationIssue>;

bool ValidateConfigData(const ConfigData& config,
                        ConfigValidationIssues* issues = nullptr);

void SanitizeConfigData(ConfigData& config,
                        ConfigValidationIssues* issues = nullptr);

void PrintConfigValidationIssues(const ConfigValidationIssues& issues);
