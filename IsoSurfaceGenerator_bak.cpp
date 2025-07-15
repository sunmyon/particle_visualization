#include "IsoSurfaceGenerator.h"
#include "ParticleOctree.h"
#include "mesh_data.h"

#include <vtkSmartPointer.h>
#include <vtkHyperTreeGridSource.h>
#include <vtkHyperTreeGrid.h>
#include <vtkHyperTreeGridNonOrientedCursor.h>
#include <vtkDoubleArray.h>
#include <vtkCellData.h>
#include <vtkHyperTreeGridContour.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>

#include <vtkHyperTreeGridNonOrientedGeometryCursor.h>  // ← 追加
#include <vtkBitArray.h>                                // ← 追加

// SPH→セルスカラー転写＋輪郭抽出のファサード
Mesh IsoSurfaceGenerator::generateVTK( IsoSurfaceParams params ) {
  // 1) Octree を組んでおく（頂点密度や edgeValues は計算済み）
  ParticleOctree octree(std::move(params.particles),
			params.worldBox,
			params.isoLevel,
			params.minParticles,
			params.maxDepth
			);

  // 2) ソースフィルターで HTG を生成
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

  // グリッドのスケールを worldBox の大きさに合わせる 
  source->Update();

  vtkHyperTreeGrid* htg = source->GetHyperTreeGridOutput(); 
  std::cout
    << " HTG: branch="    << htg->GetBranchFactor()
    << "  cells="         << htg->GetNumberOfCells()
    << "  nonEmptyTrees=" << htg->GetNumberOfNonEmptyTrees()
    << std::endl;


  //htg->Initialize();
  // 2) recursive lambda to subdivide exactly where your octree did
  // 1) grab a fresh cursor that can modify the tree
  auto buildCursor = vtkSmartPointer<vtkHyperTreeGridNonOrientedCursor>::New();
  htg->InitializeNonOrientedCursor(buildCursor, 0, true);
    
  std::function<void(const ParticleOctree::Node*, vtkHyperTreeGridNonOrientedCursor*)>
    subdivideVTK = [&](const ParticleOctree::Node* src, vtkHyperTreeGridNonOrientedCursor* cur){
      if (!src->isLeaf) {
	cur->SubdivideLeaf();
	for (unsigned ci = 0; ci < 8; ++ci) {
	  cur->ToChild(ci);
	  subdivideVTK(src->children[ci].get(), cur);
	  cur->ToParent();
	}
      }else{
	std::cout << "  leaf global index = " << cur->GetGlobalNodeIndex() << std::endl;      
      }	
    };

  // 3) reflect your entire tree
  subdivideVTK(&octree.root(), buildCursor);

  //htg->InitializeLocalIndexNode(); //koreha??
  htg->InitializeLocalIndexNode();
  vtkIdType nTup = htg->GetGlobalNodeIndexMax() + 1;   
  
  vtkIdType nCells = htg->GetNumberOfCells();
  auto leaves = octree.getAllLeafNodes();  
  printf("nCells=%d nTup=%d nleaves=%d\n", nCells, nTup, leaves.size());
  
  auto cellScalars = vtkSmartPointer<vtkDoubleArray>::New();
  cellScalars->SetName("density");
  cellScalars->SetNumberOfTuples(nCells);
  htg->GetCellData()->SetScalars(cellScalars);

  auto valueCursor = vtkSmartPointer<vtkHyperTreeGridNonOrientedCursor>::New();
  htg->InitializeNonOrientedCursor(valueCursor, 0, /*create=*/false);
  
  std::function<void(const ParticleOctree::Node*, vtkHyperTreeGridNonOrientedCursor*, vtkIdType&)>
    writeScalars = [&](const ParticleOctree::Node* src, vtkHyperTreeGridNonOrientedCursor* cur, vtkIdType& counter){
      if (src->isLeaf) {
	//vtkIdType cid = cur->GetGlobalNodeIndex();
	double avg = 0;
	for (float v : src->edgeValues) avg += v;
	avg /= src->edgeValues.size();

	printf("cid=%d avg=%g\n", counter, avg);
	cellScalars->SetValue(counter, avg);
	++counter;
      }
      else {
	cur->SubdivideLeaf();            // if you passed create=false above, this re‐uses existing subdivision
	for (unsigned ci = 0; ci < 8; ++ci) {
	  cur->ToChild(ci);
	  writeScalars(src->children[ci].get(), cur, counter);
	  cur->ToParent();
	}
      }
    };

  vtkIdType counter = 0;
  writeScalars(&octree.root(), valueCursor, counter);

  auto geomCursor = vtkSmartPointer<vtkHyperTreeGridNonOrientedGeometryCursor>::New();
  htg->InitializeNonOrientedGeometryCursor(geomCursor, /*treeIndex=*/0, /*create=*/false);

// mask を 0 で初期化
auto mask = vtkSmartPointer<vtkBitArray>::New();
mask->SetName("Mask");
mask->SetNumberOfTuples(nCells);
for (vtkIdType i = 0; i < nCells; ++i) {
  mask->SetValue(i, 0);  // 0 = non-blanked
}

// HTG にマスクをセット
htg->SetMask(mask);

  
  htg->ComputeBounds();
  
  //htg->InitializeLocalIndexNode();
  htg->Modified();
  //htg->ComputeBounds();  
  
  // 4) カーソルで葉ノードをたどり、cellScalars に密度を転写
  /*auto cursor = vtkSmartPointer<vtkHyperTreeGridNonOrientedCursor>(htg->NewNonOrientedCursor(0, true)  // treeIndex=0, create=true
    );
    std::function<void(const ParticleOctree::Node*)> recurse =
    [&](auto const* node)
    {
    if (node->isLeaf) {
    vtkIdType cid = cursor->GetGlobalNodeIndex();
    double sum = 0;
    for (float v : node->edgeValues) sum += v;
    cellScalars->SetValue(cid, sum / node->edgeValues.size());
    }
    else {
    cursor->SubdivideLeaf();
    for (unsigned i = 0; i < 8; ++i) {
    cursor->ToChild(i);
    recurse(node->children[i].get());
    cursor->ToParent();
    }
    }
    };
    recurse(&octree.root());
  */
  
  // 5) 輪郭抽出
  auto contour = vtkSmartPointer<vtkHyperTreeGridContour>::New();
  contour->SetInputData(htg);
  contour->SetValue(0, params.isoLevel);
  contour->Update();

  // （法線がほしい場合は PolyDataNormals をかませても OK）
  auto poly = vtkPolyData::SafeDownCast(contour->GetOutputDataObject(0));

  // 6) Mesh 構造体に変換
  Mesh mesh;
  // 頂点
  {
    auto pts = poly->GetPoints();
    vtkIdType N = pts->GetNumberOfPoints();
    mesh.vertices.reserve(N*3);
    for (vtkIdType i = 0; i < N; ++i) {
      double xyz[3];
      pts->GetPoint(i, xyz);
      mesh.vertices.push_back((float)xyz[0]);
      mesh.vertices.push_back((float)xyz[1]);
      mesh.vertices.push_back((float)xyz[2]);
    }
  }
  // インデックス
  {
    auto cells = poly->GetPolys();
    cells->InitTraversal();
    vtkIdType npts;
    const vtkIdType *ids;
    while (cells->GetNextCell(npts, ids)) {
      // ids[0] にセル内ポイント数、ids[1..npts] がインデックス
      for (int k = 1; k <= npts; ++k)
        mesh.indices.push_back((unsigned)ids[k]);
    }
  }

  return mesh;
}
