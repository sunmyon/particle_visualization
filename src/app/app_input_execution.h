#pragma once

#include "interaction/input_event.h"

class InteractionState;
struct CameraContext;
struct SettingsRuntimeState;

struct InputExecutionResult {
  bool closeRequested = false;
};

InputExecutionResult ExecuteInputEvents(InputEventQueue& input,
                                        InteractionState& interaction,
                                        CameraContext& camera,
                                        const SettingsRuntimeState& settings);
