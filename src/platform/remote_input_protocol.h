#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "interaction/input_event.h"

namespace RemoteInputProtocol {

// One JSON object per transport message. No socket, window or ImGui dependency.
inline constexpr std::size_t MaxMessageBytes = 64 * 1024;
inline constexpr int MaxFramebufferDimension = 8192;
inline constexpr std::size_t MaxFramebufferPixels = 32 * 1024 * 1024;

// Invalid/unsupported messages produce no event. Legacy unversioned messages
// use version 1. UI capture is retained as a legacy hint, not a UI authority.
std::optional<InputEvent> Decode(std::string_view message);

} // namespace RemoteInputProtocol
