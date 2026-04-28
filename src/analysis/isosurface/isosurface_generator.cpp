// test_descriptor_isosurface.cpp
#include "analysis/isosurface/isosurface_generator.h"
#include "analysis/isosurface/mesh_data.h"
#include "OctTree/ParticleOctree.h"

#include <vtkHyperTreeGridNonOrientedCursor.h>  
#include <vtkHyperTreeGridContour.h> 
#include <vtkCellData.h>

#include <vtkSmartPointer.h>
#include <vtkHyperTreeGridSource.h>
#include <vtkHyperTreeGrid.h>
#include <vtkHyperTreeGridToUnstructuredGrid.h>
#include <vtkContourFilter.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPolyData.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkDataObject.h>
#include <iostream>
#include <string>

#include <vtkCleanPolyData.h>
#include <vtkPolyDataNormals.h>      // ← これを追加
#include <vtkSmoothPolyDataFilter.h>              // 基本的なラプラシアン平滑
#include <vtkWindowedSincPolyDataFilter.h> 
#include <vtkCellDataToPointData.h>

#include <vtkFillHolesFilter.h>
#include <vtkMergePoints.h>
#include <vtkDecimatePro.h>

namespace {
  // レベルごとの Descriptor を貯める配列
  void buildDescriptorLevel(const ParticleOctree::Node* node,
                            int depth,
                            int maxDepth,
                            std::vector<std::string>& levels)
  {
    if (depth >= maxDepth) return;

    // 初めてこの depth に来たら、空文字列を作る
    if ((int)levels.size() <= depth) {
      levels.emplace_back();
    }

    // このノードが subdivide される（非 leaf）なら 'R', そうでなければ '.'
    levels[depth].push_back(node->isLeaf ? '.' : 'R');

    // subdivide されていれば子を再帰
    if (!node->isLeaf) {
      for (int ci = 0; ci < 8; ++ci) {
        buildDescriptorLevel(node->children[ci].get(),
                             depth + 1, maxDepth, levels);
      }
    }
  }

  // levels に詰まった文字列を "|" で連結し、Descriptor 完成
  std::string makeDescriptor(const ParticleOctree& octree, int maxDepth)
  {
    std::vector<std::string> levels;
    buildDescriptorLevel(&octree.root(), /*depth=*/0, maxDepth, levels);

    // join with '|'
    std::string desc;
    for (size_t i = 0; i < levels.size(); ++i) {
      if (i) desc.push_back('|');
      desc += levels[i];
    }
    // もし maxDepth が levels.size() より深ければ、
    // 末尾に追加の "|...." を入れてもよいですが、
    // VTK はレベルごとに文字数チェックを行うので
    // octree 構造と levels.size()==maxDepth が合うようにしてください。
    return desc;
  }
}

