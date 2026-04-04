#pragma once

class IConvexHull {
public:
  virtual ~IConvexHull() = default;
  virtual bool isInside(const std::array<double, 3>& pt) const = 0;
};
