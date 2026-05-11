#include "UI/file_format_dialog.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#ifdef HAVE_HDF5
#include "FileIO/hdf5_utils.h"
#endif

namespace {

float TypeMaskColumnWidth()
{
  const ImGuiStyle& style = ImGui::GetStyle();
  float width = 0.0f;
  for (int type = 0; type < 6; ++type) {
    char label[8];
    std::snprintf(label, sizeof(label), "%d", type);
    width += ImGui::CalcTextSize(label).x + 2.0f * style.FramePadding.x;
    if (type > 0) {
      width += 4.0f;
    }
  }
  return width + style.CellPadding.x;
}

void SetupTypeMaskColumn()
{
  ImGui::TableSetupColumn("Types",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          TypeMaskColumnWidth());
}

float DataTypeColumnWidth()
{
  const ImGuiStyle& style = ImGui::GetStyle();
  float textWidth = 0.0f;
  for (int n = 0; n < kNumDataTypeChoices; ++n) {
    textWidth =
      std::max(textWidth, ImGui::CalcTextSize(kDataTypeChoices[n].name).x);
  }
  return textWidth + 2.0f * style.FramePadding.x +
         2.0f * style.CellPadding.x + ImGui::GetFrameHeight();
}


bool IsFixedGadgetBlock(FieldKey key)
{
  return key == FieldKey::Position ||
         key == FieldKey::Velocity ||
         key == FieldKey::ID ||
         key == FieldKey::Mass ||
         key == FieldKey::Type;
}

bool IsGadgetIdLikeBlock(FieldKey key)
{
  return key == FieldKey::ID || key == FieldKey::Type;
}

bool IsGadgetFloatingBlock(FieldKey key)
{
  return key != FieldKey::Dummy && !IsGadgetIdLikeBlock(key);
}

int GadgetDomainIndexOr(const std::string& sourceName, int fallback)
{
  const size_t sep = sourceName.find(':');
  const std::string domain = sep == std::string::npos
    ? sourceName
    : sourceName.substr(0, sep);
  if (domain == "all") return 0;
  if (domain == "type0") return 1;
  if (domain == "type0and5") return 2;
  if (domain == "absolute") return 3;
  return fallback;
}

std::uint8_t GadgetDomainTypeMask(int domainIndex)
{
  switch (domainIndex) {
  case 0: return 0x3fu;
  case 1: return 0x01u;
  case 2: return 0x21u;
  case 3: return 0x01u;
  default: return 0x01u;
  }
}

int GadgetBlockRepeat(const std::string& sourceName)
{
  const size_t sep = sourceName.find(':');
  if (sep == std::string::npos || sep + 1 >= sourceName.size()) return 1;
  try {
    return std::max(1, std::stoi(sourceName.substr(sep + 1)));
  } catch (...) {
    return 1;
  }
}

void SetGadgetTypeSource(FieldSpec& spec, int repeat)
{
  repeat = std::max(1, repeat);
  spec.sourceName = "types:" + std::to_string(repeat);
}

void ApplyGadgetBlockDefaults(FieldSpec& spec)
{
  if (spec.key == FieldKey::Dummy) {
    spec.type = DataType::Float;
    spec.count = 1;
    spec.typeMask = 0x3fu;
    spec.sourceName = "types:1";
    return;
  }
  ApplyDefaultFieldSpec(spec);
  if (IsFixedGadgetBlock(spec.key)) {
    spec.typeMask = 0x3fu;
  } else {
    spec.typeMask = 0x01u;
  }
  spec.sourceName = "types:1";
}

void SetAllGadgetFloatingTypes(std::vector<FieldSpec>& tokens, DataType type)
{
  for (FieldSpec& spec : tokens) {
    if (!IsGadgetIdLikeBlock(spec.key)) {
      spec.type = type;
    }
  }
}

bool DrawGadgetTypeCombo(FieldSpec& spec)
{
  if (!ImGui::BeginCombo("##type", GetDataTypeDisplayName(spec.type))) {
    return false;
  }

  bool changed = false;
  for (int n = 0; n < kNumDataTypeChoices; ++n) {
    const DataType type = kDataTypeChoices[n].type;

    if (IsGadgetFloatingBlock(spec.key) &&
        type != DataType::Float &&
        type != DataType::Double) {
      continue;
    }
    if (IsGadgetIdLikeBlock(spec.key) &&
        type != DataType::Int32 &&
        type != DataType::Int64) {
      continue;
    }

    const bool selected = spec.type == type;
    if (ImGui::Selectable(kDataTypeChoices[n].name, selected)) {
      spec.type = type;
      changed = true;
    }
    if (selected) ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
  return changed;
}

void DrawTypeMaskButtons(unsigned int& mask, const char* idPrefix)
{
  ImGui::PushID(idPrefix);
  for (int type = 0; type < 6; ++type) {
    const bool enabled =
      (mask & static_cast<unsigned int>(1u << type)) != 0;
    if (type > 0) {
      ImGui::SameLine(0.0f, 4.0f);
    }

    if (enabled) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    char label[8];
    std::snprintf(label, sizeof(label), "%d", type);
    if (ImGui::SmallButton(label)) {
      const unsigned int bit = static_cast<unsigned int>(1u << type);
      if (enabled) {
        mask &= ~bit;
      } else {
        mask |= bit;
      }
    }
    if (enabled) {
      ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("PartType%d", type);
    }
  }
  ImGui::PopID();
}

void DrawFieldTypeMaskCheckboxes(FieldSpec& spec, const char* idPrefix)
{
  unsigned int mask = spec.typeMask;
  DrawTypeMaskButtons(mask, idPrefix);
  spec.typeMask = static_cast<std::uint8_t>(mask & 0x3fu);
}

void DrawGadgetTypeMaskEditor(FieldSpec& spec)
{
  const int legacyDomain = GadgetDomainIndexOr(spec.sourceName, -1);
  if (legacyDomain >= 0) {
    spec.typeMask = GadgetDomainTypeMask(legacyDomain);
  }

  const bool fixed = IsFixedGadgetBlock(spec.key);
  if (fixed) {
    spec.typeMask = 0x3fu;
    ImGui::BeginDisabled();
    DrawFieldTypeMaskCheckboxes(spec, "gadget_fixed_types");
    ImGui::EndDisabled();
    return;
  }

  DrawFieldTypeMaskCheckboxes(spec, "gadget_types");
  SetGadgetTypeSource(spec, GadgetBlockRepeat(spec.sourceName));
}

bool HandleGadgetIndexDragDrop(std::vector<FieldSpec>& tokens, int rowIndex)
{
  char label[32];
  std::snprintf(label, sizeof(label), "%d", rowIndex + 1);
  const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
  ImGui::Button(label, ImVec2(width, 0.0f));

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Drag to reorder");
  }

  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    ImGui::SetDragDropPayload("DND_GADGET_FORMAT_TOKEN",
                              &rowIndex,
                              sizeof(rowIndex));
    const FieldSpec& spec = tokens[rowIndex];
    ImGui::Text("Moving %s",
                spec.key == FieldKey::Dummy
                  ? "skip/dummy"
                  : GetFieldKeyDisplayName(spec.key));
    ImGui::EndDragDropSource();
  }

