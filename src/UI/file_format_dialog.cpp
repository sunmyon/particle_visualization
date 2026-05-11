#include "UI/file_format_dialog.h"
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

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

void SetGadgetDomainSource(FieldSpec& spec, int domainIndex, int repeat)
{
  static const char* domains[] = { "all", "type0", "type0and5", "absolute" };
  domainIndex = std::clamp(domainIndex, 0, 3);
  repeat = std::max(1, repeat);
  spec.sourceName =
    std::string(domains[domainIndex]) + ":" + std::to_string(repeat);
}

void ApplyGadgetBlockDefaults(FieldSpec& spec)
{
  if (spec.key == FieldKey::Dummy) {
    spec.type = DataType::Float;
    spec.count = 1;
    spec.sourceName = "all:1";
    return;
  }
  ApplyDefaultFieldSpec(spec);
  if (IsFixedGadgetBlock(spec.key)) {
    spec.sourceName = "all:1";
  } else {
    spec.sourceName = "type0:1";
  }
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

void DrawFieldTypeMaskCheckboxes(FieldSpec& spec, const char* idPrefix)
{
  for (int type = 0; type < 6; ++type) {
    bool enabled =
      (spec.typeMask & static_cast<std::uint8_t>(1u << type)) != 0;
    char label[32];
    std::snprintf(label, sizeof(label), "T%d##%s%d", type, idPrefix, type);
    if (ImGui::Checkbox(label, &enabled)) {
      if (enabled) {
        spec.typeMask |= static_cast<std::uint8_t>(1u << type);
      } else {
        spec.typeMask &= static_cast<std::uint8_t>(~(1u << type));
      }
    }
    if (type != 2 && type != 5) {
      ImGui::SameLine();
    }
  }
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

  if (ImGui::BeginTable("BinaryFormatTable", 6,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthFixed, 72.0f);
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
      if (ImGui::Button("^") && i > 0) {
        std::swap(tokens[i], tokens[i - 1]);
      }
      ImGui::SameLine();
      if (ImGui::Button("v") &&
          i + 1 < static_cast<int>(tokens.size())) {
        std::swap(tokens[i], tokens[i + 1]);
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

  if (ImGui::BeginTable("GadgetFormatTable", 8,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field / skip", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthFixed, 95.0f);
    ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthFixed, 72.0f);
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
      if (spec.key == FieldKey::Dummy) {
        static const char* domains[] = { "all", "type0", "type0+type5", "absolute" };
        int domainIndex = GadgetDomainIndexOr(spec.sourceName, 0);
        int repeat = GadgetBlockRepeat(spec.sourceName);
        if (ImGui::Combo("##domain", &domainIndex, domains, IM_ARRAYSIZE(domains))) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      } else if (IsFixedGadgetBlock(spec.key)) {
        ImGui::TextUnformatted("all");
      } else {
        static const char* domains[] = { "all", "type0", "type0+type5" };
        int domainIndex = std::min(GadgetDomainIndexOr(spec.sourceName, 1), 2);
        const int repeat = GadgetBlockRepeat(spec.sourceName);
        if (ImGui::Combo("##domain", &domainIndex, domains, IM_ARRAYSIZE(domains))) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      }

      ImGui::TableNextColumn();
      DrawGadgetTypeCombo(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##components", &spec.count);
      if (spec.count < 1) spec.count = 1;

      ImGui::TableNextColumn();
      if (spec.key == FieldKey::Dummy) {
        int domainIndex = GadgetDomainIndexOr(spec.sourceName, 0);
        int repeat = GadgetBlockRepeat(spec.sourceName);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##repeat", &repeat)) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      } else {
        ImGui::TextUnformatted("-");
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("^") && i > 0) {
        std::swap(tokens[i], tokens[i - 1]);
      }
      ImGui::SameLine();
      if (ImGui::Button("v") &&
          i + 1 < static_cast<int>(tokens.size())) {
        std::swap(tokens[i], tokens[i + 1]);
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

#ifdef HAVE_HDF5
void DrawSimpleHDF5FormatEditor(std::vector<FieldSpec>& tokens)
{
  if (ImGui::Button("Add field")) {
    FieldSpec spec;
    spec.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(spec);
    tokens.push_back(std::move(spec));
  }

  if (ImGui::BeginTable("FieldTable", 7,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable |
                          ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f))) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 240.0f);
    ImGui::TableSetupColumn("Types", ImGuiTableColumnFlags_WidthFixed, 190.0f);
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
            spec.key = key;
            ApplyDefaultFieldSpec(spec);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", spec.sourceName.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##sourceName", buf, IM_ARRAYSIZE(buf))) {
          spec.sourceName = buf;
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

  if (ImGui::BeginTable("GadgetFormatTable", 8,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Field / skip", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Components/element", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthFixed, 72.0f);
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
      if (spec.key == FieldKey::Dummy) {
        static const char* domains[] = { "all", "type0", "type0+type5", "absolute" };
        int domainIndex = GadgetDomainIndexOr(spec.sourceName, 0);
        int repeat = GadgetBlockRepeat(spec.sourceName);
        if (ImGui::Combo("##domain", &domainIndex, domains, IM_ARRAYSIZE(domains))) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      } else if (IsFixedGadgetBlock(spec.key)) {
        ImGui::TextUnformatted("all");
      } else {
        static const char* domains[] = { "all", "type0", "type0+type5" };
        int domainIndex = std::min(GadgetDomainIndexOr(spec.sourceName, 1), 2);
        const int repeat = GadgetBlockRepeat(spec.sourceName);
        if (ImGui::Combo("##domain", &domainIndex, domains, IM_ARRAYSIZE(domains))) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      }

      ImGui::TableNextColumn();
      DrawGadgetTypeCombo(spec);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputInt("##components", &spec.count);
      if (spec.count < 1) spec.count = 1;

      ImGui::TableNextColumn();
      if (spec.key == FieldKey::Dummy) {
        int domainIndex = GadgetDomainIndexOr(spec.sourceName, 0);
        int repeat = GadgetBlockRepeat(spec.sourceName);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##repeat", &repeat)) {
          SetGadgetDomainSource(spec, domainIndex, repeat);
        }
      } else {
        ImGui::TextUnformatted("-");
      }

      ImGui::TableNextColumn();
      if (ImGui::Button("^") && i > 0) {
        std::swap(state.formatTokensEdit[i], state.formatTokensEdit[i - 1]);
      }
      ImGui::SameLine();
      if (ImGui::Button("v") &&
          i + 1 < static_cast<int>(state.formatTokensEdit.size())) {
        std::swap(state.formatTokensEdit[i], state.formatTokensEdit[i + 1]);
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
#endif
                           FileFormat& readFormat)
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
        DrawSimpleHDF5FormatEditor(state.hdf5FormatTokensEdit);
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
    ImGui::TableSetupColumn("Types", ImGuiTableColumnFlags_WidthFixed, 150.0f);
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
      for (int type = 0; type < 6; ++type) {
        if (type > 0) ImGui::SameLine();
        bool enabled =
          (spec.typeMask & static_cast<unsigned int>(1u << type)) != 0;
        char label[16];
        std::snprintf(label, sizeof(label), "%d", type);
        if (ImGui::Checkbox(label, &enabled)) {
          if (enabled) {
            spec.typeMask |= static_cast<unsigned int>(1u << type);
          } else {
            spec.typeMask &= ~static_cast<unsigned int>(1u << type);
          }
        }
      }

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

  if (ImGui::BeginTable("FieldTable", 7,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 240.0f);
    ImGui::TableSetupColumn("Types", ImGuiTableColumnFlags_WidthFixed, 190.0f);
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
