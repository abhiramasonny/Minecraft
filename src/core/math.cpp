#include "core/math.h"

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

Vec3 normalize(Vec3 v) {
  float len = std::sqrt(dot(v, v));
  if (len <= 0.00001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

float radians(float degrees) {
  return degrees * 3.1415926535f / 180.0f;
}

float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

Mat4 perspective(float fovDegrees, float aspect, float nearZ, float farZ) {
  float f = 1.0f / std::tan(radians(fovDegrees) * 0.5f);
  Mat4 out = {};
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = (farZ + nearZ) / (nearZ - farZ);
  out.m[11] = -1.0f;
  out.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
  return out;
}

Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
  Vec3 f = normalize(center - eye);
  Vec3 r = normalize(cross(f, up));
  Vec3 u = cross(r, f);

  Mat4 out = {};
  out.m[0] = r.x;
  out.m[1] = u.x;
  out.m[2] = -f.x;
  out.m[3] = 0.0f;

  out.m[4] = r.y;
  out.m[5] = u.y;
  out.m[6] = -f.y;
  out.m[7] = 0.0f;

  out.m[8] = r.z;
  out.m[9] = u.z;
  out.m[10] = -f.z;
  out.m[11] = 0.0f;

  out.m[12] = -dot(r, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = dot(f, eye);
  out.m[15] = 1.0f;

  return out;
}
