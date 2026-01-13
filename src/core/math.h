#pragma once

#include <cmath>

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Mat4 {
  float m[16];
};

Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 v, float s);

float dot(Vec3 a, Vec3 b);
Vec3 cross(Vec3 a, Vec3 b);
Vec3 normalize(Vec3 v);

float radians(float degrees);
float clamp01(float value);

Mat4 perspective(float fovDegrees, float aspect, float nearZ, float farZ);
Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up);
