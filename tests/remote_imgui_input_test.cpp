#include "platform/remote_imgui_input.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include <imgui.h>

namespace {

void Check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void BeginTestFrame()
{
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(320.0f, 240.0f);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
}

} // namespace

int main()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigInputTrickleEventQueue = false;
  unsigned char* fontPixels = nullptr;
  int fontWidth = 0;
  int fontHeight = 0;
  io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
  Check(fontPixels && fontWidth > 0 && fontHeight > 0, "Font atlas failed");

  InputEvent localMove;
  localMove.type = InputEventType::PointerMove;
  localMove.x = 99.0f;
  localMove.y = 77.0f;
  QueueRemoteInputForImGui({localMove});
  BeginTestFrame();
  Check(io.MousePos.x != 99.0f || io.MousePos.y != 77.0f,
        "Local input was injected twice");
  ImGui::EndFrame();

  InputEvent move;
  move.source = InputSource::Remote;
  move.type = InputEventType::PointerMove;
  move.x = 12.0f;
  move.y = 34.0f;
  InputEvent button = move;
  button.type = InputEventType::PointerButton;
  button.button = PointerButton::Left;
  button.action = InputAction::Press;
  InputEvent wheel = move;
  wheel.type = InputEventType::PointerScroll;
  wheel.wheelX = -0.5f;
  wheel.wheelY = 2.0f;
  InputEvent key;
  key.source = InputSource::Remote;
  key.type = InputEventType::Key;
  key.key = InputKey::A;
  key.action = InputAction::Press;
  key.modifiers.ctrl = true;
  InputEvent text;
  text.source = InputSource::Remote;
  text.type = InputEventType::Text;
  text.text = "é";
  QueueRemoteInputForImGui({move, button, wheel, key, text});
  BeginTestFrame();
  Check(io.MousePos.x == 12.0f && io.MousePos.y == 34.0f,
        "Remote pointer position was not injected");
  Check(io.MouseDown[ImGuiMouseButton_Left],
        "Remote mouse button was not injected");
  Check(io.MouseWheelH == -0.5f && io.MouseWheel == 2.0f,
        "Remote wheel was not injected");
#if defined(__APPLE__)
  const bool controlDown = io.KeySuper;
#else
  const bool controlDown = io.KeyCtrl;
#endif
  Check(ImGui::IsKeyDown(ImGuiKey_A) && controlDown,
        "Remote key or modifier was not injected");
  Check(io.InputQueueCharacters.Size == 1 && io.InputQueueCharacters[0] == 0x00e9,
        "Remote UTF-8 text was not decoded by ImGui");
  ImGui::EndFrame();

  InputEvent release = button;
  release.action = InputAction::Release;
  InputEvent keyRelease = key;
  keyRelease.action = InputAction::Release;
  keyRelease.modifiers.ctrl = false;
  QueueRemoteInputForImGui({release, keyRelease});
  BeginTestFrame();
  Check(!io.MouseDown[ImGuiMouseButton_Left] &&
        !ImGui::IsKeyDown(ImGuiKey_A) && !io.KeyCtrl && !io.KeySuper,
        "Remote release state was not injected");
  ImGui::EndFrame();

  BeginTestFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
  ImGui::Begin("Remote input test", nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
  Check(!ImGui::Button("Click target"), "Button clicked without input");
  const ImVec2 buttonMin = ImGui::GetItemRectMin();
  const ImVec2 buttonMax = ImGui::GetItemRectMax();
  ImGui::End();
  ImGui::Render();

  InputEvent clickPosition;
  clickPosition.source = InputSource::Remote;
  clickPosition.type = InputEventType::PointerMove;
  clickPosition.x = (buttonMin.x + buttonMax.x) * 0.5f;
  clickPosition.y = (buttonMin.y + buttonMax.y) * 0.5f;
  InputEvent clickPress = clickPosition;
  clickPress.type = InputEventType::PointerButton;
  clickPress.button = PointerButton::Left;
  clickPress.action = InputAction::Press;
  QueueRemoteInputForImGui({clickPosition});
  BeginTestFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
  ImGui::Begin("Remote input test", nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
  Check(!ImGui::Button("Click target"), "Button clicked on pointer movement");
  Check(ImGui::IsItemHovered(), "Remote pointer did not hover the ImGui button");
  ImGui::End();
  ImGui::Render();

  QueueRemoteInputForImGui({clickPress});
  BeginTestFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
  ImGui::Begin("Remote input test", nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
  Check(!ImGui::Button("Click target"), "Button clicked before release");
  Check(ImGui::IsItemHovered(), "Remote pointer did not hover the ImGui button");
  Check(ImGui::IsItemActive(), "Remote press did not activate the ImGui button");
  ImGui::End();
  ImGui::Render();

  InputEvent clickRelease = clickPress;
  clickRelease.action = InputAction::Release;
  QueueRemoteInputForImGui({clickRelease});
  BeginTestFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
  ImGui::Begin("Remote input test", nullptr,
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
  Check(ImGui::Button("Click target"),
        "Remote press/release did not activate an ImGui button");
  ImGui::End();
  ImGui::Render();

  std::vector<InputEvent> events(5);
  events[0].source = InputSource::Remote;
  events[0].type = InputEventType::PointerMove;
  events[1].source = InputSource::Remote;
  events[1].type = InputEventType::PointerButton;
  events[2].source = InputSource::Remote;
  events[2].type = InputEventType::Key;
  events[2].capturedByUI = true;
  events[3].source = InputSource::Remote;
  events[3].type = InputEventType::Text;
  events[3].capturedByUI = true;
  events[4].source = InputSource::Local;
  events[4].type = InputEventType::PointerMove;
  events[4].capturedByUI = false;
  ApplyRemoteInputCapture(events, {true, false});
  Check(events[0].capturedByUI && events[1].capturedByUI,
        "Server mouse capture was not applied");
  Check(!events[2].capturedByUI && !events[3].capturedByUI,
        "Server keyboard capture did not replace the client hint");
  Check(!events[4].capturedByUI, "Local capture state was changed");

  ImGui::DestroyContext();
  std::cout << "Remote ImGui input checks passed\n";
  return 0;
}
