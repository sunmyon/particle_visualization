#pragma once

struct PlotBatchExportViewContext {
  const char* snapshotFolderPath = nullptr;
  const char* snapshotFileFormat = nullptr;
  int initialIndex = 0;
  int currentStep = 0;
  int skipStep = 1;
  int batchSize = 1;
  bool useHDF5 = false;
  float renderToWorldScale = 1.0f;
  float cameraPosition[3] = {0.0f, 0.0f, 5.0f};
  float cameraTarget[3] = {0.0f, 0.0f, 0.0f};
};
