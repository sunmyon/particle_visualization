#ifdef USE_CONVEX_HULL
#include "main.h"
#include "camera.h"
#include "FindClumps/create_convex_hull.h"
#include "FindClumps/find_clumps.h"
#include "compute_2D_histogram.h"

#include <CGAL/convex_hull_3.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Side_of_triangle_mesh.h>

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::Point_3 Point_3;
typedef CGAL::Polyhedron_3<Kernel> Polyhedron_3;

class CGALConvexHull : public IConvexHull {
public:
    // コンストラクタでPolyhedron_3オブジェクトを受け取る
    CGALConvexHull(const Polyhedron_3& polyhedron)
      : polyhedron_(polyhedron), tester_(polyhedron_) {}

    bool isInside(const std::array<double, 3>& ptArr) const override {
        Point_3 pt(ptArr[0], ptArr[1], ptArr[2]);
        return (tester_(pt) != CGAL::ON_UNBOUNDED_SIDE);
    }

private:
  Polyhedron_3 polyhedron_;
  CGAL::Side_of_triangle_mesh<Polyhedron_3, Kernel> tester_;
};

// PImplイディオムで内部実装を分離
struct ConvexHullGenerator::Impl {
    Polyhedron_3 computeConvexHullForClump(const TrackingVector<ParticleData>& pts) {
        TrackingVector<Point_3> points;
        for (const ParticleData& p : pts) {
            points.push_back(Point_3(p.pos[0], p.pos[1], p.pos[2]));
        }
        Polyhedron_3 poly;
        CGAL::convex_hull_3(points.begin(), points.end(), poly);
        return poly;
    }

    TrackingVector<float> ExtractLineVertices(const Polyhedron_3& poly) {
        TrackingVector<float> vertices;
        for (auto facet = poly.facets_begin(); facet != poly.facets_end(); ++facet) {
            TrackingVector<Point_3> facetPoints;
            auto h = facet->facet_begin();
            auto start = h;
            do {
                facetPoints.push_back(h->vertex()->point());
                ++h;
            } while (h != start);
            if (facetPoints.size() >= 2) {
                for (size_t j = 0; j < facetPoints.size(); j++) {
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
};

ConvexHullGenerator::ConvexHullGenerator() {
    impl = new Impl();
}

ConvexHullGenerator::~ConvexHullGenerator() {
    delete impl;
}

TrackingVector<float>
ConvexHullGenerator::buildLineVertices(const TrackingVector<ParticleData>& pts) {
  auto poly = impl->computeConvexHullForClump(pts);
  return impl->ExtractLineVertices(poly);
}

#endif
