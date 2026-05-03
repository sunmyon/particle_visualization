#pragma once
#include <string>
#include "core/quantity.h"

enum class DataSource : int { Gas = 0, DM = 1, Stars = 2 };
enum class StarQuantity : int { Density=0, Metallicity=1, Mass=2, Flux=3 };

struct FluxSettings {
  float band_center_nm = 1500.0f;
  float band_width_nm  = 200.0f;
};

struct ProjectionMapParams {
  int npixel = 200;

  float xlen[3] = {2.f, 2.f, 1.f};
  float xoffset[3] = {0.f, 0.f, 0.f};
  float tilt[3] = {0.f, 0.f, 0.f};

  bool flagDensityWeight = true;
  bool flagVoronoi = true;
  bool useGpuProjection = false;
  int step_z = 200;

  bool flagLogScale = true;
  bool autoRange = true;
  float range_min = 0.0f;
  float range_max = 1.0f;

  bool flagShowStarParticles = true;
  bool flagShowCuboid = false;

  bool flagSpecifyZoomRegionByMass = false;
  float criticalGasMassForZoomRegion = 0.0f;
  float lenZoomRegion = 0.0f;

  bool flagPlaceScale = false;
  float arrowLenX = 100.0f;
  char arrowLabelStr[255] = "100 au";

  bool flagTimeLabel = true;
  bool flagUseRedshift = false;
  char timeFormatBuf[255] = "t=%.3f";

  char fileFormat[255] = "image_%04d.png";
  char folderPath[255] = "./output";

  std::string var;

  int selectedAxis = 2;
  int selectedType = 0;
  int colormapindex = 0;

  float factorShownTimeInUnitTime = 1.0f;

  DataSource dataSource = DataSource::Gas;
  StarQuantity starQuantity = StarQuantity::Density;
  FluxSettings flux;
  float psf_sigma_pix = 1.5f;
  QuantityId selectedVarGas = QuantityId::Density;

  bool multiPanelEnabled = false;
  int multiPanelRows = 2;
  int multiPanelCols = 2;
  int multiPanelCount = 4;
  bool multiPanelShowTimeLabel[6] = {true, true, true, true, true, true};
  bool multiPanelShowScale[6] = {true, true, true, true, true, true};
  QuantityId multiPanelVars[6] = {
    QuantityId::Density,
    QuantityId::Temperature,
    QuantityId::Mass,
    QuantityId::Hsml,
    QuantityId::Density,
    QuantityId::Temperature
  };

  char filterExpr[256] = "return m > 10.0";
  char pointSizeExpr[256] = "return m / 10.0";
  char pointColorExpr[256] = "return { r = m/100.0, g = 0.5, b = 0.2, a = 1.0 }";
  char minValueExpr[32] = "return 0.0";
  char maxValueExpr[32] = "return 1.0";
};
