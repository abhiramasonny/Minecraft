#include "render/renderer.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace {
constexpr float kChunkSize = 16.0f;
constexpr float kChunkEpsilon = 0.02f;

GLuint g_worldProgram = 0;
GLint g_worldLightDirLoc = -1;
GLint g_worldAmbientLoc = -1;
GLint g_worldDiffuseLoc = -1;
GLint g_worldTextureLoc = -1;
GLint g_worldTexScaleLoc = -1;
GLint g_worldTexOffsetLoc = -1;
GLint g_worldAlphaMulLoc = -1;

WorldLight g_worldLight = {};

constexpr int kWaterFrameCount = 32;
constexpr float kWaterFrameDuration = 0.1f;

const char* kWorldVertexShader = R"(
#version 120
uniform vec2 uTexScale;
uniform vec2 uTexOffset;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vColor;
void main() {
  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
  vTexCoord = gl_MultiTexCoord0.xy * uTexScale + uTexOffset;
  vNormal = gl_Normal;
  vColor = gl_Color;
}
)";

const char* kWorldFragmentShader = R"(
#version 120
uniform sampler2D uTexture;
uniform vec3 uLightDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform float uAlphaMul;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vColor;
void main() {
  vec3 normal = normalize(vNormal);
  vec3 lightDir = normalize(uLightDir);
  float ndotl = max(dot(normal, lightDir), 0.0);
  vec3 light = uAmbient + uDiffuse * ndotl;

  float faceFactor = mix(0.8, 1.0, clamp(normal.y * 0.5 + 0.5, 0.0, 1.0));
  light = clamp(light * faceFactor, 0.0, 1.0);
  
  vec4 texel = texture2D(uTexture, vTexCoord);
  vec3 rgb = texel.rgb * vColor.rgb * light;
  gl_FragColor = vec4(rgb, texel.a * uAlphaMul);
}
)";

GLuint compileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    char log[1024];
    GLsizei length = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), &length, log);
    std::fprintf(stderr, "Shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool initWorldProgram() {
  GLuint vs = compileShader(GL_VERTEX_SHADER, kWorldVertexShader);
  if (vs == 0) {
    return false;
  }
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, kWorldFragmentShader);
  if (fs == 0) {
    glDeleteShader(vs);
    return false;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    char log[1024];
    GLsizei length = 0;
    glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), &length, log);
    std::fprintf(stderr, "Shader link failed: %s\n", log);
    glDeleteProgram(program);
    program = 0;
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (program == 0) {
    return false;
  }
  g_worldProgram = program;
  g_worldLightDirLoc = glGetUniformLocation(program, "uLightDir");
  g_worldAmbientLoc = glGetUniformLocation(program, "uAmbient");
  g_worldDiffuseLoc = glGetUniformLocation(program, "uDiffuse");
  g_worldTextureLoc = glGetUniformLocation(program, "uTexture");
  g_worldTexScaleLoc = glGetUniformLocation(program, "uTexScale");
  g_worldTexOffsetLoc = glGetUniformLocation(program, "uTexOffset");
  g_worldAlphaMulLoc = glGetUniformLocation(program, "uAlphaMul");
  return true;
}

void uploadChunkMesh(Chunk& chunk) {
  if (chunk.vbo == 0) {
    glGenBuffers(1, &chunk.vbo);
  }
  if (chunk.ibo == 0) {
    glGenBuffers(1, &chunk.ibo);
  }
  glBindBuffer(GL_ARRAY_BUFFER, chunk.vbo);
  if (!chunk.vertices.empty()) {
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(chunk.vertices.size() * sizeof(Vertex)),
                 chunk.vertices.data(),
                 GL_STATIC_DRAW);
  } else {
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ibo);
  if (!chunk.indices.empty()) {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(chunk.indices.size() * sizeof(uint32_t)),
                 chunk.indices.data(),
                 GL_STATIC_DRAW);
  } else {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  chunk.meshDirty = false;
}
} // namespace

void initRenderer() {
  if (!initWorldProgram()) {
    std::fprintf(stderr, "World shader unavailable; using fixed pipeline.\n");
  }
}

void shutdownRenderer() {
  if (g_worldProgram != 0) {
    glDeleteProgram(g_worldProgram);
    g_worldProgram = 0;
  }
}

void setWorldLight(const WorldLight& light) {
  g_worldLight.direction = normalize(light.direction);
  g_worldLight.ambient = light.ambient;
  g_worldLight.diffuse = light.diffuse;
}

void uploadVisibleChunkMeshes(World& world) {
  int uploaded = 0;
  int maxPerFrame = 8;
  
  for (const ChunkCoord& coord : world.visibleChunks) {
    if (uploaded >= maxPerFrame) {
      break;
    }
    Chunk* chunk = findChunkMutable(world, coord.cx, coord.cy, coord.cz);
    if (!chunk || !chunk->meshDirty) {
      continue;
    }
    uploadChunkMesh(*chunk);
    uploaded++;
  }
}