  bool reordered = false;
  if (ImGui::BeginDragDropTarget()) {
    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();
    float midY = (itemMin.y + itemMax.y) * 0.5f;

    const ImGuiPayload* payload =
      ImGui::AcceptDragDropPayload("DND_GADGET_FORMAT_TOKEN");
    int dropIndex = rowIndex;
    if (ImGui::GetIO().MousePos.y > midY) {
      dropIndex = rowIndex + 1;
    }

    if (payload) {
      IM_ASSERT(payload->DataSize == sizeof(int));
      int srcIndex = *static_cast<const int*>(payload->Data);
      if (srcIndex >= 0 &&
          srcIndex < static_cast<int>(tokens.size()) &&
          srcIndex != dropIndex &&
          srcIndex != dropIndex - 1) {
        FieldSpec token = tokens[srcIndex];
        tokens.erase(tokens.begin() + srcIndex);
        if (srcIndex < dropIndex) {
          --dropIndex;
        }
        dropIndex = std::clamp(dropIndex, 0, static_cast<int>(tokens.size()));
        tokens.insert(tokens.begin() + dropIndex, token);
        reordered = true;
      }
    }

    ImGui::EndDragDropTarget();
  }

  return reordered;
}

const char* MissingPolicyName(SnapshotOutputMissingPolicy policy)
{
  switch (policy) {
  case SnapshotOutputMissingPolicy::Omit:
    return "omit";
  case SnapshotOutputMissingPolicy::FillDefault:
    return "fill default";
  case SnapshotOutputMissingPolicy::Require:
    return "require";
  }
  return "omit";
}

void ApplyOutputFieldDefaults(SnapshotOutputFieldSpec& spec)
{
  FieldSpec defaults;
  defaults.key = spec.key;
  ApplyDefaultFieldSpec(defaults);
  spec.type = spec.key == FieldKey::ID ? DataType::Int64 : DataType::Double;
  spec.count = std::max(1, defaults.count);
  spec.outputName = GetDefaultHDF5SourceName(spec.key);
  if (spec.outputName == "unknown" || spec.outputName == "dummy") {
    spec.outputName.clear();
  }
  spec.defaultValues.assign(static_cast<std::size_t>(spec.count), 0.0);
  switch (spec.key) {
  case FieldKey::Position:
  case FieldKey::Velocity:
  case FieldKey::ID:
  case FieldKey::Mass:
    spec.typeMask = 0x3fu;
    break;
  default:
    spec.typeMask = 0x01u;
    break;
  }
}

std::string DefaultValuesText(const SnapshotOutputFieldSpec& spec)
{
  std::string text;
  for (std::size_t i = 0; i < spec.defaultValues.size(); ++i) {
    if (i > 0) text += ",";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.8g", spec.defaultValues[i]);
    text += buf;
  }
  return text;
}

void ParseDefaultValues(const char* text, SnapshotOutputFieldSpec& spec)
{
  spec.defaultValues.clear();
  std::stringstream ss(text ? text : "");
  std::string part;
  while (std::getline(ss, part, ',')) {
    try {
      spec.defaultValues.push_back(std::stod(part));
    } catch (...) {
    }
  }
  const std::size_t count = static_cast<std::size_t>(std::max(1, spec.count));
  if (spec.defaultValues.empty()) {
    spec.defaultValues.assign(count, 0.0);
  } else {
    while (spec.defaultValues.size() < count) {
      spec.defaultValues.push_back(spec.defaultValues.back());
    }
    if (spec.defaultValues.size() > count) {
      spec.defaultValues.resize(count);
    }
  }
}

bool HandleBinaryIndexDragDrop(std::vector<FieldSpec>& tokens, int row)
{
  char label[32];
  std::snprintf(label, sizeof(label), "%d", row + 1);
  ImGui::Selectable(label, false);

  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    ImGui::SetDragDropPayload("DND_BINARY_FORMAT_TOKEN",
                              &row,
                              sizeof(row));
    ImGui::Text("Moving row %d", row + 1);
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginDragDropTarget()) {
    const ImGuiPayload* payload =
      ImGui::AcceptDragDropPayload("DND_BINARY_FORMAT_TOKEN");
    if (payload) {
      IM_ASSERT(payload->DataSize == sizeof(int));
      const int srcIndex = *static_cast<const int*>(payload->Data);
      if (srcIndex >= 0 &&
          srcIndex < static_cast<int>(tokens.size()) &&
          srcIndex != row) {
        FieldSpec token = tokens[srcIndex];
        tokens.erase(tokens.begin() + srcIndex);
        int dst = row;
        if (srcIndex < row) --dst;
        tokens.insert(tokens.begin() + dst, token);
        ImGui::EndDragDropTarget();
        return true;
      }
    }
    ImGui::EndDragDropTarget();
  }

  return false;
}

