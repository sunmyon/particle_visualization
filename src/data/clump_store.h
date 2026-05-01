#pragma once

#include <string>
#include <vector>
#include "data/clump_data.h"

class ClumpStore {
public:
  bool empty() const { return clumps_.empty(); }
  size_t size() const { return clumps_.size(); }

  void clear() {
    clumps_.clear();
    filePath_.clear();
    loaded_ = false;
    selectedIndex_ = -1;
  }

  bool loaded() const { return loaded_; }
  void setLoaded(bool v) { loaded_ = v; }

  const std::string& filePath() const { return filePath_; }
  void setFilePath(const std::string& path) { filePath_ = path; }

  const std::vector<ClumpData>& clumps() const { return clumps_; }
  std::vector<ClumpData>& clumps() { return clumps_; }

  const ClumpData& clump(int i) const { return clumps_[i]; }
  ClumpData& clump(int i) { return clumps_[i]; }

  int selectedIndex() const { return selectedIndex_; }
  void setSelectedIndex(int i) { selectedIndex_ = i; }

  void setClumps(std::vector<ClumpData>&& clumps) {
    clumps_ = std::move(clumps);
    loaded_ = !clumps_.empty();
  }

  int findIndexByClumpID(int clumpID) const {
    for (size_t i = 0; i < clumps_.size(); ++i) 
      if (clumps_[i].clumpID == clumpID) 
	return static_cast<int>(i);
    return -1;
  }
  
private:
  bool loaded_ = false;
  std::string filePath_;
  std::vector<ClumpData> clumps_;
  int selectedIndex_ = -1;
};
