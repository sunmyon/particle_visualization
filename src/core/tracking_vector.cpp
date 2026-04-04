#include "core/tracking_vector.h"

namespace tracking_alloc_detail {
  std::size_t totalAllocated = 0;
  std::mutex mutex;
}
