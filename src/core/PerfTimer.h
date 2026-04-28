#pragma once
#include <chrono>
#include <cstdio>
#include <string>

class ScopeTimer {
public:
  using clock = std::chrono::steady_clock;

  explicit ScopeTimer(const char* name)
      : name_(name), start_(clock::now()) {}

  ~ScopeTimer() {
    const auto end = clock::now();
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();

    // Use different display units below and above 1 ms.
    if (us >= 1000) {
      std::fprintf(stderr, "[TIMER] %s: %.3f ms\n", name_, us / 1000.0);
    } else {
      std::fprintf(stderr, "[TIMER] %s: %lld us\n", name_, (long long)us);
    }
  }

private:
  const char* name_;
  clock::time_point start_;
};

// Macros that include line numbers so names stay unique after copy-paste.
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define TIME_FUNCTION() ScopeTimer CONCAT(_scope_timer_, __LINE__)(__func__)
#define TIME_SCOPE(name_literal) ScopeTimer CONCAT(_scope_timer_, __LINE__)(name_literal)
