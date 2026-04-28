#include "common_ui.h"

#include <imgui.h>

#include "app/state/runtime_state.h"

void ShowTime(const SnapshotCurrentState& current){
    // Pin the window to the top-left corner at pixel coordinate (10,10).
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    // Use a transparent background when desired.
    ImGui::SetNextWindowBgAlpha(0.3f);
    // Hide the title bar, border, and scrollbars through window flags.
    ImGui::Begin("Time Overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);
    if (current.useComovingCoordinates) {
      ImGui::Text("a: %.6g", current.loadedScaleFactor);
      ImGui::Text("z: %.6g", current.loadedRedshift);
      if (current.hasCosmicTime) {
        ImGui::Text("cosmic time [Gyr]: %.6g", current.loadedCosmicTime);
      } else {
        ImGui::Text("cosmic time: N/A");
      }
    } else {
      ImGui::Text("time: %.6g", current.loadedTime);
    }
    ImGui::End();
}

void ShowCameraSettingsUI(){};
