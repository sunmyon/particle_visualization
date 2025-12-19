#include <nanoflann.hpp>
#include <Eigen/Core>
#include "main.h"
#include <StreamLine/stream_line.h>

void StreamlineComputer::setRegionFromParticleData(TrackingVector<ParticleData>& particles){  
  if(flagSetRegionByHand == true)
    return;
  
  for(int k=0;k<3;k++){
    xmin_seed[k] = 1.e100;
    xmax_seed[k] = -1.e100;
  }

  for(auto &p : particles){
    if(p.type != 0) continue;

    for(int k=0;k<3;k++){
      if(p.pos[k] < xmin_seed[k]) xmin_seed[k] = p.pos[k];
      if(p.pos[k] > xmax_seed[k]) xmax_seed[k] = p.pos[k];
    }
  }  
}

void StreamlineComputer::setRegionByHand(float *center, float *len){
  for(int k=0;k<3;k++){
    xmin_seed[k] = center[k] - 0.5 * len[k];
    xmax_seed[k] = center[k] + 0.5 * len[k];
  }

  flagSetRegionByHand = true;
}

void StreamlineComputer::setStreamRegionFromParticleData(TrackingVector<ParticleData>& particles){  
  if(flagSetStreamRegionByHand == true)
    return;
  
  for(int k=0;k<3;k++){
    xmin_[k] = 1.e100;
    xmax_[k] = -1.e100;
  }

  for(auto &p : particles){
    if(p.type != 0) continue;

    for(int k=0;k<3;k++){
      if(p.pos[k] < xmin_[k]) xmin_[k] = p.pos[k];
      if(p.pos[k] > xmax_[k]) xmax_[k] = p.pos[k];
    }
  }  
}

void StreamlineComputer::setStreamRegionByHand(float *center, float *len){
  for(int k=0;k<3;k++){
    xmin_[k] = center[k] - 0.5 * len[k];
    xmax_[k] = center[k] + 0.5 * len[k];
  }

  flagSetStreamRegionByHand = true;
}


void StreamlineComputer::setSeeds(TrackingVector<ParticleData>& particles, int n_seeds){
  int count = 0;
  for(auto &p : particles){
    if(p.type != 0) continue;

    if(p.pos[0] < xmin_seed[0] || p.pos[0] > xmax_seed[0])
      continue;
    if(p.pos[1] < xmin_seed[1] || p.pos[1] > xmax_seed[1])
      continue;
    if(p.pos[2] < xmin_seed[2] || p.pos[2] > xmax_seed[2])
      continue;      

    count++;
  }

  int nparticles_in_region = count;
  int lines_per_particle = nparticles_in_region / n_seeds;
  if(n_seeds > max_stream_lines){
    lines_per_particle = nparticles_in_region / max_stream_lines;
    n_seeds = nparticles_in_region / lines_per_particle;
  }
  
  m_seeds.clear();
  m_seeds.reserve(n_seeds);

  m_hsmls.clear();
  m_hsmls.reserve(n_seeds);
  
  count = 0;
  for(auto &p : particles){
    if(p.type != 0) continue;
      
    if(p.pos[0] < xmin_seed[0] || p.pos[0] > xmax_seed[0])
      continue;
    if(p.pos[1] < xmin_seed[1] || p.pos[1] > xmax_seed[1])
      continue;
    if(p.pos[2] < xmin_seed[2] || p.pos[2] > xmax_seed[2])
      continue;      

    if(count % lines_per_particle == 0){
      Vec3 pos;
      pos.x = p.pos[0];
      pos.y = p.pos[1];
      pos.z = p.pos[2];

      float hsml = p.Hsml;
      float v2 = p.vel[0]*p.vel[0]+p.vel[1]*p.vel[1]+p.vel[2]*p.vel[2];      
      float t_step = hsml;
      if(v2 > 0.)
	t_step = hsml / sqrt(v2);
      
      m_seeds.push_back(pos);
      m_hsmls.push_back(t_step);
    }
    
    count++;
  }
}

