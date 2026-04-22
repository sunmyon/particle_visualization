#include "config/config_validation.h"

#include <cmath>
#include <iostream>

namespace {
  void AddIssue(ConfigValidationIssues* issues,
		ConfigValidationIssue::Severity severity,
		const std::string& field,
		const std::string& message)
  {
    if (!issues) return;

    issues->push_back(ConfigValidationIssue{
	severity,
	field,
	message
      });
  }

  bool IsFinitePositive(double value)
  {
    return std::isfinite(value) && value > 0.0;
  }

  bool IsFiniteNonNegative(double value)
  {
    return std::isfinite(value) && value >= 0.0;
  }
} // namespace

bool ValidateConfigData(const ConfigData& config,
                        ConfigValidationIssues* issues)
{
  bool ok = true;

  const auto& session = config.session;
  const auto& persistent = config.persistent;

  if (session.skipStep <= 0) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "session.skipStep",
             "skipStep must be greater than zero.");
  }

  if (session.batchSize < 1) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "session.batchSize",
             "batchSize must be at least one.");
  }

  if (session.currentStep < 0) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "session.currentStep",
             "currentStep must be non-negative.");
  }

  if (!std::isfinite(persistent.desiredMax) || persistent.desiredMax <= 0.0f) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.desiredMax",
             "desiredMax must be finite and greater than zero.");
  }

  if (!IsFinitePositive(persistent.units.mass_g)) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.units.mass_g",
             "Unit mass must be finite and greater than zero.");
  }

  if (!IsFinitePositive(persistent.units.length_cm)) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.units.length_cm",
             "Unit length must be finite and greater than zero.");
  }

  if (!IsFinitePositive(persistent.units.velocity_cm_per_s)) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.units.velocity_cm_per_s",
             "Unit velocity must be finite and greater than zero.");
  }

  if (!IsFinitePositive(persistent.units.hubble)) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.units.hubble",
             "Hubble parameter must be finite and greater than zero.");
  }

  if (!IsFiniteNonNegative(persistent.mask.radius)) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.mask.radius",
             "Mask radius must be finite and non-negative.");
  }

  if (persistent.mask.maxParticles < 0) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.mask.maxParticles",
             "maxParticles must be non-negative.");
  }

  if (persistent.mask.outsideStride < 1) {
    ok = false;
    AddIssue(issues, ConfigValidationIssue::Severity::Error,
             "persistent.mask.outsideStride",
             "outsideStride must be at least one.");
  }

  for (int i = 0; i < 6; ++i) {
    const auto& visual = persistent.visual.types[i];

    if (!std::isfinite(visual.pointSize) || visual.pointSize <= 0.0f) {
      ok = false;
      AddIssue(issues, ConfigValidationIssue::Severity::Error,
               "persistent.visual.types[].pointSize",
               "Particle point size must be finite and greater than zero.");
    }

    if (!std::isfinite(visual.colorMin) || !std::isfinite(visual.colorMax) ||
        visual.colorMin >= visual.colorMax) {
      ok = false;
      AddIssue(issues, ConfigValidationIssue::Severity::Error,
               "persistent.visual.types[].colorRange",
               "Color range must be finite and colorMin < colorMax.");
    }

    if (visual.colormapIndex < 0) {
      ok = false;
      AddIssue(issues, ConfigValidationIssue::Severity::Error,
               "persistent.visual.types[].colormapIndex",
               "Colormap index must be non-negative.");
    }
  }

  if (persistent.fileFormatPattern.empty()) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.fileFormatPattern",
             "File format pattern is empty.");
  }

  if (persistent.folderPath.empty()) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.folderPath",
             "Folder path is empty.");
  }

  return ok;
}

