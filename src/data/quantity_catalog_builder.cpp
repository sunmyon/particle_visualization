#include "data/simulation_block.h"
#include "core/quantity.h"

#include <cstdint>

namespace {
const char* LoadedFieldNameForQuantity(QuantityId q)
{
  switch (q) {
  case QuantityId::Density: return "density";
  case QuantityId::Temperature: return "temperature";
  case QuantityId::Mass: return "mass";
  case QuantityId::Hsml: return "Hsml";
  case QuantityId::B:
  case QuantityId::Beta: return kBfieldKey;
  case QuantityId::Metallicity: return kMetallicityKey;
  case QuantityId::ElectronAbundance: return kElectronAbundanceKey;
  case QuantityId::H2Abundance: return kH2AbundanceKey;
  case QuantityId::HDAbundance: return kHDAbundanceKey;
  case QuantityId::J21: return kJ21Key;
  case QuantityId::Val: return kVal1Key;
  case QuantityId::Val2: return kVal2Key;
  default: return nullptr;
  }
}

uint8_t LoadedTypeMaskForQuantity(const SimulationBlock& block, QuantityId q)
{
  const char* name = LoadedFieldNameForQuantity(q);
  if (!name || !*name) {
    return 0x3fu;
  }

  auto it = block.loadedFieldTypeMask.find(name);
  if (it != block.loadedFieldTypeMask.end()) {
    return static_cast<uint8_t>(it->second & 0x3fu);
  }

  if (!block.loadedFieldTypeMask.empty()) {
    return q == QuantityId::Mass ? 0x3fu : 0x00u;
  }

  return 0x3fu;
}
}

void BuildQuantityCatalog(const SimulationBlock& block, QuantityCatalogState& out){
  out.nAllQ = 0; out.nUIQ = 0;
  out.nUIQByType.fill(0);

  auto pushAll = [&](QuantityId q){ out.allQ[out.nAllQ++] = q; };
  auto pushUI  = [&](QuantityId q){
    out.uiQ[out.nUIQ++] = q;
    const uint8_t mask = LoadedTypeMaskForQuantity(block, q);
    for (int type = 0; type < kNumTypes; ++type) {
      if ((mask & static_cast<uint8_t>(1u << type)) == 0) {
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
