#ifdef USE_CONVEX_HULL
#include "analysis/convex_hull/convex_hull_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

Vec3d operator+(const Vec3d& a, const Vec3d& b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3d operator-(const Vec3d& a, const Vec3d& b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3d operator*(const Vec3d& a, double s)
{
  return {a.x * s, a.y * s, a.z * s};
}

double dot(const Vec3d& a, const Vec3d& b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3d cross(const Vec3d& a, const Vec3d& b)
{
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  };
}

double length2(const Vec3d& a)
{
  return dot(a, a);
}

double length(const Vec3d& a)
{
  return std::sqrt(length2(a));
}

struct Face {
  int a = 0;
  int b = 0;
  int c = 0;
  Vec3d normal;
  double offset = 0.0;
  bool removed = false;
};

struct Edge {
  int a = 0;
  int b = 0;
};

class PlaneConvexHull : public IConvexHull {
public:
  PlaneConvexHull(std::vector<Vec3d> points,
                  std::vector<Face> faces,
                  double eps)
    : points_(std::move(points)), faces_(std::move(faces)), eps_(eps)
  {
  }

  bool isInside(const std::array<double, 3>& ptArr) const override
  {
    const Vec3d p{ptArr[0], ptArr[1], ptArr[2]};
    for (const Face& f : faces_) {
      if (f.removed) continue;
      if (dot(f.normal, p) + f.offset > eps_) {
        return false;
      }
    }
    return true;
  }

  std::vector<float> extractLineVertices() const
  {
    std::vector<float> vertices;
    vertices.reserve(faces_.size() * 18);
    for (const Face& f : faces_) {
      if (f.removed) continue;
      appendEdge(vertices, f.a, f.b);
      appendEdge(vertices, f.b, f.c);
      appendEdge(vertices, f.c, f.a);
    }
    return vertices;
  }

private:
  void appendEdge(std::vector<float>& vertices, int ia, int ib) const
  {
    const Vec3d& a = points_[static_cast<size_t>(ia)];
    const Vec3d& b = points_[static_cast<size_t>(ib)];
    vertices.push_back(static_cast<float>(a.x));
    vertices.push_back(static_cast<float>(a.y));
    vertices.push_back(static_cast<float>(a.z));
    vertices.push_back(static_cast<float>(b.x));
    vertices.push_back(static_cast<float>(b.y));
    vertices.push_back(static_cast<float>(b.z));
  }

  std::vector<Vec3d> points_;
  std::vector<Face> faces_;
  double eps_ = 0.0;
};

std::vector<Vec3d> deduplicate(const std::vector<glm::vec3>& input)
{
  std::vector<Vec3d> points;
  points.reserve(input.size());
  for (const glm::vec3& p : input) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
      continue;
    }
    points.push_back({p.x, p.y, p.z});
  }

  std::sort(points.begin(), points.end(), [](const Vec3d& a, const Vec3d& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
  });

  points.erase(std::unique(points.begin(),
                           points.end(),
                           [](const Vec3d& a, const Vec3d& b) {
                             return a.x == b.x && a.y == b.y && a.z == b.z;
                           }),
               points.end());
  return points;
}

Face makeFace(const std::vector<Vec3d>& points,
              int a,
              int b,
              int c,
              const Vec3d& interior)
{
  Face f;
  f.a = a;
  f.b = b;
  f.c = c;

  auto computePlane = [&]() {
    const Vec3d ab = points[static_cast<size_t>(f.b)] -
                     points[static_cast<size_t>(f.a)];
    const Vec3d ac = points[static_cast<size_t>(f.c)] -
                     points[static_cast<size_t>(f.a)];
    f.normal = cross(ab, ac);
    const double invLen = 1.0 / std::max(length(f.normal), 1.0e-300);
    f.normal = f.normal * invLen;
    f.offset = -dot(f.normal, points[static_cast<size_t>(f.a)]);
  };

  computePlane();
  if (dot(f.normal, interior) + f.offset > 0.0) {
    std::swap(f.b, f.c);
    computePlane();
  }
  return f;
}

