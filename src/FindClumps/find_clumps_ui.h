#pragma once

#include "core/tracking_vector.h"
struct SnapshotNavigationState;
struct SnapshotCurrentState;

struct ClumpFinderWindowState;
struct LoadedClumpWindowState;
struct ClumpChainWindowState;

void DrawClumpFinderUI(ClumpFinderWindowState& ui);

void DrawClumpListUI(LoadedClumpWindowState& ui);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  const SnapshotNavigationState& nav,
                          const SnapshotCurrentState& current);