void SanitizeConfigData(ConfigData& config,
                        ConfigValidationIssues* issues)
{
  auto& session = config.session;
  auto& persistent = config.persistent;

  if (session.skipStep <= 0) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "session.skipStep",
             "Invalid skipStep was replaced with 1.");
    session.skipStep = 1;
  }

  if (session.batchSize < 1) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "session.batchSize",
             "Invalid batchSize was replaced with 1.");
    session.batchSize = 1;
  }

  if (session.currentStep < 0) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "session.currentStep",
             "Negative currentStep was replaced with 0.");
    session.currentStep = 0;
  }

  const int expectedCurrentFileIndex =
    session.initialIndex + session.currentStep * session.skipStep;

  if (session.currentFileIndex != expectedCurrentFileIndex) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "session.currentFileIndex",
             "currentFileIndex was recomputed from initialIndex, currentStep, and skipStep.");
    session.currentFileIndex = expectedCurrentFileIndex;
  }

  if (!std::isfinite(persistent.desiredMax) || persistent.desiredMax <= 0.0f) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.desiredMax",
             "Invalid desiredMax was replaced with 1.");
    persistent.desiredMax = 1.0f;
  }

  if (!IsFinitePositive(persistent.units.mass_g)) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.units.mass_g",
             "Invalid unit mass was replaced with 1.");
    persistent.units.mass_g = 1.0;
  }

  if (!IsFinitePositive(persistent.units.length_cm)) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.units.length_cm",
             "Invalid unit length was replaced with 1.");
    persistent.units.length_cm = 1.0;
  }

  if (!IsFinitePositive(persistent.units.velocity_cm_per_s)) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.units.velocity_cm_per_s",
             "Invalid unit velocity was replaced with 1.");
    persistent.units.velocity_cm_per_s = 1.0;
  }

  if (!IsFinitePositive(persistent.units.hubble)) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.units.hubble",
             "Invalid Hubble parameter was replaced with 1.");
    persistent.units.hubble = 1.0;
  }

  if (!IsFiniteNonNegative(persistent.mask.radius)) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.mask.radius",
             "Invalid mask radius was replaced with 0 and sphere mask was disabled.");
    persistent.mask.radius = 0.0;
    persistent.mask.enableSphere = false;
  }

  if (persistent.mask.maxParticles < 0) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.mask.maxParticles",
             "Negative maxParticles was replaced with 0.");
    persistent.mask.maxParticles = 0;
  }

  if (persistent.mask.enableMaxParticles && persistent.mask.maxParticles == 0) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.mask.enableMaxParticles",
             "Max-particle mask was disabled because maxParticles is zero.");
    persistent.mask.enableMaxParticles = false;
  }

  if (persistent.mask.outsideStride < 1) {
    AddIssue(issues, ConfigValidationIssue::Severity::Warning,
             "persistent.mask.outsideStride",
             "Invalid outsideStride was replaced with 1.");
    persistent.mask.outsideStride = 1;
  }

  for (int i = 0; i < 6; ++i) {
    auto& visual = persistent.visual.types[i];

    if (!std::isfinite(visual.pointSize) || visual.pointSize <= 0.0f) {
      AddIssue(issues, ConfigValidationIssue::Severity::Warning,
               "persistent.visual.types[].pointSize",
               "Invalid point size was replaced with 1.");
      visual.pointSize = 1.0f;
    }

    if (!std::isfinite(visual.colorMin)) {
      AddIssue(issues, ConfigValidationIssue::Severity::Warning,
               "persistent.visual.types[].colorMin",
               "Invalid colorMin was replaced with 0.");
      visual.colorMin = 0.0f;
    }

    if (!std::isfinite(visual.colorMax)) {
      AddIssue(issues, ConfigValidationIssue::Severity::Warning,
               "persistent.visual.types[].colorMax",
               "Invalid colorMax was replaced with colorMin + 1.");
      visual.colorMax = visual.colorMin + 1.0f;
    }

    if (visual.colorMin >= visual.colorMax) {
      AddIssue(issues, ConfigValidationIssue::Severity::Warning,
               "persistent.visual.types[].colorRange",
               "Invalid color range was expanded to colorMin + 1.");
      visual.colorMax = visual.colorMin + 1.0f;
    }

    if (visual.colormapIndex < 0) {
      AddIssue(issues, ConfigValidationIssue::Severity::Warning,
               "persistent.visual.types[].colormapIndex",
               "Negative colormap index was replaced with 0.");
      visual.colormapIndex = 0;
    }
  }
}

void PrintConfigValidationIssues(const ConfigValidationIssues& issues)
{
  for (const auto& issue : issues) {
    const char* severity =
      issue.severity == ConfigValidationIssue::Severity::Error
        ? "error"
        : "warning";

    std::cerr << "[config " << severity << "] "
              << issue.field << ": "
              << issue.message << "\n";
  }
}

