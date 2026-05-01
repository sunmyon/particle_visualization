#pragma once

#include "core/tracking_vector.h"
struct SnapshotNavigationState;
struct SnapshotCurrentState;
struct PlotBatchExportViewContext;

struct ClumpFinderWindowState;
struct LoadedClumpWindowState;
struct ClumpChainWindowState;

void DrawClumpFinderUI(ClumpFinderWindowState& ui,
                       const PlotBatchExportViewContext& exportContext);

void DrawClumpListUI(LoadedClumpWindowState& ui,
                     const PlotBatchExportViewContext& exportContext);

void DrawClumpChainListUI(ClumpChainWindowState& ui,
			  const SnapshotNavigationState& nav,
                          const SnapshotCurrentState& current,
                          const PlotBatchExportViewContext& exportContext);
