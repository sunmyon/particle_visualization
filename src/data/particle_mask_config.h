struct ParticleMaskConfig {
  bool  enableSphere = false;
  float center[3] = {0,0,0};
  float radius = 0.0f;

  enum class OutsideMode : int { Drop=0, Thin=1, KeepAll=2 };
  OutsideMode outsideMode = OutsideMode::Drop;
  int outsideStride = 10;

  enum class TypeMode : int { Off=0, On_NoThin=1, On_ThinOK=2 };
  TypeMode typeMode[6] = { TypeMode::On_ThinOK, TypeMode::On_ThinOK,
                           TypeMode::On_NoThin, TypeMode::On_NoThin,
                           TypeMode::On_NoThin, TypeMode::On_NoThin };

  bool enableMaxParticles = false;
  int  maxParticles = 2'000'000;
};
