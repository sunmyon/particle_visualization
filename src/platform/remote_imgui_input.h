#pragma once

#include <vector>

#include "interaction/input_event.h"

struct RemoteInputCapture {
  bool mouse = false;
  bool keyboard = false;
};

// Call after transport input is drained and before ImGui::NewFrame().
// Local events are deliberately ignored because the local platform backend
// already submits them directly to ImGui.
void QueueRemoteInputForImGui(const std::vector<InputEvent>& events);

// Call after ImGui::NewFrame(). Client-provided capturedByUI values are not
// authoritative for remote events; capture is decided by the server UI.
void ApplyRemoteInputCapture(std::vector<InputEvent>& events,
                             const RemoteInputCapture& capture);

RemoteInputCapture CurrentImGuiInputCapture();