void StreamlineComputer::build(ParticleBlock& particles,
			       double theta_max_in_degree = 10.0f){

  printf("estimating grad vector field...\n");
  estimate_gradB(particles);
  printf("done!\n");
  
  float theta_max = theta_max_in_degree * 3.14159265f / 180.0f;

  m_lines.clear();
  m_lines.reserve(m_seeds.size());
  for(size_t i=0;i<m_seeds.size();i++){
    auto &s = m_seeds[i];
    float h_init = 0.001 * m_hsmls[i];
    //float h_init = 0.001 * (xmax_[0] - xmin_[0]);
    
    printf("init: %zu\n", i);
    
    std::vector<Vec3> stream;
    stream = integrateBiStreamline(s, h_init, MaxStep);

    printf("stream size:%zu\n", stream.size());
    
    std::vector<Vec3> sampled;
    sampled = sampleByCurvature(stream, theta_max);
    m_lines.push_back(sampled);
    //m_lines.push_back(stream);

    printf("stream size sampled:%zu\n", sampled.size());
  }

  flattenLines_();
}

void StreamlineComputer::estimate_gradB(ParticleBlock& particleBlock){
  TrackingVector<struct particle_stream> p_stream;

  bool flag_Bfield = particleBlock.hasBfield();
  
  for(size_t i=0; i< particleBlock.particles.size();i++){
    ParticleData& p = particleBlock.particles[i];
    
    if(p.type != 0) continue;
      
    if(p.pos[0] < xmin_[0] || p.pos[0] > xmax_[0])
      continue;
    if(p.pos[1] < xmin_[1] || p.pos[1] > xmax_[1])
      continue;
    if(p.pos[2] < xmin_[2] || p.pos[2] > xmax_[2])
      continue;
    
    struct particle_stream ps;
    for(int k=0;k<3;k++)
      ps.pos[k] = p.pos[k];

    if(flag_Bfield){
      const float *bf = particleBlock.getBfield(i);
      ps.vect[0] = bf[0];
      ps.vect[1] = bf[1];
      ps.vect[2] = bf[2];
    }else{
      for(int k=0;k<3;k++)
	ps.vect[k] = p.vel[k];
    }

    p_stream.push_back(ps);
  }

  cloud.particles = p_stream;
  
  m_kdTree.reset( new KDTreeType(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */)));
  m_kdTree->buildIndex();

  TrackingVector<KDTreeType::IndexType> ret_indices(N_neighbours);
  TrackingVector<float> out_dist2(N_neighbours);
  
  nanoflann::SearchParameters params;
  gradB.clear();
  gradB.resize(cloud.particles.size());

  r_neighbours.clear();
  r_neighbours.resize(cloud.particles.size());
  
  for (size_t i = 0; i < cloud.particles.size(); i++) {
    const auto& pi = cloud.particles[i];
    float query_pt[3] = { pi.pos[0], pi.pos[1], pi.pos[2] };

    size_t num_results = m_kdTree->knnSearch(query_pt, N_neighbours, ret_indices.data(), out_dist2.data());    
    if (num_results == 0)
      continue;

    r_neighbours[i] =  sqrt(out_dist2[num_results - 1]);
    
    // 2) 正規方程式系の構築：∑_j w_j (∇B · (c_j-c_i) ≈ B[j]-B[i])
    //    ここでは単純に w_j=1 として
    Eigen::Matrix3f M = Eigen::Matrix3f::Zero();
    Eigen::Vector3f bx = Eigen::Vector3f::Zero();
    Eigen::Vector3f by = Eigen::Vector3f::Zero();
    Eigen::Vector3f bz = Eigen::Vector3f::Zero();
    for (int t = 0; t < N_neighbours; ++t) {
      int idx = ret_indices[t];
      const auto& pj = cloud.particles[idx];
      
      Eigen::Vector3f d = Eigen::Vector3f::Zero();
      for(int k = 0; k < 3; ++k)
	d[k] = pj.pos[k] - pi.pos[k];
      
      M += d * d.transpose(); 
      bx += d * (pj.vect[0] - pi.vect[0]); 
      by += d * (pj.vect[1] - pi.vect[1]); 
      bz += d * (pj.vect[2] - pi.vect[2]); 
    }
    
    // 3) ∇B = M^{-1} * [bx,by,bz]
    Eigen::Matrix3f Minv = M.inverse();
    gradB[i][0] = Minv * bx;  // ∇B_x
    gradB[i][1] = Minv * by;  // ∇B_y
    gradB[i][2] = Minv * bz;  // ∇B_z
  }  
}



