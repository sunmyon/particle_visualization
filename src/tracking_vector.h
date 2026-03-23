#pragma once
#include <vector>
#include <mutex>
#include <iostream>


extern std::size_t g_totalAllocated;
extern std::mutex g_mutex;

template<typename T>
struct TrackingAllocator {
    using value_type = T;

    TrackingAllocator() = default;
    
    template<typename U>
    constexpr TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        std::size_t bytes = n * sizeof(T);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_totalAllocated += bytes;
        }
	if(bytes > 1024. * 1024.)
	  std::cout << "Allocating " << bytes << " bytes. Total allocated: " << g_totalAllocated/1024./1024. << " Mbytes." << std::endl;
        return static_cast<T*>(::operator new(bytes));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::size_t bytes = n * sizeof(T);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_totalAllocated -= bytes;
        }
	if(bytes > 1024. * 1024.)
	  std::cout << "Deallocating " << bytes << " bytes. Total allocated: " << g_totalAllocated/1024./1024. << " Mbytes." << std::endl;
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
