struct ClumpEvolutionInfo {
  int size = 0;
  int offset = 0;

  int index = -1;
  int next_index = -1;
  int stellar_count = 0;
  int stellar_id = -1;

  int global_id = -1;

  float stellar_mass = 0.0f;
  float stellar_mass_maximum = 0.0f;

  float mass = 0.0f;
  float density = 0.0f;
  float temperature = 0.0f;
  float temperature_d = 0.0f;
  float pos[3] = {0.0f, 0.0f, 0.0f};

  int snapindex = -1;
  float time = 0.0f;

  bool flag_star = false;

  float getValue(const std::string& var) const {
    if (var == "Density")      return density;
    if (var == "Temperature")  return temperature;
    if (var == "ClumpMass")    return mass;
    if (var == "StellarMass")  return stellar_mass;

    std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0.\n";
    return 0.0f;
  }
};

struct ClumpChainProperties {
  int first_snapshot = -1;
  int last_snapshot = -1;

  float first_time = 0.0f;
  float last_time = 0.0f;

  float density = 0.0f;
  float temperature = 0.0f;
  float temperature_d = 0.0f;

  int SF_snapshot = -1;
  float SF_time = 0.0f;

  int nstar = 0;
  float mstar = 0.0f;
  float mstar_maximum = 0.0f;
  float mass_maximum = 0.0f;

  int stellar_id = -1;
  int global_id = -1;
};
