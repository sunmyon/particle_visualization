enum class DataRefreshKind {
  SnapshotLoaded,
  HaloCatalogReloaded,
  ClumpCatalogReloaded,
  ParticleDataModified
};

void HandleDataRefresh(DataRefreshKind kind,
                       AppState& state,
                       AppRuntimeState& runtime,
                       AppServices& services);
