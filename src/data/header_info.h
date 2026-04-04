#pragma once
struct HeaderInfo
{
  int npart;
  double time;            // "time" 属性 (例: Gadget系HDF5でのシミュレーション時刻)
  double cosmic_time;
  double boxSize;         // "BoxSize" 属性 (例)
  int    NumPart_ThisFile[6]; // "NumPart_Total" 属性 (6要素配列想定)
  double Omega0;
  double OmegaLambda;
  double HubbleParam;     // "HubbleParam"
  double massTable[6];    // "MassTable" (6要素配列想定)

  double UnitLength_in_cm;
  double UnitVelocity_in_cm_per_s;
  double UnitMass_in_g;

  bool   flag_comoving;
  bool   flag_density_in_cgs;
  bool   flag_B_in_cgs;
  bool   flag_hdf5;
};