void StreamlineComputer::evalFieldAt(const float x[3], float outB[3], float &hsml){
  double dist2 = (x[0]-x_ref[0])*(x[0]-x_ref[0]) + (x[1]-x_ref[1])*(x[1]-x_ref[1]) + (x[2]-x_ref[2])*(x[2]-x_ref[2]);
  double dist_to_border = radius_list - sqrt(dist2);
  double dist_nearest_next;
  
  bool flag_find_neighbours = false;
  if(dist_to_border < dist_nearest)
    flag_find_neighbours = true;

  int idx;
  
  if(flag_find_neighbours){
    m_kdTree->knnSearch(x, N_neighbours, indices_list, distances_list2);

    idx = indices_list[0];
    dist_nearest = sqrt(distances_list2[0]);
    dist_nearest_next = sqrt(distances_list2[1]);
    
    radius_list = sqrt(distances_list2[N_neighbours - 1]);
    
    x_ref[0] = x[0];
    x_ref[1] = x[1];
    x_ref[2] = x[2];
  }else{
    int index_min = -1;
    double dist_min2 = 1.e100, dist_min_next2 = 1.e100;
    for(int i=0;i<N_neighbours;i++){
      int id_list = indices_list[i];
      const auto& p = cloud.particles[id_list];
      double dist_ind2 = (x[0]-p.pos[0])*(x[0]-p.pos[0]) + (x[1]-p.pos[1])*(x[1]-p.pos[1]) + (x[2]-p.pos[2])*(x[2]-p.pos[2]);
      if(dist_min2 > dist_ind2){
	dist_min_next2 = dist_min2;
	
	dist_min2 = dist_ind2;
	index_min = id_list;
      }else if(dist_min_next2 > dist_ind2){
	dist_min_next2 = dist_ind2;	
      }
    }
    
    idx = index_min;
    dist_nearest = sqrt(dist_min2);
    dist_nearest_next = sqrt(dist_min_next2);
  }

  printf("flag=%d dist=%g neearest_next=%g\n", flag_find_neighbours, dist_nearest, dist_nearest_next);
  
  hsml = dist_nearest_next;
  
  auto &p  = cloud.particles[idx];
  auto &g  = gradB[idx];

  float d0 = x[0] - p.pos[0];
  float d1 = x[1] - p.pos[1];
  float d2 = x[2] - p.pos[2];
  for(int k=0;k<3;++k)
    outB[k] = p.vect[k] + g[0][k]*d0 + g[1][k]*d1 + g[2][k]*d2;

  //printf("idx=%d vec=%g %g %g out=%g %g %g\n", idx, p.vect[0], p.vect[1], p.vect[2], outB[0], outB[1], outB[2]);
}




std::vector<Vec3> StreamlineComputer::integrateBiStreamline(const Vec3 &seed, float h, int maxSteps)
{
  // 1) seed から正方向の流線
  auto forward = integrateStreamline(seed,  h, maxSteps);
  
  // 2) seed から逆方向の流線
  auto backward = integrateStreamline(seed, -h, maxSteps);
  
  // 3) backward は seed→逆方向へ進んでいるので、順序反転して seed を末尾に
  std::reverse(backward.begin(), backward.end());
  
  // 4) seed が両方に含まれて重複するので、どちらか一方の seed を外す
  backward.pop_back();  // これで seed は forward の先頭だけに
  
  // 5) backward＋forward で１本につなげる
  std::vector<Vec3> fullLine;
  fullLine.reserve(backward.size() + forward.size());
  fullLine.insert(fullLine.end(),   backward.begin(), backward.end());
  fullLine.insert(fullLine.end(),   forward.begin(),   forward.end());
  
  return fullLine;
}

