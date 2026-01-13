#include "render/renderer.h"

#include <GLFW/glfw3.h>

void drawWorld(const World& world) {
  unsigned int bound = 0;
  bool started = false;

  for (const ChunkCoord& coord : world.visibleChunks) {
    const Chunk* chunk = findChunk(world, coord.cx, coord.cy, coord.cz);
    if (!chunk) {
      continue;
    }
    for (const Quad& quad : chunk->quads) {
      if (!started || quad.textureId != bound) {
        if (started) {
          glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, quad.textureId);
        glBegin(GL_QUADS);
        bound = quad.textureId;
        started = true;
      }

      for (const Vertex& vertex : quad.v) {
        glColor3f(vertex.r, vertex.g, vertex.b);
        glTexCoord2f(vertex.u, vertex.v);
        glVertex3f(vertex.x, vertex.y, vertex.z);
      }
    }
  }

  if (started) {
    glEnd();
  }
}

void drawBlockOutline(int wx, int wy, int wz) {
  const float epsilon = 0.02f;
  float x0 = static_cast<float>(wx) - epsilon;
  float y0 = static_cast<float>(wy) - epsilon;
  float z0 = static_cast<float>(wz) - epsilon;
  float x1 = static_cast<float>(wx + 1) + epsilon;
  float y1 = static_cast<float>(wy + 1) + epsilon;
  float z1 = static_cast<float>(wz + 1) + epsilon;

  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(2.0f);
  glColor4f(1.0f, 1.0f, 1.0f, 0.6f);

  glBegin(GL_LINES);
  glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
  glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
  glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
  glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);

  glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
  glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
  glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
  glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);

  glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
  glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
  glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
  glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
  glEnd();

  glLineWidth(1.0f);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
}