void drawWorld(const World& world, double timeNow) {
  if (g_worldProgram != 0) {
    glUseProgram(g_worldProgram);
    if (g_worldLightDirLoc >= 0) {
      glUniform3f(g_worldLightDirLoc,
                  g_worldLight.direction.x,
                  g_worldLight.direction.y,
                  g_worldLight.direction.z);
    }
    if (g_worldAmbientLoc >= 0) {
      glUniform3f(g_worldAmbientLoc,
                  g_worldLight.ambient.x,
                  g_worldLight.ambient.y,
                  g_worldLight.ambient.z);
    }
    if (g_worldDiffuseLoc >= 0) {
      glUniform3f(g_worldDiffuseLoc,
                  g_worldLight.diffuse.x,
                  g_worldLight.diffuse.y,
                  g_worldLight.diffuse.z);
    }
    if (g_worldTextureLoc >= 0) {
      glUniform1i(g_worldTextureLoc, 0);
    }
    if (g_worldTexScaleLoc >= 0) {
      glUniform2f(g_worldTexScaleLoc, 1.0f, 1.0f);
    }
    if (g_worldTexOffsetLoc >= 0) {
      glUniform2f(g_worldTexOffsetLoc, 0.0f, 0.0f);
    }
    if (g_worldAlphaMulLoc >= 0) {
      glUniform1f(g_worldAlphaMulLoc, 1.0f);
    }
  }

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);

  unsigned int lastTextureId = 0;
  int chunksDrawn = 0;
  bool blending = false;
  bool depthWrite = true;
  bool animated = false;
  float lastScaleY = 1.0f;
  float lastOffsetY = 0.0f;
  float lastAlphaMul = 1.0f;

  for (const ChunkCoord& coord : world.visibleChunks) {
    const Chunk* chunk = findChunk(world, coord.cx, coord.cy, coord.cz);
    if (!chunk || chunk->vertices.empty() || chunk->indices.empty() || chunk->batches.empty() || chunk->blocks.empty()) {
      continue;
    }
    if (chunk->vbo == 0 || chunk->ibo == 0) {
      continue;
    }
    ++chunksDrawn;
    glPushMatrix();
    glTranslatef(static_cast<float>(coord.cx) * kChunkSize,
                 static_cast<float>(coord.cy) * kChunkSize,
                 static_cast<float>(coord.cz) * kChunkSize);
    glBindBuffer(GL_ARRAY_BUFFER, chunk->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->ibo);
    glVertexPointer(3, GL_SHORT, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glTexCoordPointer(2, GL_SHORT, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glNormalPointer(GL_BYTE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

    for (const MeshBatch& batch : chunk->batches) {
      if (batch.textureId != lastTextureId) {
        glBindTexture(GL_TEXTURE_2D, batch.textureId);
        lastTextureId = batch.textureId;
      }
      if (batch.transparent != blending) {
        if (batch.transparent) {
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glDepthMask(GL_FALSE);
          depthWrite = false;
        } else {
          glDisable(GL_BLEND);
          glDepthMask(GL_TRUE);
          depthWrite = true;
        }
        blending = batch.transparent;
      }
      if (batch.animated != animated) {
        animated = batch.animated;
      }
      if (g_worldProgram != 0 && (g_worldTexScaleLoc >= 0 || g_worldTexOffsetLoc >= 0)) {
        float scaleY = 1.0f;
        float offsetY = 0.0f;
        if (animated) {
          int frame = static_cast<int>(std::floor(timeNow / kWaterFrameDuration)) % kWaterFrameCount;
          scaleY = 1.0f / static_cast<float>(kWaterFrameCount);
          offsetY = scaleY * static_cast<float>(frame);
        }
        if (scaleY != lastScaleY && g_worldTexScaleLoc >= 0) {
          glUniform2f(g_worldTexScaleLoc, 1.0f, scaleY);
          lastScaleY = scaleY;
        }
        if (offsetY != lastOffsetY && g_worldTexOffsetLoc >= 0) {
          glUniform2f(g_worldTexOffsetLoc, 0.0f, offsetY);
          lastOffsetY = offsetY;
        }
      }
      if (g_worldProgram != 0 && g_worldAlphaMulLoc >= 0) {
        float alphaMul = animated ? 0.7f : 1.0f;
        if (alphaMul != lastAlphaMul) {
          glUniform1f(g_worldAlphaMulLoc, alphaMul);
          lastAlphaMul = alphaMul;
        }
      }
      glDrawElements(GL_TRIANGLES,
                     batch.indexCount,
                     GL_UNSIGNED_INT,
                     reinterpret_cast<void*>(static_cast<std::uintptr_t>(batch.indexOffset * sizeof(uint32_t))));
    }
    glPopMatrix();
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  if (blending) {
    glDisable(GL_BLEND);
  }
  if (!depthWrite) {
    glDepthMask(GL_TRUE);
  }

  if (g_worldProgram != 0) {
    glUseProgram(0);
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
  glDisable(GL_LIGHTING);
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
  glEnable(GL_LIGHTING);
  glEnable(GL_TEXTURE_2D);
}

void drawChunkBounds(const World& world) {
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(1.0f);
  glColor4f(0.2f, 0.8f, 1.0f, 0.55f);

  glBegin(GL_LINES);
  for (const ChunkCoord& coord : world.visibleChunks) {
    float x0 = static_cast<float>(coord.cx) * kChunkSize - kChunkEpsilon;
    float y0 = static_cast<float>(coord.cy) * kChunkSize - kChunkEpsilon;
    float z0 = static_cast<float>(coord.cz) * kChunkSize - kChunkEpsilon;
    float x1 = x0 + kChunkSize + kChunkEpsilon * 2.0f;
    float y1 = y0 + kChunkSize + kChunkEpsilon * 2.0f;
    float z1 = z0 + kChunkSize + kChunkEpsilon * 2.0f;

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
  }
  glEnd();

  glLineWidth(1.0f);
  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
  glEnable(GL_TEXTURE_2D);
}
