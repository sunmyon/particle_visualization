#pragma once

#include "app/state/tool_window_state.h"
#include "app/state/window_commands.h"

inline void SetToolWindowOpen(ToolWindowUIState& windows,
                              WindowId id,
                              bool open)
{
  switch (id) {
  case WindowId::RadialProfile:
    windows.radialProfile.open = open;
    break;
  case WindowId::Histogram2D:
    windows.histogram2D.open = open;
    break;
  case WindowId::ProjectionMap:
    windows.projectionMap.open = open;
    break;
  case WindowId::ProjectionFontSelection:
    windows.projectionMap.fontWindowOpen = open;
    break;
  case WindowId::TopParticles:
    windows.topParticles.open = open;
    break;
  case WindowId::Haloes:
    windows.haloes.open = open;
    break;
  case WindowId::Mask:
    windows.mask.open = open;
    break;
  case WindowId::FileFormatDialog:
    windows.fileFormatDialog.showFormatDialog = open;
    break;
  case WindowId::HDF5FormatDialog:
#ifdef HAVE_HDF5
    windows.fileFormatDialog.showHDF5MappingDialog = open;
#endif
    break;
  case WindowId::ClumpFinder:
    windows.clumpFind.open = open;
    break;
  case WindowId::ClumpList:
    windows.clumpList.open = open;
    break;
  case WindowId::ClumpChain:
    windows.clumpChain.open = open;
    break;
  }
}

inline void ApplyWindowCommands(WindowCommandQueue& queue,
                                ToolWindowUIState& windows)
{
  for (const WindowCommand& command : queue.commands) {
    switch (command.type) {
    case WindowCommandType::Open:
      SetToolWindowOpen(windows, command.target, true);
      break;
    case WindowCommandType::Close:
      SetToolWindowOpen(windows, command.target, false);
      break;
    case WindowCommandType::Focus:
      SetToolWindowOpen(windows, command.target, true);
      break;
    }
  }

  queue.clear();
}
