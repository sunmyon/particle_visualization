#include "FileIO/file_format_dialog.h"
#include <imgui.h>

void DrawBinaryFormatDialog(bool& showDialog,
                            std::vector<FieldSpec>& formatTokensEdit,
                            SnapshotSource& source) {
  if (!showDialog) return;

  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit Data Format", &showDialog)){
    ImGui::End();
    return;
  }

  for (size_t i = 0; i < formatTokensEdit.size(); i++) {
    ImGui::PushID((int)i);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s, count=%d",
                  GetFieldKeyDisplayName(formatTokensEdit[i].key),
                  GetDataTypeDisplayName(formatTokensEdit[i].type),
                  formatTokensEdit[i].count);

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
          FieldSpec token = formatTokensEdit[srcIndex];
          formatTokensEdit.erase(formatTokensEdit.begin() + srcIndex);
          if (srcIndex < dropIndex)
            dropIndex--;
          formatTokensEdit.insert(formatTokensEdit.begin() + dropIndex, token);
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginCombo("##fieldKeyCombo",
                          GetFieldKeyDisplayName(formatTokensEdit[i].key))) {
      for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
        FieldKey key = kAvailableFieldKeys[n];
        bool is_selected = (formatTokensEdit[i].key == key);

        if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
          formatTokensEdit[i].key = key;
          ApplyDefaultFieldSpec(formatTokensEdit[i]);
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Type", GetDataTypeDisplayName(formatTokensEdit[i].type))) {
      for (int n = 0; n < kNumDataTypeChoices; ++n) {
        DataType type = kDataTypeChoices[n].type;
        bool is_selected = (formatTokensEdit[i].type == type);

        if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
          formatTokensEdit[i].type = type;
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::InputInt("Count", &formatTokensEdit[i].count);
    if (formatTokensEdit[i].count < 0)
      formatTokensEdit[i].count = 0;

    if (ImGui::Button("Delete")) {
      formatTokensEdit.erase(formatTokensEdit.begin() + i);
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
    formatTokensEdit.push_back(newToken);
  }
  if (ImGui::Button("OK")) {
    source.formatTokens = formatTokensEdit;
    showDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    showDialog = false;
  }
  ImGui::End();
}

#ifdef HAVE_HDF5
void DrawHDF5FormatDialog(bool& showDialog,
                          std::vector<FieldSpec>& formatTokensEdit,
                          SnapshotSource& source) {
  if (!showDialog) return;

  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit HDF5 Data Format", &showDialog)){
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

    for (int i = 0; i < (int)formatTokensEdit.size(); ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", i + 1);

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##fieldKeyCombo",
                            GetFieldKeyDisplayName(formatTokensEdit[i].key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          bool is_selected = (formatTokensEdit[i].key == key);

          if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
            formatTokensEdit[i].key = key;
            ApplyDefaultFieldSpec(formatTokensEdit[i]);
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
        ImGui::PushItemWidth(-1);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", formatTokensEdit[i].sourceName.c_str());

        if (ImGui::InputText("##sourceName", buf, IM_ARRAYSIZE(buf))) {
          formatTokensEdit[i].sourceName = buf;
        }

        ImGui::PopItemWidth();
      }

      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##typeCombo",
                            GetDataTypeDisplayName(formatTokensEdit[i].type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          DataType type = kDataTypeChoices[n].type;
          bool is_selected = (formatTokensEdit[i].type == type);

          if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
            formatTokensEdit[i].type = type;
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::InputInt("##count", &formatTokensEdit[i].count, 1, 10);
      if (formatTokensEdit[i].count < 1) formatTokensEdit[i].count = 1;

      ImGui::TableNextColumn();
      if (ImGui::Button("-", ImVec2(24,24))) {
        formatTokensEdit.erase(formatTokensEdit.begin() + i);
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
    formatTokensEdit.push_back(newToken);
  }
  ImGui::SameLine();
  if (ImGui::Button("OK")) {
    source.formatTokens_hdf5 = formatTokensEdit;
    showDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    showDialog = false;
  }

  ImGui::End();
}
#endif
