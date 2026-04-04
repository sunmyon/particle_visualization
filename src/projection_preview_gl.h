#pragma once

#include <cstdint>
#include <glad/glad.h>

struct ProjectionImage;
struct ProjectionPreviewUIState;

class ProjectionPreviewGL {
public:
  void update(const ProjectionImage& img);
  ProjectionPreviewUIState makeUIState() const;
  void destroy();

private:
  GLuint tex_ = 0;
  int width_ = 0;
  int height_ = 0;
  uint64_t uploadedVersion_ = 0;
};
