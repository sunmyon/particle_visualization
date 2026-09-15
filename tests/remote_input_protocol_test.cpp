#include "platform/remote_input_protocol.h"
#include "platform/remote_frame_flow_control.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void Check(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}

int main()
{
  using RemoteInputProtocol::Decode;

  RemoteFrameFlowControl flow;
  Check(!flow.tryBeginFrame(), "Frame began before the viewer was ready");
  flow.markViewerReady();
  Check(flow.tryBeginFrame(), "Initial dirty frame was not released");
  Check(!flow.tryBeginFrame(), "Consumed frame was released twice");
  flow.markViewerReady();
  Check(!flow.tryBeginFrame(), "Unchanged frame was released");
  flow.markDirty();
  const auto triggerTime = std::chrono::steady_clock::now();
  flow.markViewerReady(42, triggerTime);
  RemoteFrameTrigger trigger;
  Check(flow.tryBeginFrame(&trigger),
        "Dirty frame was not released to a ready viewer");
  Check(trigger.sequence == 42 && trigger.receivedAt == triggerTime,
        "Frame trigger identity or receive time was lost");

  Check(RemoteInputProtocol::IsFrameRequest(
          R"({"type":"frame_request","version":1})"),
        "Frame request control message was not recognized");
  Check(!RemoteInputProtocol::IsFrameRequest(
          R"({"type":"frame_request","version":2})"),
        "Unsupported frame request version was accepted");
  const auto move = Decode(R"({"type":"pointer_move","x":12.5,"y":24,
    "clientSequence":17,"primaryDown":true,"capturedByUI":true,"modifiers":{"shift":true},
    "viewport":{"x":3,"y":4,"width":800,"height":600,
    "framebufferScaleX":2,"framebufferScaleY":2}})");
  Check(move && move->type == InputEventType::PointerMove &&
    move->x == 12.5f && move->y == 24 && move->primaryDown &&
    move->capturedByUI && move->modifiers.shift &&
    move->viewport.x == 3 && move->viewport.height == 600 &&
    move->viewport.framebufferScaleX == 2 &&
    move->remoteSequence == 17 &&
    move->source == InputSource::Remote, "Legacy movement changed");
  Check(InputEvent{}.source == InputSource::Local, "Local default changed");
  const auto wheel = Decode(R"({"type":"pointer_scroll","wheelX":-0.5,"wheelY":2})");
  Check(wheel && wheel->type == InputEventType::PointerScroll &&
    wheel->wheelX == -0.5f && wheel->wheelY == 2, "Wheel axes lost");
  for (const auto* button : {"Left", "Right", "Middle"}) {
    for (const auto* action : {"Press", "Release"}) {
      const auto event = Decode(std::string(R"({"type":"pointer_button","button":")") +
        button + R"(","action":")" + action + R"("})");
      Check(event && event->type == InputEventType::PointerButton &&
        event->button != PointerButton::None &&
        (event->action == InputAction::Release) == (std::string(action) == "Release"),
        "Button transition lost");
    }
  }
  const auto escape = Decode(R"({"type":"key","key":"Escape","action":"Press"})");
  Check(escape && escape->key == InputKey::Escape &&
    escape->action == InputAction::Press, "Legacy Escape changed");
  const auto unknown = Decode(R"({"type":"key","key":""})");
  Check(unknown && unknown->key == InputKey::Unknown, "Legacy unknown key changed");
  const auto key = Decode(R"({"version":1,"type":"key","key":"A","action":"Repeat",
    "modifiers":{"ctrl":true,"alt":true,"super":true}})");
  Check(key && key->key == InputKey::A && key->action == InputAction::Repeat &&
    key->modifiers.ctrl && key->modifiers.alt && key->modifiers.super,
    "Keyboard modifiers/repeat lost");
  const auto text = Decode(R"({"type":"text","text":"日本語 🌌"})");
  Check(text && text->type == InputEventType::Text && text->text == "日本語 🌌",
    "UTF-8 text lost");
  const auto resize = Decode(R"({"type":"framebuffer_resize","width":2560,"height":1440,
    "displayWidth":1280,"displayHeight":720,"framebufferScaleX":2,"framebufferScaleY":2,
    "presentationMode":"idle"})");
  Check(resize && resize->type == InputEventType::FramebufferResize &&
    resize->width == 2560 && resize->height == 1440 &&
    resize->displayWidth == 1280 && resize->displayHeight == 720 &&
    resize->framebufferScaleX == 2 && resize->framebufferScaleY == 2 &&
    resize->idlePresentation,
    "Resize/DPI metrics lost");
  const auto legacyResize = Decode(
    R"({"type":"framebuffer_resize","width":1280,"height":720})");
  Check(legacyResize && legacyResize->displayWidth == 1280 &&
    legacyResize->displayHeight == 720 &&
    legacyResize->framebufferScaleX == 1,
    "Legacy resize defaults changed");

  for (const auto* bad : {
    "", "{", "null", "[]", "1", "{}", R"({"type":"typo"})",
    R"({"type":1})", R"({"type":"key","version":2})",
    R"({"type":"key","version":1.5})", R"({"type":"key","action":"typo"})",
    R"({"type":"key","key":"typo"})", R"({"type":"pointer_move","x":"bad"})",
    R"({"type":"pointer_move","x":1e100})", R"({"type":"pointer_move","x":1e999})",
    R"({"type":"pointer_move","primaryDown":1})", R"({"type":"key","modifiers":[]})",
    R"({"type":"key","modifiers":{"ctrl":"true"}})",
    R"({"type":"key","viewport":null})", R"({"type":"key","viewport":{"width":0}})",
    R"({"type":"key","clientSequence":-1})",
    R"({"type":"key","clientSequence":1.5})",
    R"({"type":"key","viewport":{"framebufferScaleX":-1}})",
    R"({"type":"framebuffer_resize","width":1.5,"height":1})",
    R"({"type":"framebuffer_resize","width":4294967297,"height":1})",
    R"({"type":"framebuffer_resize","width":18446744073709551615,"height":1})",
    R"({"type":"framebuffer_resize","width":8193,"height":1})",
    R"({"type":"framebuffer_resize","width":8192,"height":8192})",
    R"({"type":"framebuffer_resize","width":10,"height":10,"displayWidth":10})",
    R"({"type":"framebuffer_resize","width":10,"height":10,"displayWidth":10,"displayHeight":10,"framebufferScaleX":0})",
    R"({"type":"framebuffer_resize","width":10,"height":10,"presentationMode":"preview"})",
    R"({"type":"framebuffer_resize","width":-1,"height":1})",
    R"({"type":"framebuffer_resize"})", R"({"type":"pointer_button"})",
    R"({"type":"pointer_button","button":"Left"})",
    R"({"type":"pointer_button","button":"Left","action":"Repeat"})",
    R"({"type":"text"})", R"({"type":"text","text":12})",
    R"({"type":"text","text":"\u0000"})", R"({"type":"text","text":"\ud800"})"
  }) {
    Check(!Decode(bad), bad);
  }
  Check(!Decode(std::string(RemoteInputProtocol::MaxMessageBytes + 1, ' ')),
    "Oversize input accepted");
  Check(!Decode(std::string("{\"type\":\"text\",\"text\":\"") + char(0xff) + "\"}"),
    "Invalid UTF-8 accepted");

  InputEventQueue queue;
  queue.push(*move);
  queue.push(*text);
  queue.push(*resize);
  const auto events = queue.drain();
  Check(events.size() == 3 && events[0].x == move->x &&
    events[1].text == text->text && events[2].width == 2560 && queue.empty(),
    "Queue must preserve ordered owned event payloads");
  queue.push(*resize);
  queue.prepend({*text, *move});
  const auto restored = queue.drain();
  Check(restored.size() == 3 && restored[0].type == InputEventType::Text &&
    restored[1].type == InputEventType::PointerMove &&
    restored[2].type == InputEventType::FramebufferResize,
    "Restored frame input must remain ahead of newly received input");
  std::cout << "Remote input protocol checks passed\n";
}
