enum class DataRefreshKind {
  SnapshotLoaded,
  HaloCatalogReloaded,
  ClumpCatalogReloaded,
  SimulationElementModified
};

void HandleDataRefresh(DataRefreshKind kind,
                       AppState& state,
                       AppRuntimeState& runtime,
                       AppServices& services);
