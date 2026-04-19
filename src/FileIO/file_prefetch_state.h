#pragma once
#include "FileIO/prefetch_cache.h"

#include <future>
#include <atomic>
#include <mutex>

struct PrefetchState {
  PrefetchCache cache;
  std::future<void> future;
  std::atomic<bool> running{false};
  std::atomic<int> generation{0};
  std::mutex mutex;
};
