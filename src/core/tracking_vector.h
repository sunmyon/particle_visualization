#pragma once
#include <vector>
#include <mutex>
#include <iostream>

namespace tracking_alloc_detail {

inline std::mutex& get_mutex()
{
  static std::mutex* m = new std::mutex();
  return *m;
}

inline std::size_t& get_total_allocated()
{
  static std::size_t* total = new std::size_t(0);
  return *total;
}

} 

template<typename T>
struct TrackingAllocator {
  using value_type = T;

  TrackingAllocator() = default;

  template<typename U>
  constexpr TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    std::size_t bytes = n * sizeof(T);
    {
      std::lock_guard<std::mutex> lock(tracking_alloc_detail::get_mutex());
      tracking_alloc_detail::get_total_allocated() += bytes;
    }
    if (bytes > 1024. * 1024.) {
      std::cout << "Allocating " << bytes
                << " bytes. Total allocated: "
                << tracking_alloc_detail::get_total_allocated() / 1024. / 1024.
                << " Mbytes." << std::endl;
    }
    return static_cast<T*>(::operator new(bytes));
  }

  void deallocate(T* p, std::size_t n) noexcept {
    std::size_t bytes = n * sizeof(T);
    {
      std::lock_guard<std::mutex> lock(tracking_alloc_detail::get_mutex());
      tracking_alloc_detail::get_total_allocated() -= bytes;
    }
    if (bytes > 1024. * 1024.) {
      std::cout << "Deallocating " << bytes
                << " bytes. Total allocated: "
                << tracking_alloc_detail::get_total_allocated() / 1024. / 1024.
                << " Mbytes." << std::endl;
    }
    ::operator delete(p);
  }
};

template<typename T, typename U>
bool operator==(const TrackingAllocator<T>&, const TrackingAllocator<U>&) {
  return true;
}

template<typename T, typename U>
bool operator!=(const TrackingAllocator<T>& a, const TrackingAllocator<U>& b) {
  return !(a == b);
}

template<typename T>
using TrackingVector = std::vector<T, TrackingAllocator<T>>;
