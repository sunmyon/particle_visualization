#pragma once
struct HeaderInfo
{
  int npart = 0;
  double time = 0.0;            // time or scale factor (legacy)
  double redshift = 0.0;
  double boxSize = 0.0;         // "BoxSize" 属性 (例)
  int    NumPart_ThisFile[6] = {0, 0, 0, 0, 0, 0}; // "NumPart_Total" 属性 (6要素配列想定)
  double Omega0 = 0.0;
  double OmegaLambda = 0.0;
  double HubbleParam = 1.0;     // "HubbleParam"
  double massTable[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // "MassTable" (6要素配列想定)

  double UnitLength_in_cm = 1.0;
  double UnitVelocity_in_cm_per_s = 1.0;
  double UnitMass_in_g = 1.0;

  bool   flag_comoving = false;
  bool   flag_density_in_cgs = false;
  bool   flag_B_in_cgs = false;
  bool   flag_hdf5 = false;

  bool   has_redshift = false;
};