std::shared_ptr<PlaneConvexHull>
buildAabbHull(const std::vector<Vec3d>& input)
{
  if (input.empty()) {
    return nullptr;
  }

  Vec3d lo = input.front();
  Vec3d hi = input.front();
  for (const Vec3d& p : input) {
    lo.x = std::min(lo.x, p.x);
    lo.y = std::min(lo.y, p.y);
    lo.z = std::min(lo.z, p.z);
    hi.x = std::max(hi.x, p.x);
    hi.y = std::max(hi.y, p.y);
    hi.z = std::max(hi.z, p.z);
  }

  const Vec3d span = hi - lo;
  const double pad = std::max({length(span) * 1.0e-9, 1.0e-9});
  lo = lo + Vec3d{-pad, -pad, -pad};
  hi = hi + Vec3d{ pad,  pad,  pad};

  std::vector<Vec3d> points = {
    {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
    {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
    {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
    {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}
  };
  const Vec3d interior = (lo + hi) * 0.5;
  std::vector<Face> faces;
  faces.reserve(12);
  const int tris[][3] = {
    {0, 2, 1}, {0, 3, 2},
    {4, 5, 6}, {4, 6, 7},
    {0, 1, 5}, {0, 5, 4},
    {1, 2, 6}, {1, 6, 5},
    {2, 3, 7}, {2, 7, 6},
    {3, 0, 4}, {3, 4, 7}
  };
  for (const auto& tri : tris) {
    faces.push_back(makeFace(points, tri[0], tri[1], tri[2], interior));
  }
  return std::make_shared<PlaneConvexHull>(std::move(points),
                                           std::move(faces),
                                           pad * 10.0);
}

bool findInitialTetrahedron(const std::vector<Vec3d>& points,
                            int& i0,
                            int& i1,
                            int& i2,
                            int& i3,
                            double eps)
{
  if (points.size() < 4) return false;

  std::array<int, 6> extreme = {0, 0, 0, 0, 0, 0};
  for (size_t i = 1; i < points.size(); ++i) {
    const int ii = static_cast<int>(i);
    if (points[i].x < points[static_cast<size_t>(extreme[0])].x) extreme[0] = ii;
    if (points[i].x > points[static_cast<size_t>(extreme[1])].x) extreme[1] = ii;
    if (points[i].y < points[static_cast<size_t>(extreme[2])].y) extreme[2] = ii;
    if (points[i].y > points[static_cast<size_t>(extreme[3])].y) extreme[3] = ii;
    if (points[i].z < points[static_cast<size_t>(extreme[4])].z) extreme[4] = ii;
    if (points[i].z > points[static_cast<size_t>(extreme[5])].z) extreme[5] = ii;
  }

  i0 = extreme[0];
  i1 = extreme[1];
  double maxD2 = -1.0;
  for (int a : extreme) {
    for (int b : extreme) {
      if (a == b) continue;
      const double d2 = length2(points[static_cast<size_t>(b)] -
                                points[static_cast<size_t>(a)]);
      if (d2 > maxD2) {
        maxD2 = d2;
        i0 = a;
        i1 = b;
      }
    }
  }
  if (maxD2 <= eps * eps) return false;

  const Vec3d line = points[static_cast<size_t>(i1)] -
                    points[static_cast<size_t>(i0)];
  i2 = -1;
  double maxLineDist2 = -1.0;
  for (size_t i = 0; i < points.size(); ++i) {
    if (static_cast<int>(i) == i0 || static_cast<int>(i) == i1) continue;
    const Vec3d v = points[i] - points[static_cast<size_t>(i0)];
    const double d2 = length2(cross(line, v)) / std::max(length2(line), eps * eps);
    if (d2 > maxLineDist2) {
      maxLineDist2 = d2;
      i2 = static_cast<int>(i);
    }
  }
  if (i2 < 0 || maxLineDist2 <= eps * eps) return false;

  const Vec3d n = cross(points[static_cast<size_t>(i1)] -
                          points[static_cast<size_t>(i0)],
                        points[static_cast<size_t>(i2)] -
                          points[static_cast<size_t>(i0)]);
  const double nLen = std::max(length(n), eps);
  i3 = -1;
  double maxPlaneDist = -1.0;
  for (size_t i = 0; i < points.size(); ++i) {
    if (static_cast<int>(i) == i0 ||
        static_cast<int>(i) == i1 ||
        static_cast<int>(i) == i2) {
      continue;
    }
    const double d =
      std::abs(dot(n, points[i] - points[static_cast<size_t>(i0)])) / nLen;
    if (d > maxPlaneDist) {
      maxPlaneDist = d;
      i3 = static_cast<int>(i);
    }
  }
  return i3 >= 0 && maxPlaneDist > eps;
}

bool edgeOpposite(const Edge& a, const Edge& b)
{
  return a.a == b.b && a.b == b.a;
}

void addBoundaryEdge(std::vector<Edge>& boundary, Edge e)
{
  for (auto it = boundary.begin(); it != boundary.end(); ++it) {
    if (edgeOpposite(*it, e)) {
      boundary.erase(it);
      return;
    }
  }
  boundary.push_back(e);
}

std::shared_ptr<PlaneConvexHull>
buildIncrementalHull(std::vector<Vec3d> points)
{
  if (points.size() < 4) {
    return buildAabbHull(points);
  }

  Vec3d lo = points.front();
  Vec3d hi = points.front();
  for (const Vec3d& p : points) {
    lo.x = std::min(lo.x, p.x);
    lo.y = std::min(lo.y, p.y);
    lo.z = std::min(lo.z, p.z);
    hi.x = std::max(hi.x, p.x);
    hi.y = std::max(hi.y, p.y);
    hi.z = std::max(hi.z, p.z);
  }
  const double eps = std::max(length(hi - lo) * 1.0e-10, 1.0e-10);

  int i0 = -1;
  int i1 = -1;
  int i2 = -1;
  int i3 = -1;
  if (!findInitialTetrahedron(points, i0, i1, i2, i3, eps)) {
    return buildAabbHull(points);
  }

  const Vec3d interior = (points[static_cast<size_t>(i0)] +
                          points[static_cast<size_t>(i1)] +
                          points[static_cast<size_t>(i2)] +
                          points[static_cast<size_t>(i3)]) * 0.25;

  std::vector<Face> faces;
  faces.reserve(points.size() * 2);
  faces.push_back(makeFace(points, i0, i1, i2, interior));
  faces.push_back(makeFace(points, i0, i3, i1, interior));
  faces.push_back(makeFace(points, i1, i3, i2, interior));
  faces.push_back(makeFace(points, i2, i3, i0, interior));

  for (size_t ip = 0; ip < points.size(); ++ip) {
    const int pIndex = static_cast<int>(ip);
    if (pIndex == i0 || pIndex == i1 || pIndex == i2 || pIndex == i3) {
      continue;
    }

    std::vector<size_t> visibleFaces;
    visibleFaces.reserve(32);
    for (size_t f = 0; f < faces.size(); ++f) {
      if (faces[f].removed) continue;
      if (dot(faces[f].normal, points[ip]) + faces[f].offset > eps) {
        visibleFaces.push_back(f);
      }
    }
    if (visibleFaces.empty()) {
      continue;
    }

    std::vector<Edge> boundary;
    boundary.reserve(visibleFaces.size() * 3);
    for (size_t faceIndex : visibleFaces) {
      Face& f = faces[faceIndex];
      addBoundaryEdge(boundary, {f.a, f.b});
      addBoundaryEdge(boundary, {f.b, f.c});
      addBoundaryEdge(boundary, {f.c, f.a});
      f.removed = true;
    }

    for (const Edge& e : boundary) {
      Face nf = makeFace(points, e.a, e.b, pIndex, interior);
      if (length2(nf.normal) > 0.0) {
        faces.push_back(nf);
      }
    }
  }

  faces.erase(std::remove_if(faces.begin(),
                             faces.end(),
                             [](const Face& f) { return f.removed; }),
              faces.end());
  if (faces.empty()) {
    return buildAabbHull(points);
  }

  return std::make_shared<PlaneConvexHull>(std::move(points),
                                           std::move(faces),
                                           eps * 16.0);
}

} // namespace

struct ConvexHullGenerator::Impl {
};

ConvexHullGenerator::ConvexHullGenerator()
{
  impl = new Impl();
}

ConvexHullGenerator::~ConvexHullGenerator()
{
  delete impl;
}

std::shared_ptr<IConvexHull>
ConvexHullGenerator::buildHull(const std::vector<glm::vec3>& points)
{
  return buildIncrementalHull(deduplicate(points));
}

std::vector<float>
ConvexHullGenerator::buildLineVertices(const std::vector<glm::vec3>& points)
{
  auto hull = std::dynamic_pointer_cast<PlaneConvexHull>(
    buildIncrementalHull(deduplicate(points)));
  if (!hull) {
    return {};
  }
  return hull->extractLineVertices();
}

#endif
