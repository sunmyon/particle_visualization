#ifdef USE_CONVEX_HULL
#include "main.h"
#include "camera.h"
#include "FindClumps/create_convex_hull.h"
#include "FindClumps/find_clumps.h"
#include "compute_2D_histogram.h"

#ifdef MACOS
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

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
struct ConvexHullRenderer::Impl {
    GLuint convexHullVAO = 0, convexHullVBO = 0;
    GLuint lineProgram = 0;

    std::unordered_map<int, Polyhedron_3> convexHullCache;
    std::unordered_map<int, GLuint> convexHullVAOCache;
    std::unordered_map<int, GLuint> convexHullVBOCache;
    std::unordered_map<int, size_t> convexHullVertexCountCache;
    std::unordered_map<int, bool> convexHullDirtyMap;

    TrackingVector<bool> convexHullDirty;

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


ConvexHullRenderer::ConvexHullRenderer() {
    impl = new Impl();
}

ConvexHullRenderer::~ConvexHullRenderer() {
    delete impl;
}

void ConvexHullRenderer::Init(GLuint lineProgram) {
  impl->lineProgram = lineProgram;

    glGenVertexArrays(1, &impl->convexHullVAO);
    glGenBuffers(1, &impl->convexHullVBO);
}


void ConvexHullRenderer::Render(const glm::mat4 &view, const glm::mat4 &projection, FindClump *clump, ParticleArray *P, histogram2D *hist) {
  if(!clump->checkClumpComputation())
    return;
  
  // シェーダーのuniform更新（view/projectionは毎フレーム更新）
  glUseProgram(impl->lineProgram);
  glUniformMatrix4fv(glGetUniformLocation(impl->lineProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(impl->lineProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

  int nclumps = clump->get_nclumps() ;

  if(clump->checkClearCache()){
    impl->convexHullCache.clear();
    impl->convexHullVBOCache.clear();
    impl->convexHullVAOCache.clear();
    impl->convexHullVertexCountCache.clear();
    impl->convexHullDirtyMap.clear();
    for (int i = 0; i < nclumps; i++) 
      impl->convexHullDirtyMap[i] = true;    // 初回は更新が必要

    clump->finishClearCache();

    // 変換先のコンテナ
    TrackingVector<std::shared_ptr<IConvexHull>> convexHullsVec;
    
    // unordered_map の各要素について、CGALConvexHull の shared_ptr を作成
    for (const auto &pair : impl->convexHullCache) {
      // pair.first はキー、pair.second は Polyhedron_3
      convexHullsVec.push_back(std::make_shared<CGALConvexHull>(pair.second));
    }

    hist->setConvexHulls(convexHullsVec);
    P->particlesDirty = true;  // グローバルなフラグをtrueに設定
  }
  
  // gClumpList は、検出された各クランプの情報が入っているとする
  for (int i = 0; i < nclumps; i++) {
    if (clump->flagShowHull(i)){
      // dirtyフラグがtrueなら再計算・VBO更新
      if (impl->convexHullDirtyMap[i]) {
	TrackingVector<ParticleData> pts = clump->get_particle_indices(i, P->particles);	
	Polyhedron_3 poly = impl->computeConvexHullForClump(pts);	
	TrackingVector<float> vertices = impl->ExtractLineVertices(poly);
	impl->convexHullVertexCountCache[i] = vertices.size() / 3;
	impl->convexHullCache[i] = poly;
	
	GLuint vbo, vao;
	if (impl->convexHullVBOCache.find(i) == impl->convexHullVBOCache.end()) {
	  glGenBuffers(1, &vbo);
	  impl->convexHullVBOCache[i] = vbo;
	} else {
	  vbo = impl->convexHullVBOCache[i];
	}

	// VAO の生成
	if (impl->convexHullVAOCache.find(i) == impl->convexHullVAOCache.end()) {
	  glGenVertexArrays(1, &vao);
	  impl->convexHullVAOCache[i] = vao;
	} else {
	  vao = impl->convexHullVAOCache[i];
	}
	// VAO バインドして VBO を更新、頂点属性を設定
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
		
	impl->convexHullDirtyMap[i] = false;
      }
      // 描画：キャッシュ済みのVAOをバインドしてドローコール
      GLuint vao = impl->convexHullVAOCache[i];
      glBindVertexArray(vao);
      size_t vertexCount = impl->convexHullVertexCountCache[i];
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
      glBindVertexArray(0);
    }
  }
}
#endif