void DrawSimpleBinaryFormatEditor(std::vector<FieldSpec>& tokens)
{
  ImGui::TextDisabled("Drag rows by the Index column to reorder fields.");
  if (ImGui::Button("Add token")) {
    FieldSpec spec;
    spec.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(spec);
    tokens.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("BinaryFormatTable", 5,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
      FieldSpec& spec = tokens[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (HandleBinaryIndexDragDrop(tokens, i)) {
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##field", GetFieldKeyDisplayName(spec.key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          const bool selected = spec.key == key;
          if (ImGui::Selectable(GetFieldKeyDisplayName(key), selected)) {
            spec.key = key;
            ApplyDefaultFieldSpec(spec);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##type", GetDataTypeDisplayName(spec.type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          DataType type = kDataTypeChoices[n].type;
          const bool selected = spec.type == type;
          if (ImGui::Selectable(kDataTypeChoices[n].name, selected)) {
            spec.type = type;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##count", &spec.count);
      if (spec.count < 0) spec.count = 0;

      ImGui::TableNextColumn();
      if (ImGui::Button("-")) {
        tokens.erase(tokens.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void DrawSimpleGadgetFormatEditor(std::vector<FieldSpec>& tokens)
{
  ImGui::TextDisabled(
    "Order is the Gadget binary block order. Record markers are handled automatically.");
  ImGui::TextDisabled(
    "The 256-byte Gadget header is read automatically; do not add it here.");
  ImGui::TextDisabled(
    "For skip/dummy: components = values per element; repeat = number of whole blocks.");
  ImGui::TextDisabled("Drag rows by the Index column to reorder blocks.");
  if (ImGui::Button("Reset Gadget default")) {
    tokens = MakeDefaultGadgetFormatTokens();
  }
  ImGui::SameLine();
  if (ImGui::Button("All float")) {
    SetAllGadgetFloatingTypes(tokens, DataType::Float);
  }
  ImGui::SameLine();
  if (ImGui::Button("All double")) {
    SetAllGadgetFloatingTypes(tokens, DataType::Double);
  }
  ImGui::SameLine();
  if (ImGui::Button("Add field/skip")) {
    FieldSpec spec;
    spec.key = FieldKey::Dummy;
    ApplyGadgetBlockDefaults(spec);
    tokens.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("GadgetFormatTable", 7,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field / skip", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    SetupTypeMaskColumn();
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, DataTypeColumnWidth());
    ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
      FieldSpec& spec = tokens[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (HandleGadgetIndexDragDrop(tokens, i)) {
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::TableNextColumn();
      const char* blockName = spec.key == FieldKey::Dummy
        ? "skip/dummy"
        : GetFieldKeyDisplayName(spec.key);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##block", blockName)) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          if (key == FieldKey::Type || key == FieldKey::Unknown) continue;
          const char* label = key == FieldKey::Dummy
            ? "skip/dummy"
            : GetFieldKeyDisplayName(key);
          const bool selected = spec.key == key;
          if (ImGui::Selectable(label, selected)) {
            spec.key = key;
            ApplyGadgetBlockDefaults(spec);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      DrawGadgetTypeMaskEditor(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      DrawGadgetTypeCombo(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##components", &spec.count);
      if (spec.count < 1) spec.count = 1;

      ImGui::TableNextColumn();
      if (spec.key == FieldKey::Dummy) {
        int repeat = GadgetBlockRepeat(spec.sourceName);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##repeat", &repeat)) {
          SetGadgetTypeSource(spec, repeat);
        }
      } else {
        ImGui::TextUnformatted("-");
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("-")) {
        tokens.erase(tokens.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void DrawCommonInputFormatEditor(InputDensityUnit& inputDensityUnit,
                                 InputTemperatureUnit& inputTemperatureUnit,
                                 InputMagneticFieldUnit& inputMagneticFieldUnit,
                                 UnitSystem& unitsDraft,
                                 const UnitSystem& currentUnits,
                                 bool& unitsDraftDirty,
                                 bool& applyUnitsRequested,
                                 bool& unitConversionRebuildRequested)
{
  ImGui::SeparatorText("Input field interpretation");
  ImGui::TextDisabled(
    "Used when the file does not provide enough metadata. HDF5/AREPO metadata overrides this when available.");
  ImGui::TextDisabled(
    "If a field is marked as code-unit, conversion uses Code units / load defaults.");

  int densityUnitIndex = static_cast<int>(inputDensityUnit);
  static const char* DensityUnitNames[] = {
    "code mass density",
    "nH [cm^-3]",
    "mass density [g cm^-3]"
  };
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::Combo("Input density unit",
                   &densityUnitIndex,
                   DensityUnitNames,
                   IM_ARRAYSIZE(DensityUnitNames))) {
    inputDensityUnit =
      static_cast<InputDensityUnit>(std::clamp(densityUnitIndex, 0, 2));
  }

  int temperatureUnitIndex = static_cast<int>(inputTemperatureUnit);
  static const char* TemperatureUnitNames[] = {
    "temperature [K]",
    "code internal energy"
  };
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::Combo("Input thermal field",
                   &temperatureUnitIndex,
                   TemperatureUnitNames,
                   IM_ARRAYSIZE(TemperatureUnitNames))) {
    inputTemperatureUnit =
      static_cast<InputTemperatureUnit>(std::clamp(temperatureUnitIndex, 0, 1));
  }

  int magneticFieldUnitIndex = static_cast<int>(inputMagneticFieldUnit);
  static const char* MagneticFieldUnitNames[] = {
    "B [Gauss]",
    "code magnetic field"
  };
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::Combo("Input magnetic field unit",
                   &magneticFieldUnitIndex,
                   MagneticFieldUnitNames,
                   IM_ARRAYSIZE(MagneticFieldUnitNames))) {
    inputMagneticFieldUnit =
      static_cast<InputMagneticFieldUnit>(
        std::clamp(magneticFieldUnitIndex, 0, 1));
  }

  ImGui::TextDisabled(
    "Positions, masses, velocities, and smoothing lengths remain stored in code units.");

  ImGui::SeparatorText("Code units / load defaults");
  ImGui::TextDisabled(
    "These values are the code units used to interpret code-unit fields.");
  ImGui::TextDisabled(
    "Apply updates future loads and rescales loaded density/T/B when applicable.");

  if (!unitsDraftDirty && !applyUnitsRequested) {
    unitsDraft = currentUnits;
  }

  bool unitChanged = false;
  const float unitInputWidth = 220.0f;
  const float presetColumnX = 380.0f;
  ImGui::SetNextItemWidth(220.0f);
  unitChanged |= ImGui::InputDouble("UnitLength_in_cm",
                                    &unitsDraft.length_cm,
                                    0., 0., "%g");
  ImGui::SameLine(presetColumnX);
  if (ImGui::SmallButton("AU")) {
    unitsDraft.setLengthToAU();
    unitChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("pc")) {
    unitsDraft.setLengthToPC();
    unitChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("kpc")) {
    unitsDraft.setLengthToKPC();
    unitChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Mpc")) {
    unitsDraft.setLengthToMPC();
    unitChanged = true;
  }

  ImGui::SetNextItemWidth(unitInputWidth);
  unitChanged |= ImGui::InputDouble("UnitMass_in_g",
                                    &unitsDraft.mass_g,
                                    0., 0., "%g");
  ImGui::SameLine(presetColumnX);
  if (ImGui::SmallButton("Msun")) {
    unitsDraft.setMassToSolar();
    unitChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("1e10 Msun")) {
    unitsDraft.setMassTo1e10Solar();
    unitChanged = true;
  }

  ImGui::SetNextItemWidth(unitInputWidth);
  unitChanged |= ImGui::InputDouble("UnitVelocity_in_cm_per_s",
                                    &unitsDraft.velocity_cm_per_s,
                                    0., 0., "%g");
  ImGui::SetNextItemWidth(unitInputWidth);
  unitChanged |= ImGui::InputDouble("Hubble",
                                    &unitsDraft.hubble,
                                    0., 0., "%g");
  unitChanged |= ImGui::Checkbox("ComovingCoordinate",
                                 &unitsDraft.useComovingCoordinate);

  if (unitChanged) {
    unitsDraftDirty = true;
  }

  ImGui::BeginDisabled(!unitsDraftDirty || applyUnitsRequested);
  if (ImGui::Button("Apply code units")) {
    applyUnitsRequested = true;
    unitConversionRebuildRequested = true;
  }
  ImGui::EndDisabled();
  if (applyUnitsRequested) {
    ImGui::SameLine();
    ImGui::TextDisabled("Applying...");
  }
}

#ifdef HAVE_HDF5
DataType Hdf5PreviewDataType(const HDF5Utils::H5DatasetMeta& meta)
{
  if (meta.cls == H5T_FLOAT) {
    return meta.bytes > sizeof(float) ? DataType::Double : DataType::Float;
  }
  if (meta.cls == H5T_INTEGER) {
    return meta.bytes > sizeof(int32_t) ? DataType::Int64 : DataType::Int32;
  }
  return DataType::Float;
}

int Hdf5PreviewComponentCount(const HDF5Utils::H5DatasetMeta& meta)
{
  if (meta.rank >= 2 && meta.dims[1] > 0) {
    return static_cast<int>(std::min<hsize_t>(meta.dims[1], 1024));
  }
  return 1;
}

bool Hdf5DatasetShouldDefaultToAllTypes(const std::string& name)
{
  return name == GetDefaultHDF5DatasetName(FieldKey::Position) ||
         name == GetDefaultHDF5DatasetName(FieldKey::Velocity) ||
         name == GetDefaultHDF5DatasetName(FieldKey::Mass) ||
         name == GetDefaultHDF5DatasetName(FieldKey::ID) ||
         name == "IDs" ||
         name == "ID" ||
         name == "ParticleID";
}

std::string Hdf5MetadataMaskString(std::uint8_t mask)
{
  std::string out;
  for (int ptype = 0; ptype < 6; ++ptype) {
    if ((mask & static_cast<std::uint8_t>(1u << ptype)) == 0) {
      continue;
    }
    if (!out.empty()) out += ",";
    out += "T" + std::to_string(ptype);
  }
  return out.empty() ? "none" : out;
}

bool HasHdf5MetadataExtension(const std::filesystem::path& path)
{
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return ext == ".h5" || ext == ".hdf5";
}

std::vector<std::string> DiscoverSplitHdf5MetadataParts(const std::string& path)
{
  namespace fs = std::filesystem;

  const fs::path selected(path);
  if (!HasHdf5MetadataExtension(selected)) {
    return {path};
  }

  const fs::path parent = selected.parent_path();
  const std::string filename = selected.filename().string();
  const std::string suffix = selected.extension().string();
  const size_t suffixPos = filename.size() - suffix.size();
  const size_t partDot = filename.rfind('.', suffixPos > 0 ? suffixPos - 1 : 0);
  if (partDot == std::string::npos || partDot >= suffixPos) {
    return {path};
  }

  const std::string partToken =
    filename.substr(partDot + 1, suffixPos - partDot - 1);
  if (partToken.empty() ||
      !std::all_of(partToken.begin(),
                   partToken.end(),
                   [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return {path};
  }

  const std::string prefix = filename.substr(0, partDot + 1);
  std::map<int, std::string> parts;
  try {
    for (const auto& entry : fs::directory_iterator(parent)) {
      if (!entry.is_regular_file()) continue;
      const std::string name = entry.path().filename().string();
      if (name.size() <= prefix.size() + suffix.size()) continue;
      if (name.compare(0, prefix.size(), prefix) != 0) continue;
      if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        continue;
      }

      const std::string token =
        name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
      if (token.empty() ||
          !std::all_of(token.begin(),
                       token.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; })) {
        continue;
      }
      parts[std::stoi(token)] = entry.path().string();
    }
  } catch (const fs::filesystem_error&) {
    return {path};
  }

  if (parts.size() <= 1 || parts.begin()->first != 0) {
    return {path};
  }

  std::vector<std::string> out;
  out.reserve(parts.size());
  int expected = 0;
  for (const auto& [partIndex, partPath] : parts) {
    if (partIndex != expected) {
      return {};
    }
    out.push_back(partPath);
    ++expected;
  }
  return out;
}

void AccumulateHDF5MetadataPreview(FileFormatDialogState& state,
                                   H5::H5File& file)
{
  for (int ptype = 0; ptype < 6; ++ptype) {
    char groupPath[32];
    std::snprintf(groupPath, sizeof(groupPath), "/PartType%d", ptype);
    if (!HDF5Utils::groupExists(file, groupPath)) {
      continue;
    }

    H5::Group group = file.openGroup(groupPath);
    const hsize_t nobj = group.getNumObjs();
    for (hsize_t i = 0; i < nobj; ++i) {
      if (group.getObjTypeByIdx(i) != H5G_DATASET) {
        continue;
      }
      const std::string name = group.getObjnameByIdx(i);
      H5::DataSet ds = group.openDataSet(name);
      const HDF5Utils::H5DatasetMeta meta = HDF5Utils::getDatasetMeta(ds);
      if (meta.rank < 1 || meta.rank > 2) {
        continue;
      }
      const DataType previewType = Hdf5PreviewDataType(meta);
      const int previewCount = Hdf5PreviewComponentCount(meta);
      std::cerr << "[HDF5MetadataScan] ptype=T" << ptype
                << " dataset=" << name
                << " type=" << GetDataTypeDisplayName(previewType)
                << " count=" << previewCount
                << " bit=0x" << std::hex
                << static_cast<unsigned>(1u << ptype)
                << std::dec << "\n";

      auto it = std::find_if(
        state.hdf5MetadataPreview.begin(),
        state.hdf5MetadataPreview.end(),
        [&](const Hdf5DatasetMetadataPreview& item) {
          return item.sourceName == name;
        });
      const std::uint8_t typeMask = Hdf5DatasetShouldDefaultToAllTypes(name)
        ? 0x3fu
        : static_cast<std::uint8_t>(1u << ptype);
      if (it == state.hdf5MetadataPreview.end()) {
        Hdf5DatasetMetadataPreview preview;
        preview.sourceName = name;
        preview.type = previewType;
        preview.count = previewCount;
        preview.typeMask = typeMask;
        state.hdf5MetadataPreview.push_back(std::move(preview));
      } else {
        it->typeMask =
          static_cast<std::uint8_t>(it->typeMask | typeMask);
      }
    }
  }
}

void ScanHDF5MetadataPreview(FileFormatDialogState& state, const char* path)
{
  state.hdf5MetadataPreview.clear();
  state.hdf5MetadataPath = path ? path : "";
  state.hdf5MetadataScanned = false;
  state.hdf5MetadataMessage.clear();

  if (state.hdf5MetadataPath.empty()) {
    state.hdf5MetadataMessage = "No HDF5 file path is selected.";
    return;
  }

  try {
    H5SilenceErrors quiet(true);
    const std::vector<std::string> parts =
      DiscoverSplitHdf5MetadataParts(state.hdf5MetadataPath);
    if (parts.empty()) {
      state.hdf5MetadataMessage =
        "HDF5 metadata scan failed: split snapshot parts are not contiguous.";
      return;
    }

    for (const std::string& part : parts) {
      H5::H5File file(part, H5F_ACC_RDONLY);
      AccumulateHDF5MetadataPreview(state, file);
    }

    std::sort(state.hdf5MetadataPreview.begin(),
              state.hdf5MetadataPreview.end(),
              [](const Hdf5DatasetMetadataPreview& a,
                 const Hdf5DatasetMetadataPreview& b) {
                return a.sourceName < b.sourceName;
              });
    state.hdf5MetadataScanned = true;
    state.hdf5MetadataMessage =
      "Scanned " + std::to_string(state.hdf5MetadataPreview.size()) +
      " HDF5 datasets from " + std::to_string(parts.size()) + " file(s).";
    std::cerr << "[HDF5MetadataScan] path=" << state.hdf5MetadataPath
              << " parts=" << parts.size()
              << " datasets=" << state.hdf5MetadataPreview.size() << "\n";
    for (const Hdf5DatasetMetadataPreview& preview :
         state.hdf5MetadataPreview) {
      std::cerr << "[HDF5MetadataScan] dataset=" << preview.sourceName
                << " type=" << GetDataTypeDisplayName(preview.type)
                << " count=" << preview.count
                << " mask=0x" << std::hex
                << static_cast<unsigned>(preview.typeMask & 0x3fu)
                << std::dec << " ("
                << Hdf5MetadataMaskString(preview.typeMask) << ")\n";
    }
  } catch (const H5::Exception& e) {
    state.hdf5MetadataMessage =
      std::string("HDF5 metadata scan failed: ") + e.getDetailMsg();
  } catch (const std::exception& e) {
    state.hdf5MetadataMessage =
      std::string("HDF5 metadata scan failed: ") + e.what();
  } catch (...) {
    state.hdf5MetadataMessage = "HDF5 metadata scan failed.";
  }
}

void ApplyHDF5MetadataPreview(FieldSpec& spec,
                              const Hdf5DatasetMetadataPreview& preview)
{
  std::cerr << "[HDF5MetadataApply] field="
            << GetFieldKeyDisplayName(spec.key)
            << " source=" << preview.sourceName
            << " type=" << GetDataTypeDisplayName(preview.type)
            << " count=" << preview.count
            << " mask=0x" << std::hex
            << static_cast<unsigned>(preview.typeMask & 0x3fu)
            << std::dec << " ("
            << Hdf5MetadataMaskString(preview.typeMask) << ")\n";
  spec.sourceName = preview.sourceName;
  spec.type = preview.type;
  spec.count = std::max(1, preview.count);
  spec.typeMask = preview.typeMask;
}

std::string HDF5MetadataDatasetLabel(std::string sourceName)
{
  const size_t slash = sourceName.find_last_of('/');
  if (slash != std::string::npos) {
    sourceName = sourceName.substr(slash + 1);
  }
  return sourceName;
}

const Hdf5DatasetMetadataPreview* FindHDF5MetadataPreview(
  const std::vector<Hdf5DatasetMetadataPreview>& preview,
  const std::string& sourceName)
{
  const std::string label = HDF5MetadataDatasetLabel(sourceName);
  auto it = std::find_if(
    preview.begin(),
    preview.end(),
    [&](const Hdf5DatasetMetadataPreview& item) {
      return item.sourceName == sourceName || item.sourceName == label;
    });
  return it == preview.end() ? nullptr : &(*it);
}

bool ApplyHDF5MetadataPreviewBySource(
  FieldSpec& spec,
  const std::vector<Hdf5DatasetMetadataPreview>& preview)
{
  if (const Hdf5DatasetMetadataPreview* item =
        FindHDF5MetadataPreview(preview, spec.sourceName)) {
    ApplyHDF5MetadataPreview(spec, *item);
    return true;
  }
  return false;
}

void ApplyHDF5MetadataPreviewToMatchingFields(
  std::vector<FieldSpec>& tokens,
  const std::vector<Hdf5DatasetMetadataPreview>& preview)
{
  for (FieldSpec& spec : tokens) {
    if (spec.sourceName.empty() || spec.sourceName == "dummy") {
      continue;
    }
    ApplyHDF5MetadataPreviewBySource(spec, preview);
  }
}

void DrawSimpleHDF5FormatEditor(FileFormatDialogState& state,
                                std::vector<FieldSpec>& tokens,
                                const char* metadataPath)
{
  if (ImGui::Button("Scan metadata")) {
    ScanHDF5MetadataPreview(state, metadataPath);
    if (state.hdf5MetadataScanned) {
      ApplyHDF5MetadataPreviewToMatchingFields(tokens,
                                               state.hdf5MetadataPreview);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear metadata")) {
    state.hdf5MetadataScanned = false;
    state.hdf5MetadataPreview.clear();
    state.hdf5MetadataPath.clear();
    state.hdf5MetadataMessage.clear();
  }
  if (!state.hdf5MetadataMessage.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.hdf5MetadataMessage.c_str());
  }

  if (ImGui::Button("Add field")) {
    FieldSpec spec;
    spec.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(spec);
    tokens.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("HDF5InputFormatTable", 7,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 168.0f);
    SetupTypeMaskColumn();
    ImGui::TableSetupColumn("dataType", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
      FieldSpec& spec = tokens[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", i + 1);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##field", GetFieldKeyDisplayName(spec.key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          const bool selected = spec.key == key;
          if (ImGui::Selectable(GetFieldKeyDisplayName(key), selected)) {
            const FieldSpec before = spec;
            spec.key = key;
            ApplyDefaultFieldSpec(spec);
            if (state.hdf5MetadataScanned &&
                !before.sourceName.empty() &&
                before.sourceName != "dummy" &&
                before.sourceName != "unknown") {
              spec.sourceName = HDF5MetadataDatasetLabel(before.sourceName);
              if (!ApplyHDF5MetadataPreviewBySource(
                    spec,
                    state.hdf5MetadataPreview)) {
                spec.type = before.type;
                spec.count = before.count;
                spec.typeMask = before.typeMask;
              }
            } else if (state.hdf5MetadataScanned) {
              ApplyHDF5MetadataPreviewBySource(spec,
                                               state.hdf5MetadataPreview);
            }
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        if (state.hdf5MetadataScanned) {
          const char* previewLabel = spec.sourceName.empty()
            ? "<select dataset>"
            : spec.sourceName.c_str();
          ImGui::SetNextItemWidth(-1);
          if (ImGui::BeginCombo("##sourceName", previewLabel)) {
            for (const Hdf5DatasetMetadataPreview& preview :
                 state.hdf5MetadataPreview) {
              const bool selected = spec.sourceName == preview.sourceName;
              if (ImGui::Selectable(preview.sourceName.c_str(), selected)) {
                ApplyHDF5MetadataPreview(spec, preview);
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
        } else {
          char buf[128];
          std::snprintf(buf, sizeof(buf), "%s", spec.sourceName.c_str());
          ImGui::SetNextItemWidth(-1);
          if (ImGui::InputText("##sourceName", buf, IM_ARRAYSIZE(buf))) {
            spec.sourceName = buf;
          }
        }
      }

      ImGui::TableNextColumn();
      DrawFieldTypeMaskCheckboxes(spec, "simple_hdf5_type");

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##type", GetDataTypeDisplayName(spec.type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          const DataType type = kDataTypeChoices[n].type;
          const bool selected = spec.type == type;
          if (ImGui::Selectable(kDataTypeChoices[n].name, selected)) {
            spec.type = type;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##count", &spec.count, 1, 10);
      if (spec.count < 1) spec.count = 1;

      ImGui::TableNextColumn();
      if (ImGui::Button("-", ImVec2(24,24))) {
        tokens.erase(tokens.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }
}
#endif

void DrawGadgetFormatDialog(FileFormatDialogState& state,
                            std::vector<FieldSpec>& formatTokens)
{
  if (!state.showFormatDialog) return;

  ImGui::SetNextWindowSize(ImVec2(780, 420), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Edit Gadget Binary Format",
                    &state.showFormatDialog)) {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled(
    "Order is the Gadget binary block order. Record markers are handled automatically.");
  ImGui::TextDisabled(
    "The 256-byte Gadget header is read automatically; do not add it here.");
  ImGui::TextDisabled(
    "For skip/dummy: components = values per element; repeat = number of whole blocks.");
  ImGui::TextDisabled("Drag rows by the Index column to reorder blocks.");

  if (ImGui::Button("Reset Gadget default")) {
    state.formatTokensEdit = MakeDefaultGadgetFormatTokens();
  }
  ImGui::SameLine();
  if (ImGui::Button("All float")) {
    SetAllGadgetFloatingTypes(state.formatTokensEdit, DataType::Float);
  }
  ImGui::SameLine();
  if (ImGui::Button("All double")) {
    SetAllGadgetFloatingTypes(state.formatTokensEdit, DataType::Double);
  }
  ImGui::SameLine();
  if (ImGui::Button("Add field/skip")) {
    FieldSpec spec;
    spec.key = FieldKey::Dummy;
    ApplyGadgetBlockDefaults(spec);
    state.formatTokensEdit.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("GadgetFormatTable", 7,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field / skip", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    SetupTypeMaskColumn();
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, DataTypeColumnWidth());
    ImGui::TableSetupColumn("Components/element", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(state.formatTokensEdit.size()); ++i) {
      FieldSpec& spec = state.formatTokensEdit[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (HandleGadgetIndexDragDrop(state.formatTokensEdit, i)) {
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::TableNextColumn();
      const char* blockName = spec.key == FieldKey::Dummy
        ? "skip/dummy"
        : GetFieldKeyDisplayName(spec.key);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##block", blockName)) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          if (key == FieldKey::Type || key == FieldKey::Unknown) continue;
          const char* label = key == FieldKey::Dummy
            ? "skip/dummy"
            : GetFieldKeyDisplayName(key);
          const bool selected = spec.key == key;
          if (ImGui::Selectable(label, selected)) {
            spec.key = key;
            ApplyGadgetBlockDefaults(spec);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      DrawGadgetTypeMaskEditor(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      DrawGadgetTypeCombo(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##components", &spec.count);
      if (spec.count < 1) spec.count = 1;

      ImGui::TableNextColumn();
      if (spec.key == FieldKey::Dummy) {
        int repeat = GadgetBlockRepeat(spec.sourceName);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##repeat", &repeat)) {
          SetGadgetTypeSource(spec, repeat);
        }
      } else {
        ImGui::TextUnformatted("-");
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("-")) {
        state.formatTokensEdit.erase(state.formatTokensEdit.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  if (ImGui::Button("OK")) {
    formatTokens = state.formatTokensEdit;
    state.showFormatDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    state.showFormatDialog = false;
  }

  ImGui::End();
}

} // namespace

void DrawBinaryFormatDialog(FileFormatDialogState& state,
                            std::vector<FieldSpec>& formatTokens,
                            FileFormat readFormat) {
  if (!state.showFormatDialog) return;
  if (readFormat == FileFormat::Gadget) {
    DrawGadgetFormatDialog(state, formatTokens);
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit Data Format", &state.showFormatDialog)){
    ImGui::End();
    return;
  }

  for (size_t i = 0; i < state.formatTokensEdit.size(); i++) {
    ImGui::PushID((int)i);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s, count=%d",
                  GetFieldKeyDisplayName(state.formatTokensEdit[i].key),
                  GetDataTypeDisplayName(state.formatTokensEdit[i].type),
                  state.formatTokensEdit[i].count);

    if (ImGui::Selectable(buf)) {
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
      int payloadIndex = static_cast<int>(i);
      ImGui::SetDragDropPayload("DND_FORMAT_TOKEN", &payloadIndex, sizeof(payloadIndex));
      ImGui::Text("Moving %s", buf);
      ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
      ImVec2 itemMin = ImGui::GetItemRectMin();
      ImVec2 itemMax = ImGui::GetItemRectMax();
      float midY = (itemMax.y + itemMin.y) * 0.5f;

      const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_FORMAT_TOKEN");

      if (payload) {
        IM_ASSERT(payload->DataSize == sizeof(int));
        int srcIndex = *(const int*)payload->Data;
        if (static_cast<size_t>(srcIndex) > i)
          midY = itemMax.y;
        else
          midY = itemMin.y;
      }

      int dropIndex = (int)i;
      if (ImGui::GetIO().MousePos.y > midY)
        dropIndex = (int)i + 1;

      if (payload) {
        IM_ASSERT(payload->DataSize == sizeof(int));
        int srcIndex = *(const int*)payload->Data;
        if (srcIndex != dropIndex && srcIndex != dropIndex - 1) {
          FieldSpec token = state.formatTokensEdit[srcIndex];
          state.formatTokensEdit.erase(state.formatTokensEdit.begin() + srcIndex);
          if (srcIndex < dropIndex)
            dropIndex--;
          state.formatTokensEdit.insert(state.formatTokensEdit.begin() + dropIndex, token);
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginCombo("##fieldKeyCombo",
                          GetFieldKeyDisplayName(state.formatTokensEdit[i].key))) {
      for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
        FieldKey key = kAvailableFieldKeys[n];
        bool is_selected = (state.formatTokensEdit[i].key == key);

        if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
          state.formatTokensEdit[i].key = key;
          ApplyDefaultFieldSpec(state.formatTokensEdit[i]);
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Type", GetDataTypeDisplayName(state.formatTokensEdit[i].type))) {
      for (int n = 0; n < kNumDataTypeChoices; ++n) {
        DataType type = kDataTypeChoices[n].type;
        bool is_selected = (state.formatTokensEdit[i].type == type);

        if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
          state.formatTokensEdit[i].type = type;
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::InputInt("Count", &state.formatTokensEdit[i].count);
    if (state.formatTokensEdit[i].count < 0)
      state.formatTokensEdit[i].count = 0;

    if (ImGui::Button("Delete")) {
      state.formatTokensEdit.erase(state.formatTokensEdit.begin() + i);
      ImGui::PopID();
      i--;
      continue;
    }
    ImGui::Separator();
    ImGui::PopID();
  }

  if (ImGui::Button("Add Token")) {
    FieldSpec newToken;
    newToken.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(newToken);
    state.formatTokensEdit.push_back(newToken);
  }
  if (ImGui::Button("OK")) {
    formatTokens = state.formatTokensEdit;
    state.showFormatDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    state.showFormatDialog = false;
  }
  ImGui::End();
}

void DrawInputFormatDialog(FileFormatDialogState& state,
                           std::vector<FieldSpec>& binaryFormatTokens,
                           std::vector<FieldSpec>& gadgetFormatTokens,
#ifdef HAVE_HDF5
                           std::vector<FieldSpec>& hdf5FormatTokens,
                           const char* hdf5MetadataPath,
#endif
                           FileFormat& readFormat,
                           InputDensityUnit& inputDensityUnit,
                           InputTemperatureUnit& inputTemperatureUnit,
                           InputMagneticFieldUnit& inputMagneticFieldUnit,
                           UnitSystem& unitsDraft,
                           const UnitSystem& currentUnits,
                           bool& unitsDraftDirty,
                           bool& applyUnitsRequested,
                           bool& unitConversionRebuildRequested)
{
  if (!state.showFormatDialog) {
    state.inputFormatDialogInitialized = false;
    return;
  }

  if (!state.inputFormatDialogInitialized) {
    state.binaryFormatTokensEdit = binaryFormatTokens;
    state.gadgetFormatTokensEdit = gadgetFormatTokens;
#ifdef HAVE_HDF5
    state.hdf5FormatTokensEdit = hdf5FormatTokens;
#endif
    if (readFormat == FileFormat::Gadget) {
      state.activeInputFormatTab = FileFormat::Gadget;
#ifdef HAVE_HDF5
    } else if (readFormat == FileFormat::HDF5) {
      state.activeInputFormatTab = FileFormat::HDF5;
#endif
    } else {
      state.activeInputFormatTab = FileFormat::Binary;
    }
    state.selectInputFormatTabOnOpen = true;
    state.inputFormatDialogInitialized = true;
  }

  ImGui::SetNextWindowSize(ImVec2(980, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Edit Data Format", &state.showFormatDialog)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("InputFormatTabs")) {
    const bool selectingInitialTab = state.selectInputFormatTabOnOpen;
    const FileFormat initialTab = state.activeInputFormatTab;
    if (ImGui::BeginTabItem("Common")) {
      DrawCommonInputFormatEditor(inputDensityUnit,
                                  inputTemperatureUnit,
                                  inputMagneticFieldUnit,
                                  unitsDraft,
                                  currentUnits,
                                  unitsDraftDirty,
                                  applyUnitsRequested,
                                  unitConversionRebuildRequested);
      ImGui::EndTabItem();
    }

    ImGuiTabItemFlags binaryFlags =
      selectingInitialTab && initialTab == FileFormat::Binary
        ? ImGuiTabItemFlags_SetSelected
        : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem("Binary", nullptr, binaryFlags)) {
      const bool drawThisTab =
        !selectingInitialTab || initialTab == FileFormat::Binary;
      if (drawThisTab) {
        state.activeInputFormatTab = FileFormat::Binary;
        readFormat = FileFormat::Binary;
        DrawSimpleBinaryFormatEditor(state.binaryFormatTokensEdit);
      }
      ImGui::EndTabItem();
    }

    ImGuiTabItemFlags gadgetFlags =
      selectingInitialTab && initialTab == FileFormat::Gadget
        ? ImGuiTabItemFlags_SetSelected
        : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem("Gadget", nullptr, gadgetFlags)) {
      const bool drawThisTab =
        !selectingInitialTab || initialTab == FileFormat::Gadget;
      if (drawThisTab) {
        state.activeInputFormatTab = FileFormat::Gadget;
        readFormat = FileFormat::Gadget;
        DrawSimpleGadgetFormatEditor(state.gadgetFormatTokensEdit);
      }
      ImGui::EndTabItem();
    }

#ifdef HAVE_HDF5
    ImGuiTabItemFlags hdf5Flags =
      selectingInitialTab && initialTab == FileFormat::HDF5
        ? ImGuiTabItemFlags_SetSelected
        : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem("HDF5", nullptr, hdf5Flags)) {
      const bool drawThisTab =
        !selectingInitialTab || initialTab == FileFormat::HDF5;
      if (drawThisTab) {
        state.activeInputFormatTab = FileFormat::HDF5;
        readFormat = FileFormat::HDF5;
        DrawSimpleHDF5FormatEditor(state,
                                   state.hdf5FormatTokensEdit,
                                   hdf5MetadataPath);
      }
      ImGui::EndTabItem();
    }
#endif

    ImGui::EndTabBar();
    state.selectInputFormatTabOnOpen = false;
  }

  ImGui::Separator();
  if (ImGui::Button("OK")) {
    binaryFormatTokens = state.binaryFormatTokensEdit;
    gadgetFormatTokens = state.gadgetFormatTokensEdit;
#ifdef HAVE_HDF5
    std::cerr << "[HDF5FormatOK] final HDF5 input format tokens="
              << state.hdf5FormatTokensEdit.size() << "\n";
    for (const FieldSpec& spec : state.hdf5FormatTokensEdit) {
      std::cerr << "[HDF5FormatOK] field="
                << GetFieldKeyDisplayName(spec.key)
                << " source=" << spec.sourceName
                << " type=" << GetDataTypeDisplayName(spec.type)
                << " count=" << spec.count
                << " mask=0x" << std::hex
                << static_cast<unsigned>(spec.typeMask & 0x3fu)
                << std::dec << " ("
                << Hdf5MetadataMaskString(spec.typeMask) << ")\n";
    }
    hdf5FormatTokens = state.hdf5FormatTokensEdit;
#endif
    state.showFormatDialog = false;
    state.inputFormatDialogInitialized = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    state.showFormatDialog = false;
    state.inputFormatDialogInitialized = false;
  }

  ImGui::End();
}

void DrawOutputFormatDialog(FileFormatDialogState& state,
                            SnapshotOutputFormatConfig& outputFormat)
{
  if (!state.showOutputFormatDialog) return;

  ImGui::SetNextWindowSize(ImVec2(980, 460), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Edit HDF5 Output Format",
                    &state.showOutputFormatDialog)) {
    ImGui::End();
    return;
  }

  ImGui::Checkbox("Use output format override", &state.outputFormatEdit.enabled);
  ImGui::SameLine();
  if (ImGui::Button("Reset output default")) {
    state.outputFormatEdit.fields = MakeDefaultSnapshotOutputFields();
  }
  ImGui::SameLine();
  if (ImGui::Button("Add output field")) {
    SnapshotOutputFieldSpec spec;
    spec.key = FieldKey::Density;
    ApplyOutputFieldDefaults(spec);
    state.outputFormatEdit.fields.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("OutputFormatTable", 10,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Missing", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    SetupTypeMaskColumn();
    ImGui::TableSetupColumn("Up", ImGuiTableColumnFlags_WidthFixed, 36.0f);
    ImGui::TableSetupColumn("Down", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 36.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(state.outputFormatEdit.fields.size()); ++i) {
      SnapshotOutputFieldSpec& spec = state.outputFormatEdit.fields[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##field", GetFieldKeyDisplayName(spec.key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          if (key == FieldKey::Dummy || key == FieldKey::Type ||
              key == FieldKey::Unknown) {
            continue;
          }
          const bool selected = spec.key == key;
          if (ImGui::Selectable(GetFieldKeyDisplayName(key), selected)) {
            spec.key = key;
            ApplyOutputFieldDefaults(spec);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", spec.outputName.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##output", buf, IM_ARRAYSIZE(buf))) {
          spec.outputName = buf;
        }
      }

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##type", GetDataTypeDisplayName(spec.type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          const DataType type = kDataTypeChoices[n].type;
          const bool selected = spec.type == type;
          if (ImGui::Selectable(kDataTypeChoices[n].name, selected)) {
            spec.type = type;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt("##count", &spec.count)) {
        spec.count = std::max(1, spec.count);
        ParseDefaultValues(DefaultValuesText(spec).c_str(), spec);
      }

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##missing", MissingPolicyName(spec.missingPolicy))) {
        const SnapshotOutputMissingPolicy policies[] = {
          SnapshotOutputMissingPolicy::Omit,
          SnapshotOutputMissingPolicy::FillDefault,
          SnapshotOutputMissingPolicy::Require
        };
        for (SnapshotOutputMissingPolicy policy : policies) {
          const bool selected = spec.missingPolicy == policy;
          if (ImGui::Selectable(MissingPolicyName(policy), selected)) {
            spec.missingPolicy = policy;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        std::string defaults = DefaultValuesText(spec);
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%s", defaults.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##defaults", buf, IM_ARRAYSIZE(buf))) {
          ParseDefaultValues(buf, spec);
        }
      }

      ImGui::TableNextColumn();
      DrawTypeMaskButtons(spec.typeMask, "hdf5_output_types");

      ImGui::TableNextColumn();
      if (ImGui::Button("^") && i > 0) {
        std::swap(state.outputFormatEdit.fields[i],
                  state.outputFormatEdit.fields[i - 1]);
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("v") &&
          i + 1 < static_cast<int>(state.outputFormatEdit.fields.size())) {
        std::swap(state.outputFormatEdit.fields[i],
                  state.outputFormatEdit.fields[i + 1]);
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("-")) {
        state.outputFormatEdit.fields.erase(state.outputFormatEdit.fields.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  if (ImGui::Button("OK")) {
    outputFormat = state.outputFormatEdit;
    if (outputFormat.fields.empty()) {
      outputFormat.fields = MakeDefaultSnapshotOutputFields();
    }
    state.showOutputFormatDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    state.showOutputFormatDialog = false;
  }

  ImGui::End();
}

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          std::vector<FieldSpec>& formatTokens) {
  if (!state.showHDF5MappingDialog) return;

  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit HDF5 Data Format", &state.showHDF5MappingDialog)){
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("LegacyHDF5InputFormatTable", 7,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 168.0f);
    SetupTypeMaskColumn();
    ImGui::TableSetupColumn("dataType", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < (int)state.formatTokensEdit.size(); ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", i + 1);

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##fieldKeyCombo",
                            GetFieldKeyDisplayName(state.formatTokensEdit[i].key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          bool is_selected = (state.formatTokensEdit[i].key == key);

          if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
            state.formatTokensEdit[i].key = key;
            ApplyDefaultFieldSpec(state.formatTokensEdit[i]);
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        ImGui::PushItemWidth(-1);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", state.formatTokensEdit[i].sourceName.c_str());

        if (ImGui::InputText("##sourceName", buf, IM_ARRAYSIZE(buf))) {
          state.formatTokensEdit[i].sourceName = buf;
        }

        ImGui::PopItemWidth();
      }

      ImGui::TableNextColumn();
      DrawFieldTypeMaskCheckboxes(state.formatTokensEdit[i], "hdf5_type");

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##typeCombo",
                            GetDataTypeDisplayName(state.formatTokensEdit[i].type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          DataType type = kDataTypeChoices[n].type;
          bool is_selected = (state.formatTokensEdit[i].type == type);

          if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
            state.formatTokensEdit[i].type = type;
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::InputInt("##count", &state.formatTokensEdit[i].count, 1, 10);
      if (state.formatTokensEdit[i].count < 1) state.formatTokensEdit[i].count = 1;

      ImGui::TableNextColumn();
      if (ImGui::Button("-", ImVec2(24,24))) {
        state.formatTokensEdit.erase(state.formatTokensEdit.begin() + i);
        ImGui::PopID();
        --i;
        continue;
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  if (ImGui::Button("Add field")) {
    FieldSpec newToken;
    newToken.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(newToken);
    state.formatTokensEdit.push_back(newToken);
  }
  ImGui::SameLine();
  if (ImGui::Button("OK")) {
    formatTokens = state.formatTokensEdit;
    state.showHDF5MappingDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    state.showHDF5MappingDialog = false;
  }

  ImGui::End();
}

#endif