Mesh IsoSurfaceGenerator::generateVTK(IsoSurfaceParams params)
{
  ParticleOctree octree(std::move(params.particles),
			params.worldBox,
			params.minParticles,
			params.maxDepth,
			params.isoLevel,
			true
			);

  octree.balanceTree(true);
  octree.computeValueRangeRoot();
  
  octree.dumpNodeRoot();
  
  std::string descriptor = makeDescriptor(octree, params.maxDepth);
  
  // 1) HTG ソースの設定（Descriptor 方式）
  auto source = vtkSmartPointer<vtkHyperTreeGridSource>::New();
  {
    double sx = params.worldBox.max.x - params.worldBox.min.x;
    double sy = params.worldBox.max.y - params.worldBox.min.y;
    double sz = params.worldBox.max.z - params.worldBox.min.z;
    source->SetGridScale(sx, sy, sz);
  }
  source->SetDimensions(2, 2, 2);
  source->SetBranchFactor(2);
  source->SetMaxDepth(params.maxDepth);

  source->SetUseDescriptor(true);
  source->SetUseMask(false);
  source->SetDescriptor(descriptor.c_str());
  
  // 2) HTG を一括構築
  source->Update();
  vtkHyperTreeGrid* htg = source->GetHyperTreeGridOutput();
  std::cout << "[After HTG source]\n"
            << "  BranchFactor  = " << htg->GetBranchFactor() << "\n"
            << "  NumberOfCells = " << htg->GetNumberOfCells() << "\n\n";

  // 3) UnstructuredGrid に変換
  auto toUG = vtkSmartPointer<vtkHyperTreeGridToUnstructuredGrid>::New();
  toUG->SetInputConnection(source->GetOutputPort());
  toUG->Update();
  vtkUnstructuredGrid* ug =
    vtkUnstructuredGrid::SafeDownCast(toUG->GetOutputDataObject(0));
  std::cout << "[After conversion to UnstructuredGrid]\n"
            << "  Cells = " << ug->GetNumberOfCells() << "\n\n";

  // 4) 点ごとに X 座標をスカラーとしてセット
  vtkIdType numPts = ug->GetNumberOfPoints();
  auto scalars = vtkSmartPointer<vtkDoubleArray>::New();
  scalars->SetName("density");
  scalars->SetNumberOfComponents(1);
  scalars->SetNumberOfTuples(numPts);

  for (vtkIdType pid = 0; pid < numPts; ++pid) {
    double p[3];
    ug->GetPoint(pid, p);

    p[0] += params.worldBox.min.x;
    p[1] += params.worldBox.min.y;
    p[2] += params.worldBox.min.z;
      
    glm::vec3 pArr{p[0], p[1], p[2]};
    
    const auto* leaf = octree.findLeafContainingRoot(pArr);

    const auto& B = leaf->box;
    double bestVal = leaf->edgeValues[0];
    double minDist2 = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 8; ++i){
      // i ビットマスクで corner の座標を選択
      glm::vec3 corner{
	(i & 1) ? B.max.x : B.min.x,
	(i & 2) ? B.max.y : B.min.y,
	(i & 4) ? B.max.z : B.min.z
      };
      double dx = pArr.x - corner.x;
      double dy = pArr.y - corner.y;
      double dz = pArr.z - corner.z;
      double d2 = dx*dx + dy*dy + dz*dz;
      if (d2 < minDist2){
	minDist2 = d2;
	bestVal = leaf->edgeValues[i];
      }
    }

    scalars->SetValue(pid, bestVal);
    printf("p=%g %g %g val=%g\n", p[0], p[1], p[2], bestVal);
  }
  ug->GetPointData()->SetScalars(scalars);

  // (A) CellData を PointData に変換
  auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
  c2p->SetInputData(ug);
  c2p->PassCellDataOn();    // cellData を pointData にコピー
  c2p->Update();

  // (B) 変換後を ContourFilter に入力
  auto contour = vtkSmartPointer<vtkContourFilter>::New();
  contour->SetInputConnection(c2p->GetOutputPort());
  contour->SetInputArrayToProcess(
				  0, 0, 0,
				  vtkDataObject::FIELD_ASSOCIATION_POINTS,  // ← POINTS
				  "density"                                // あなたのスカラー名
				  );
  contour->SetValue(0, params.isoLevel);
  contour->Update();
  /*  
  // (C) FillHoles
  float maxHoleDiameter = 0.1;
  auto fill = vtkSmartPointer<vtkFillHolesFilter>::New();
  fill->SetInputConnection(contour->GetOutputPort());
  fill->SetHoleSize(maxHoleDiameter);

  // (D) Decimate
  auto deci = vtkSmartPointer<vtkDecimatePro>::New();
  deci->SetInputConnection(fill->GetOutputPort());
  deci->SetTargetReduction(0.5);
  deci->PreserveTopologyOn();
  deci->BoundaryVertexDeletionOff();

  // (E) Normals
  auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
  normals->SetInputConnection(deci->GetOutputPort());
  normals->SplittingOff();
  normals->ConsistencyOn();

  // (F) Laplacian 平滑化
  auto laplacian = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
  laplacian->SetInputConnection(normals->GetOutputPort());
  laplacian->SetNumberOfIterations(300);
  laplacian->SetRelaxationFactor(0.3);
  laplacian->BoundarySmoothingOn();
  laplacian->FeatureEdgeSmoothingOff();  // (もしくは On→Off を消す)
  */
  
  // --- 実行は最後に一回だけで OK ---
  contour->Update();
  vtkPolyData* smoothPoly = contour->GetOutput();
 
  //vtkPolyData* poly =  vtkPolyData::SafeDownCast(contour->GetOutputDataObject(0));
  std::cout << "[After ContourFilter]\n"
	    << "  Polys = " << smoothPoly->GetNumberOfPolys() << "\n";

  // 6) Mesh 構造体に変換
  Mesh mesh;
  // 頂点
  {
    auto pts = smoothPoly->GetPoints();
    vtkIdType N = pts->GetNumberOfPoints();
    mesh.vertices.reserve(N*3);
    for (vtkIdType i = 0; i < N; ++i) {
      double xyz[3];
      pts->GetPoint(i, xyz);

      xyz[0] += params.worldBox.min.x;
      xyz[1] += params.worldBox.min.y;
      xyz[2] += params.worldBox.min.z;

      mesh.vertices.push_back((float)xyz[0]);
      mesh.vertices.push_back((float)xyz[1]);
      mesh.vertices.push_back((float)xyz[2]);
    }
  }
  // インデックス
  {
    auto cells = smoothPoly->GetPolys();
    cells->InitTraversal();
    vtkIdType npts;
    const vtkIdType *ids;
    while (cells->GetNextCell(npts, ids)) {
      // ids[0] にセル内ポイント数、ids[1..npts] がインデックス
      for (int k = 0; k < npts; k++)
	mesh.indices.push_back((unsigned)ids[k]);
    }
  }
  
  return mesh;
}
