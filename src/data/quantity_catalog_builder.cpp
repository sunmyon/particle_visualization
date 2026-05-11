#include "data/simulation_block.h"
#include "core/quantity.h"

void BuildQuantityCatalog(const SimulationBlock& block, QuantityCatalogState& out){
  out.nAllQ = 0; out.nUIQ = 0;
  out.nUIQByType.fill(0);

  auto pushAll = [&](QuantityId q){ out.allQ[out.nAllQ++] = q; };
  auto pushUI  = [&](QuantityId q){
    out.uiQ[out.nUIQ++] = q;
    for (int type = 0; type < kNumTypes; ++type) {
      if (!block.hasQuantityForType(q, type)) {
        continue;
      }
      out.uiQByType[type][out.nUIQByType[type]++] = q;
    }
  };

  for (auto q : {QuantityId::Density, QuantityId::Temperature,
		 QuantityId::Mass, QuantityId::Hsml}) {
    pushAll(q);
    pushUI(q);
  }

  // --- Internal entries, hidden from UI but included in all. ---
  for (auto q : {QuantityId::PosX, QuantityId::PosY, QuantityId::PosZ, QuantityId::Radius, QuantityId::VRad}) {
    pushAll(q);
  }

  auto push_if_has = [&](const auto& view, QuantityId q) {
    if (block.hasSoAAs(view)) {
      pushAll(q);
      pushUI(q);
    }
  };

  push_if_has(soa_views::Bfield,            QuantityId::B);
  push_if_has(soa_views::Bfield,            QuantityId::Beta);
  push_if_has(soa_views::Metallicity,       QuantityId::Metallicity);
  push_if_has(soa_views::ElectronAbundance, QuantityId::ElectronAbundance);
  push_if_has(soa_views::H2Abundance,       QuantityId::H2Abundance);
  push_if_has(soa_views::HDAbundance,       QuantityId::HDAbundance);
  push_if_has(soa_views::J21,               QuantityId::J21);
  push_if_has(soa_views::Val1,              QuantityId::Val);
  push_if_has(soa_views::Val2,              QuantityId::Val2);      
}
