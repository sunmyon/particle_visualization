#include "analysis/isosurface/isosurface_generator.h"
#include "analysis/isosurface/mesh_data.h"
#include "data/spatial/particle_octree.h"

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
#include <vtkPolyDataNormals.h>
#include <vtkSmoothPolyDataFilter.h>              // Basic Laplacian smoothing.
#include <vtkWindowedSincPolyDataFilter.h> 
#include <vtkCellDataToPointData.h>
#include <vtkTriangleFilter.h>

#include <vtkFillHolesFilter.h>
#include <vtkMergePoints.h>
#include <vtkDecimatePro.h>

namespace {
  // Store the descriptor string for each level.
  void buildDescriptorLevel(const ParticleOctree::Node* node,
                            int depth,
                            int maxDepth,
                            std::vector<std::string>& levels)
  {
    if (depth >= maxDepth) return;

    // Create an empty string the first time this depth is encountered.
    if ((int)levels.size() <= depth) {
      levels.emplace_back();
    }

    // Use 'R' for subdivided non-leaf nodes and '.' otherwise.
    levels[depth].push_back(node->isLeaf ? '.' : 'R');

    // Recurse into children when subdivided.
    if (!node->isLeaf) {
      for (int ci = 0; ci < 8; ++ci) {
        buildDescriptorLevel(node->children[ci].get(),
                             depth + 1, maxDepth, levels);
      }
    }
  }

  // Join the level strings with "|" to complete the descriptor.
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
    // If maxDepth is deeper than levels.size(), appending extra "|...." segments
    // is possible. VTK validates string lengths per level, so keep the octree
    // structure consistent with levels.size() == maxDepth.
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
  IsoSurfaceTreeField field = BuildIsoSurfaceTreeField(octree);
  
  if (params.verbose) {
    octree.dumpNodeRoot();
  }
  
  std::string descriptor = makeDescriptor(octree, params.maxDepth);
  
  // 1) Configure the HTG source using descriptor mode.
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
  
  // 2) Build the HTG in one pass.
  source->Update();
  vtkHyperTreeGrid* htg = source->GetHyperTreeGridOutput();
  if (params.verbose) {
    std::cout << "[After HTG source]\n"
              << "  BranchFactor  = " << htg->GetBranchFactor() << "\n"
              << "  NumberOfCells = " << htg->GetNumberOfCells() << "\n\n";
  }

  // 3) Convert to UnstructuredGrid.
  auto toUG = vtkSmartPointer<vtkHyperTreeGridToUnstructuredGrid>::New();
  toUG->SetInputConnection(source->GetOutputPort());
  toUG->Update();
  vtkUnstructuredGrid* ug =
    vtkUnstructuredGrid::SafeDownCast(toUG->GetOutputDataObject(0));
  if (params.verbose) {
    std::cout << "[After conversion to UnstructuredGrid]\n"
              << "  Cells = " << ug->GetNumberOfCells() << "\n\n";
  }

  // 4) Set the X coordinate as the scalar value for each point.
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
    const float bestVal = field.nearestCornerValue(leaf, pArr);

    scalars->SetValue(pid, bestVal);
    if (params.verbose) {
      std::cout << "p=" << p[0] << " " << p[1] << " " << p[2]
                << " val=" << bestVal << "\n";
    }
  }
  ug->GetPointData()->SetScalars(scalars);

  // (A) Convert CellData to PointData.
  auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
  c2p->SetInputData(ug);
  c2p->PassCellDataOn();    // Copy CellData to PointData.
  c2p->Update();

  // (B) Feed the converted data into ContourFilter.
  auto contour = vtkSmartPointer<vtkContourFilter>::New();
  contour->SetInputConnection(c2p->GetOutputPort());
  contour->SetInputArrayToProcess(
				  0, 0, 0,
				  vtkDataObject::FIELD_ASSOCIATION_POINTS,  // ← POINTS
				  "density"
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

  // (F) Laplacian smoothing.
  auto laplacian = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
  laplacian->SetInputConnection(normals->GetOutputPort());
  laplacian->SetNumberOfIterations(300);
  laplacian->SetRelaxationFactor(0.3);
  laplacian->BoundarySmoothingOn();
  laplacian->FeatureEdgeSmoothingOff();
  */
  
  // Ensure the renderer receives triangle indices.
  auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
  triangles->SetInputConnection(contour->GetOutputPort());
  triangles->Update();
  vtkPolyData* poly = triangles->GetOutput();
 
  if (params.verbose) {
    std::cout << "[After ContourFilter]\n"
	      << "  Polys = " << poly->GetNumberOfPolys() << "\n";
  }

  // 6) Convert to Mesh.
  Mesh mesh;
  // Vertices.
  {
    auto pts = poly->GetPoints();
    if (!pts) return mesh;
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
  // Indices.
  {
    auto cells = poly->GetPolys();
    cells->InitTraversal();
    vtkIdType npts;
    const vtkIdType *ids;
    while (cells->GetNextCell(npts, ids)) {
      if (npts != 3) continue;
      mesh.indices.push_back(static_cast<unsigned>(ids[0]));
      mesh.indices.push_back(static_cast<unsigned>(ids[1]));
      mesh.indices.push_back(static_cast<unsigned>(ids[2]));
    }
  }
  
  return mesh;
}
