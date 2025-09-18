#include "tau_sph.h"
#define _USE_MATH_DEFINES 
#include <cmath>

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

#include <cassert>

namespace sphlut {

  // ---- cubic ----
  static inline float w_cubic(float q){
    if (q < 0.0f) return 0.0f;
    if (q < 1.0f) { float q2=q*q, q3=q2*q; return 1.0f - 1.5f*q2 + 0.75f*q3; }
    if (q < 2.0f) { float t=2.0f-q; return 0.25f*t*t*t; }
    return 0.0f;
  }
  
  static inline void gauss16(const float*& x, const float*& w, int& n){
    static const float X[16]={-0.9894009f,-0.9445750f,-0.8656312f,-0.7554044f,-0.6178762f,-0.4580168f,-0.2816036f,-0.0950125f,
			      0.0950125f, 0.2816036f, 0.4580168f, 0.6178762f, 0.7554044f, 0.8656312f, 0.9445750f, 0.9894009f};
    static const float W[16]={ 0.02715246f,0.06225352f,0.09515851f,0.12462897f,0.14959599f,0.16915652f,0.18260342f,0.18945061f,
                               0.18945061f,0.18260342f,0.16915652f,0.14959599f,0.12462897f,0.09515851f,0.06225352f,0.02715246f};
    x=X; w=W; n=16;
  }
  
  static float integrate_line(float q, float R, float (*wfunc)(float)){
    float smax = std::sqrt(std::max(0.0f, R*R - q*q));
    if (smax <= 0.0f)
      return 0.0f;
    
    const float *xi, *wi;
    int n;
    gauss16(xi,wi,n);

    float sum=0.0f;
    for(int i=0;i<n;i++){
      float t = 0.5f * ( (xi[i]) * (2.0f*smax) );
      float qq = std::sqrt(q*q + t*t);
      sum += wi[i] * wfunc(qq);
    }
    return sum * smax;
  }

  TauLUT buildTauLUT(Kernel k, int samples){
    assert(samples>=2);
    TauLUT lut{};
    switch(k){
    case Kernel::CubicSpline:
      lut.R = 2.0f;
      lut.alpha3 = float(1.0/M_PI);
      
      break;
    }
    
    lut.data.resize(samples);
    for (int i=0;i<samples;i++){
      float q = lut.R * (float(i)/(samples-1));
      float Jq = integrate_line(q, lut.R, w_cubic);
      lut.data[i] = Jq;
    }
    return lut;
  }
} // namespace sphlut
