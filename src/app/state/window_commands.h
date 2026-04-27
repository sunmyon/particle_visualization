#pragma once

#include <cstdint>
#include <vector>

enum class WindowId : uint8_t {
  RadialProfile,
  Histogram2D,
  ProjectionMap,
  ProjectionFontSelection,
  TopParticles,
  Haloes,
  Mask,
  FileFormatDialog,
  HDF5FormatDialog,
  ClumpFinder,
  ClumpList,
  ClumpChain,
};

enum class WindowCommandType : uint8_t {
  Open,
  Close,
  Focus,
};

struct WindowCommand {
  WindowCommandType type = WindowCommandType::Open;
  WindowId target = WindowId::ProjectionMap;
};

struct WindowCommandQueue {
  std::vector<WindowCommand> commands;

  void open(WindowId id) {
    commands.push_back({WindowCommandType::Open, id});
  }

  void close(WindowId id) {
    commands.push_back({WindowCommandType::Close, id});
  }

  void focus(WindowId id) {
    commands.push_back({WindowCommandType::Focus, id});
  }

  void clear() {
    commands.clear();
  }
};
