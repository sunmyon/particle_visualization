#pragma once
#ifndef M_PI
#define M_PI 3.141592653589793
#endif
#define _USE_MATH_DEFINES 

namespace physics_constants {
  inline constexpr double pi = 3.14159265358979323846;

  inline constexpr double proton_mass_cgs = 1.67262178e-24;
  inline constexpr double boltzmann_cgs   = 1.38065e-16;

  inline constexpr double XH  = 0.76;
  inline constexpr double XHe = 0.0625;

  inline constexpr double year_in_sec    = 3.15576e7;
  inline constexpr double solar_mass_g   = 1.989e33;
  inline constexpr double au_cm          = 1.49598e13;
  inline constexpr double pc_cm          = 3.085678e18;
  inline constexpr double kpc_cm         = 3.085678e21;
  inline constexpr double Mpc_cm         = 3.085678e24;
  inline constexpr double grav_const_cgs = 6.6743e-8;
}