// seed から流線を積分
std::vector<Vec3> StreamlineComputer::integrateStreamline(const Vec3 &seed, float h_input, int maxSteps){
  std::vector<Vec3> line;
  Vec3 x = seed;
  float h = h_input;
  for(int i=0;i<maxSteps;++i){
    line.push_back(x);
    Vec3 x_next = RK45stepAdaptive(x, h, 0.001);
    //Vec3 x_next = EulerstepAdaptive(x, h, 0.001);
    x = x_next;

    if(i%1 == 0)
      printf("pos=%g %g %g h=%g\n", x.x, x.y, x.z, h);
    
    if(is_inside_(x) == false)
      break;
  }
  
  return line;
}

bool StreamlineComputer::is_inside_(Vec3 &x){
  if(x.x < xmin_[0] || x.x > xmax_[0])
    return false;
  if(x.y < xmin_[1] || x.y > xmax_[1])
    return false;
  if(x.z < xmin_[2] || x.z > xmax_[2])
    return false;

  return true;
}


// 角度閾値 theta_max (rad) に基づくサンプリング
std::vector<Vec3> StreamlineComputer::sampleByCurvature(const std::vector<Vec3>& fullLine, float theta_max_rad)
{
  std::vector<Vec3> out;
  size_t N = fullLine.size();
  if(N==0) return out;
  out.push_back(fullLine[0]);

  for(size_t i=1; i+1<N; ++i){
    // 前後のベクトル
    Vec3 v1 = fullLine[i]   - out[out.size()-1];
    Vec3 v2 = fullLine[i+1] - fullLine[i];
    float l1 = len(v1), l2 = len(v2);
    if(l1<1e-6f || l2<1e-6f)
      continue;
    
    float cosTh = dot(v1,v2)/(l1*l2);
    cosTh = std::clamp(cosTh, -1.0f, 1.0f);
    float theta = std::acos(cosTh);
    if(theta > theta_max_rad){
      // 急カーブ部なので必ずキープ
      out.push_back(fullLine[i]);
    }
    // 平坦部はスキップ
  }
  // 末尾も必ず入れる
  if(N>1) out.push_back(fullLine[N-1]);
  return out;
}

void StreamlineComputer::flattenLines_()
{
  auto& M = m_mesh;           // StreamlineMeshData
  M.vertices.clear();
  M.firsts.clear();
  M.counts.clear();

  size_t cursor = 0;
  for (auto& line : m_lines) {
    M.firsts.push_back(cursor);
    M.counts.push_back( line.size() );
    for (auto& p : line) {
      M.vertices.push_back(p.x);
      M.vertices.push_back(p.y);
      M.vertices.push_back(p.z);
    }
    cursor += line.size();
  }
}


// coefficients for Dormand-Prince (RK45)
static constexpr float c2=1.f/5,  c3=3.f/10,  c4=4.f/5,  c5=8.f/9,   c6=1.f,     c7=1.f;
static constexpr float a21=1.f/5;
static constexpr float a31=3.f/40,  a32=9.f/40;
static constexpr float a41=44.f/45, a42=-56.f/15, a43=32.f/9;
static constexpr float a51=19372.f/6561, a52=-25360.f/2187, a53=64448.f/6561, a54=-212.f/729;
static constexpr float a61=9017.f/3168,  a62=-355.f/33,    a63=46732.f/5247,  a64=49.f/176,    a65=-5103.f/18656;
static constexpr float a71=35.f/384,     a72=0,            a73=500.f/1113,    a74=125.f/192,   a75=-2187.f/6784, a76=11.f/84;

// high-order (5th) weights:
static constexpr float b1=a71, b2=a72, b3=a73, b4=a74, b5=a75, b6=a76, b7=0;
// low-order (4th) weights:
static constexpr float b1s=5179.f/57600, b2s=0, b3s=7571.f/16695, b4s=393.f/640, b5s=-92097.f/339200, b6s=187.f/2100, b7s=1.f/40;

