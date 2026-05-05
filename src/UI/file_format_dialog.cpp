#include "UI/file_format_dialog.h"
#include <imgui.h>

#include <algorithm>
#include <cstdio>
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
  if (ImGui::Button("Add block")) {
    FieldSpec spec;
    spec.key = FieldKey::Density;
    ApplyGadgetBlockDefaults(spec);
    state.formatTokensEdit.push_back(std::move(spec));
  }
  ImGui::SameLine();
  if (ImGui::Button("Add skip/dummy")) {
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
    ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 140.0f);
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
      if (spec.key == FieldKey::Dummy) {
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
      } else {
        ImGui::TextUnformatted(GetDataTypeDisplayName(spec.type));
      }

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

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(FileFormatDialogState& state,
                          std::vector<FieldSpec>& formatTokens) {
  if (!state.showHDF5MappingDialog) return;

  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit HDF5 Data Format", &state.showHDF5MappingDialog)){
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("FieldTable", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("dataType", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
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
