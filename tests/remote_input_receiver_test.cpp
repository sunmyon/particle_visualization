#include "platform/remote_input_receiver.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

int main()
{
  // A unique local IPC endpoint avoids selecting or reserving a TCP port.
  const auto path = std::filesystem::temp_directory_path() /
    ("particle-input-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto endpoint = "ipc://" + path.string();
  bool ok = false;
  try {
    InputEventQueue queue;
    RemoteInputReceiver receiver;
    if (!receiver.start(endpoint, queue) || !receiver.active())
      throw std::runtime_error("Receiver failed to start");
    if (receiver.start(endpoint, queue))
      throw std::runtime_error("Double start accepted");
    {
      zmq::context_t context{1};
      zmq::socket_t sender{context, zmq::socket_type::push};
      sender.set(zmq::sockopt::linger, 0);
      sender.set(zmq::sockopt::sndtimeo, 2000);
      sender.connect(endpoint);
      for (const std::string message : {
        R"({"type":"key","key":"Escape"})",
        R"({"type":"pointer_move","x":{}})",
        "null", "{", R"({"type":"unsupported"})",
        R"({"type":"pointer_button","button":"Right","action":"Release"})",
        R"({"type":"text","text":"日本語"})"
      }) {
        if (!sender.send(zmq::buffer(message), zmq::send_flags::none))
          throw std::runtime_error("Send timed out");
      }
      std::vector<InputEvent> events;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (events.size() < 3 && std::chrono::steady_clock::now() < deadline) {
        auto batch = queue.drain();
        events.insert(events.end(), batch.begin(), batch.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (events.size() != 3 || events[0].key != InputKey::Escape ||
          events[1].type != InputEventType::PointerButton ||
          events[1].button != PointerButton::Right ||
          events[1].action != InputAction::Release ||
          events[2].text != "日本語" || events[2].source != InputSource::Remote)
        throw std::runtime_error("Valid input lost or invalid input enqueued");
    }
    receiver.stop();
    receiver.stop();
    if (receiver.active() || !receiver.endpoint().empty())
      throw std::runtime_error("Receiver did not stop");
    if (!receiver.start(endpoint, queue))
      throw std::runtime_error("Receiver failed to restart");
    receiver.stop();
    ok = true;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return ok ? 0 : 1;
}