// Perform one adaptive RK45 step.
//   x: current state (Vec3)  
//   h: current step size (in/out)—will be updated  
//   tol: desired local error tolerance
// Returns: next state (Vec3), and modifies h to the suggested next step.
Vec3 StreamlineComputer::RK45stepAdaptive(const Vec3 &x, float &h, float tol) {
  float h_nearest;
  Vec3 x5, k1, k2, k3, k4, k5, k6, k7;

  auto eval = [&](const Vec3 &p, Vec3 &outB, float &hsml){
    float buf[3];
    evalFieldAt( &p.x, buf, hsml );
    outB = Vec3{ buf[0], buf[1], buf[2] };
  };

  while(1){
    // k1 = f(x)
    eval(x, k1, h_nearest);

    // k2 = f(x + h*a21*k1)
    Vec3 xtmp = x + k1*(h*a21);
    eval(xtmp, k2, h_nearest);

    // k3 = f(x + h*(a31*k1 + a32*k2))
    xtmp = x + (k1*a31 + k2*a32)*h;
    eval(xtmp, k3, h_nearest);

    // k4
    xtmp = x + (k1*a41 + k2*a42 + k3*a43)*h;
    eval(xtmp, k4, h_nearest);

    // k5
    xtmp = x + (k1*a51 + k2*a52 + k3*a53 + k4*a54)*h;
    eval(xtmp, k5, h_nearest);

    // k6
    xtmp = x + (k1*a61 + k2*a62 + k3*a63 + k4*a64 + k5*a65)*h;
    eval(xtmp, k6, h_nearest);

    // compute 5th-order result:
    x5 = x + (k1*b1 + k2*b2 + k3*b3 + k4*b4 + k5*b5 + k6*b6)*h;

    // k7 (for 4th-order estimate)
    // note: Dormand–Prince does not need an extra eval; can reuse x5 as input.
    eval(x5, k7, h_nearest);

    // compute 4th-order result:
    Vec3 x4 = x + (k1*b1s + k3*b2s + k3*b3s + k4*b4s + k5*b5s + k6*b6s + k7*b7s)*h;

    // estimate the local error:
    Vec3 e = x5 - x4;
    float err = len(e);

    // update step size:
    const float safety = 0.9f;
    const float minFactor = 0.2f, maxFactor = 5.0f;
    // h_new = safety * h * (tol / err)^(1/5)
    float factor = safety * std::pow(tol / (err + 1e-30f), 0.2f);
    factor = std::clamp(factor, minFactor, maxFactor);
    float h_new = h * factor;

    printf("(a)err=%g tol=%g h=%g factor=%g h_new=%g x=%g %g %g dx=%g %g %g\n", err, tol, h, factor, h_new, x.x, x.y, x.z, x5.x-x.x, x5.y-x.y,x5.z-x.z);
  
    // if error too large, reject step and retry with smaller h:
    h = h_new;
    if (err < tol) 
      break;  
  }
  
  return x5;
}

Vec3 StreamlineComputer::EulerstepAdaptive(const Vec3 &x, float &h, float tol) {
  auto eval = [&](const Vec3 &p, Vec3 &outB, float &hsml){
    float buf[3];
    evalFieldAt( &p.x, buf, hsml);
    outB = Vec3{ buf[0], buf[1], buf[2] };
  };

  Vec3 k1, x5;
  float h_nearest;
  
  while(1){
    eval(x, k1, h_nearest);    
    double vel = sqrt(k1.x*k1.x + k1.y*k1.y + k1.z*k1.z);
    double h_new = h / fabs(h) * 0.1 * h_nearest / vel;

    h = h_new;    
    x5 = x + k1 * h;
    
    printf("(a)h=%g h_nearest=%g vel=%g x=%g %g %g dx=%g %g %g\n", h, h_nearest, vel, x.x, x.y, x.z, x5.x-x.x, x5.y-x.y,x5.z-x.z);
        
    if (1) 
      break;  
  }
  
  return x5;
}
