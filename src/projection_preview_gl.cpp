#include "projection_preview_gl.h"
#include "make_2D_projection_map.h"
#include "UI.h"

ProjectionPreviewUIState ProjectionPreviewGL::makeUIState() const
{
  ProjectionPreviewUIState ui;
  ui.textureId = (void*)(intptr_t)tex_;
  ui.width = width_;
  ui.height = height_;
  ui.valid = (tex_ != 0 && width_ > 0 && height_ > 0);
  return ui;
}

void ProjectionPreviewGL::update(const ProjectionImage& img)
{
  if (img.width <= 0 || img.height <= 0) return;
  if (img.rgb.size() != static_cast<size_t>(img.width * img.height * 3)) return;
  if (img.version == uploadedVersion_) return;

  if (tex_ == 0) {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    width_ = 0;
    height_ = 0;
  }

  glBindTexture(GL_TEXTURE_2D, tex_);

  GLint prevUnpack;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if (img.width != width_ || img.height != height_) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,
                 img.width, img.height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
    width_ = img.width;
    height_ = img.height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    img.width, img.height,
                    GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
  }

  glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
  glBindTexture(GL_TEXTURE_2D, 0);

  uploadedVersion_ = img.version;
}

void ProjectionPreviewGL::destroy()
{
  if (tex_ != 0) {
    glDeleteTextures(1, &tex_);
    tex_ = 0;
  }

  width_ = 0;
  height_ = 0;
  uploadedVersion_ = 0;
}
