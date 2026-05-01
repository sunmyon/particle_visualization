#ifdef USE_CONVEX_HULL
#include "analysis/convex_hull/convex_hull_generator.h"

#include <CGAL/convex_hull_3.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Side_of_triangle_mesh.h>

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::Point_3 Point_3;
typedef CGAL::Polyhedron_3<Kernel> Polyhedron_3;

class CGALConvexHull : public IConvexHull {
public:
  explicit CGALConvexHull(const Polyhedron_3& polyhedron)
    : polyhedron_(polyhedron), tester_(polyhedron_) {}

  bool isInside(const std::array<double, 3>& ptArr) const override {
    Point_3 pt(ptArr[0], ptArr[1], ptArr[2]);
    return (tester_(pt) != CGAL::ON_UNBOUNDED_SIDE);
  }

  std::vector<float> extractLineVertices() const {
    std::vector<float> vertices;

    for (auto facet = polyhedron_.facets_begin();
         facet != polyhedron_.facets_end(); ++facet) {
      std::vector<Point_3> facetPoints;
      auto h = facet->facet_begin();
      auto start = h;
      do {
        facetPoints.push_back(h->vertex()->point());
        ++h;
      } while (h != start);

      if (facetPoints.size() >= 2) {
        for (size_t j = 0; j < facetPoints.size(); ++j) {
          const Point_3& p1 = facetPoints[j];
          const Point_3& p2 = facetPoints[(j + 1) % facetPoints.size()];
          vertices.push_back(static_cast<float>(p1.x()));
          vertices.push_back(static_cast<float>(p1.y()));
          vertices.push_back(static_cast<float>(p1.z()));
          vertices.push_back(static_cast<float>(p2.x()));
          vertices.push_back(static_cast<float>(p2.y()));
          vertices.push_back(static_cast<float>(p2.z()));
        }
      }
    }

    return vertices;
  }

private:
  Polyhedron_3 polyhedron_;
  CGAL::Side_of_triangle_mesh<Polyhedron_3, Kernel> tester_;
};

struct ConvexHullGenerator::Impl {
  Polyhedron_3 computePolyhedron(const std::vector<glm::vec3>& points) {
    std::vector<Point_3> cgalPoints;
    cgalPoints.reserve(points.size());

    for (const auto& p : points) {
      cgalPoints.push_back(Point_3(p.x, p.y, p.z));
    }

    Polyhedron_3 poly;
    CGAL::convex_hull_3(cgalPoints.begin(), cgalPoints.end(), poly);
    return poly;
  }
};

ConvexHullGenerator::ConvexHullGenerator() {
  impl = new Impl();
}

ConvexHullGenerator::~ConvexHullGenerator() {
  delete impl;
}

std::shared_ptr<IConvexHull>
ConvexHullGenerator::buildHull(const std::vector<glm::vec3>& points)
{
  auto poly = impl->computePolyhedron(points);
  return std::make_shared<CGALConvexHull>(poly);
}

std::vector<float>
ConvexHullGenerator::buildLineVertices(const std::vector<glm::vec3>& points)
{
  auto poly = impl->computePolyhedron(points);
  CGALConvexHull hull(poly);
  return hull.extractLineVertices();
}

#endif
