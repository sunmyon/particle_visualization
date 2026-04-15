#pragma once

#include <cmath>
#include "core/physics_constants.h"

struct UnitSystem {
  double length_cm = physics_constants::pc_cm;
  double mass_g = physics_constants::solar_mass_g;
  double velocity_cm_per_s = 1.0e5;

  double length_pc = 1.0;
  double mass_msun = 1.0;
  double time_s = 1.0;
  double time_yr = 1.0;

  double hubble = 1.0;
  double grav_const_internal = physics_constants::grav_const_cgs;

  bool useComovingCoordinate = true;

  double mdot_in_msun_per_yr(){
    return mass_msun / time_yr;
  }
  
  void updateDerived()
  {
    length_pc = length_cm / physics_constants::pc_cm;
    mass_msun = mass_g / physics_constants::solar_mass_g;
    time_s = length_cm / velocity_cm_per_s;
    time_yr = time_s / physics_constants::year_in_sec;

    grav_const_internal =
      physics_constants::grav_const_cgs /
      std::pow(length_cm, 3) *
      mass_g *
      std::pow(time_s, 2);
  }

  void setLengthToAU()  { length_cm = physics_constants::au_cm; }
  void setLengthToPC()  { length_cm = physics_constants::pc_cm; }
  void setLengthToKPC() { length_cm = physics_constants::kpc_cm; }
  void setLengthToMPC() { length_cm = physics_constants::Mpc_cm; }

  void setMassToSolar()      { mass_g = physics_constants::solar_mass_g; }
  void setMassTo1e10Solar()  { mass_g = 1.0e10 * physics_constants::solar_mass_g; }
};
