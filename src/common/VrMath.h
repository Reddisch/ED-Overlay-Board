#pragma once
// Minimal rigid-transform math. Conventions (OpenXR): right-handed, +Y up, -Z forward.

#include <algorithm>
#include <cmath>
#include <numbers>

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;

struct Vec3 {
  float x = 0, y = 0, z = 0;

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 Cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  float Length() const { return std::sqrt(Dot(*this)); }
  Vec3 Normalized() const {
    float l = Length();
    return l > 1e-6f ? *this * (1.0f / l) : Vec3{0, 0, -1};
  }
};

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};

struct Pose {
  float r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  Vec3 t;

  Vec3 Rotate(const Vec3& v) const {
    return {r[0][0] * v.x + r[0][1] * v.y + r[0][2] * v.z,
            r[1][0] * v.x + r[1][1] * v.y + r[1][2] * v.z,
            r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z};
  }
  Vec3 Apply(const Vec3& v) const { return Rotate(v) + t; }

  Pose operator*(const Pose& o) const {
    Pose p;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) p.r[i][j] = r[i][0] * o.r[0][j] + r[i][1] * o.r[1][j] + r[i][2] * o.r[2][j];
    p.t = Rotate(o.t) + t;
    return p;
  }

  Pose Inverse() const {
    Pose p;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) p.r[i][j] = r[j][i];
    p.t = p.Rotate(t) * -1.0f;
    return p;
  }

  Vec3 Forward() const { return {-r[0][2], -r[1][2], -r[2][2]}; }

  static Pose Translation(const Vec3& v) {
    Pose p;
    p.t = v;
    return p;
  }
  static Pose RotX(float a) {
    Pose p;
    float c = std::cos(a), s = std::sin(a);
    p.r[1][1] = c; p.r[1][2] = -s;
    p.r[2][1] = s; p.r[2][2] = c;
    return p;
  }
  static Pose RotY(float a) {
    Pose p;
    float c = std::cos(a), s = std::sin(a);
    p.r[0][0] = c; p.r[0][2] = s;
    p.r[2][0] = -s; p.r[2][2] = c;
    return p;
  }
  static Pose RotZ(float a) {
    Pose p;
    float c = std::cos(a), s = std::sin(a);
    p.r[0][0] = c; p.r[0][1] = -s;
    p.r[1][0] = s; p.r[1][1] = c;
    return p;
  }

  // Euler angles in degrees: x = pitch, y = yaw, z = roll. R = Ry(yaw) * Rx(pitch) * Rz(roll)
  static Pose FromEulerDeg(const Vec3& position, const Vec3& eulerDeg) {
    Pose p = RotY(eulerDeg.y * kDegToRad) * RotX(eulerDeg.x * kDegToRad) * RotZ(eulerDeg.z * kDegToRad);
    p.t = position;
    return p;
  }

  Vec3 EulerDeg() const {
    float sp = std::clamp(-r[1][2], -1.0f, 1.0f);
    float pitch = std::asin(sp), yaw, roll;
    if (std::fabs(r[1][2]) < 0.9999f) {
      yaw = std::atan2(r[0][2], r[2][2]);
      roll = std::atan2(r[1][0], r[1][1]);
    } else {
      yaw = std::atan2(-r[2][0], r[0][0]);
      roll = 0.0f;
    }
    return {pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg};
  }

  // Keeps the position but only the rotation around the vertical axis (for recentering).
  Pose YawOnly() const {
    Vec3 f = Forward();
    Pose p = RotY(std::atan2(-f.x, -f.z));
    p.t = t;
    return p;
  }

  // Rotation whose -Z axis points along `forward`.
  static Pose LookRotation(const Vec3& position, const Vec3& forward, const Vec3& upHint = {0, 1, 0}) {
    Vec3 z = (forward * -1.0f).Normalized();
    Vec3 x = upHint.Cross(z);
    if (x.Length() < 1e-4f) x = Vec3{1, 0, 0}.Cross(z);
    x = x.Normalized();
    Vec3 y = z.Cross(x);
    Pose p;
    p.r[0][0] = x.x; p.r[0][1] = y.x; p.r[0][2] = z.x;
    p.r[1][0] = x.y; p.r[1][1] = y.y; p.r[1][2] = z.y;
    p.r[2][0] = x.z; p.r[2][1] = y.z; p.r[2][2] = z.z;
    p.t = position;
    return p;
  }

  static Pose FromQuat(const Quat& q, const Vec3& position) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Pose p;
    p.r[0][0] = 1 - 2 * (y * y + z * z); p.r[0][1] = 2 * (x * y - z * w);     p.r[0][2] = 2 * (x * z + y * w);
    p.r[1][0] = 2 * (x * y + z * w);     p.r[1][1] = 1 - 2 * (x * x + z * z); p.r[1][2] = 2 * (y * z - x * w);
    p.r[2][0] = 2 * (x * z - y * w);     p.r[2][1] = 2 * (y * z + x * w);     p.r[2][2] = 1 - 2 * (x * x + y * y);
    p.t = position;
    return p;
  }

  Quat ToQuat() const {
    Quat q;
    float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
      float s = std::sqrt(trace + 1.0f) * 2;
      q.w = 0.25f * s;
      q.x = (r[2][1] - r[1][2]) / s;
      q.y = (r[0][2] - r[2][0]) / s;
      q.z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
      float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
      q.w = (r[2][1] - r[1][2]) / s;
      q.x = 0.25f * s;
      q.y = (r[0][1] + r[1][0]) / s;
      q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
      float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
      q.w = (r[0][2] - r[2][0]) / s;
      q.x = (r[0][1] + r[1][0]) / s;
      q.y = 0.25f * s;
      q.z = (r[1][2] + r[2][1]) / s;
    } else {
      float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
      q.w = (r[1][0] - r[0][1]) / s;
      q.x = (r[0][2] + r[2][0]) / s;
      q.y = (r[1][2] + r[2][1]) / s;
      q.z = 0.25f * s;
    }
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-6f) {
      q.x /= n; q.y /= n; q.z /= n; q.w /= n;
    }
    return q;
  }
};
