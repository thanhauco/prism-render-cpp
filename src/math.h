#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism {

constexpr float kPi = 3.14159265358979323846f;

constexpr float saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
constexpr float mix(float a, float b, float t) { return a + (b - a) * t; }
constexpr float radians(float degrees) { return degrees * kPi / 180.0f; }

inline float smoothstep(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

struct float3 {
    float x = 0, y = 0, z = 0;

    constexpr float3() = default;
    constexpr explicit float3(float s) : x(s), y(s), z(s) {}
    constexpr float3(float x, float y, float z) : x(x), y(y), z(z) {}

    constexpr float3 operator-() const { return {-x, -y, -z}; }
    float3& operator+=(float3 b) { x += b.x; y += b.y; z += b.z; return *this; }
    float3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float3& operator*=(float3 b) { x *= b.x; y *= b.y; z *= b.z; return *this; }
};

constexpr float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr float3 operator*(float s, float3 a) { return a * s; }
constexpr float3 operator/(float3 a, float s) { return a * (1.0f / s); }

constexpr float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr float3 cross(float3 a, float3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(float3 a) { return std::sqrt(dot(a, a)); }
inline float3 normalize(float3 a) {
    float l = length(a);
    return l > 0.0f ? a / l : a;
}
constexpr float3 reflect(float3 i, float3 n) { return i - n * (2.0f * dot(n, i)); }
constexpr float3 mix(float3 a, float3 b, float t) { return a + (b - a) * t; }
constexpr float3 min(float3 a, float3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
constexpr float3 max(float3 a, float3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct float4 {
    float x = 0, y = 0, z = 0, w = 0;

    constexpr float4() = default;
    constexpr float4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    constexpr float4(float3 v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    constexpr float3 xyz() const { return {x, y, z}; }
};

constexpr float4 operator+(float4 a, float4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr float4 operator-(float4 a, float4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr float4 operator*(float4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }

// Column-major 4x4 matrix: c[i] is column i (same convention as OpenGL / Filament's math::mat4f).
struct mat4 {
    float4 c[4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    constexpr float4 operator*(float4 v) const {
        return c[0] * v.x + c[1] * v.y + c[2] * v.z + c[3] * v.w;
    }
    constexpr mat4 operator*(const mat4& m) const {
        mat4 r;
        for (int i = 0; i < 4; ++i) r.c[i] = (*this) * m.c[i];
        return r;
    }
    constexpr float3 transformPoint(float3 p) const { return ((*this) * float4(p, 1.0f)).xyz(); }
    constexpr float3 transformVector(float3 v) const { return ((*this) * float4(v, 0.0f)).xyz(); }
    constexpr float3 translation() const { return c[3].xyz(); }
};

inline mat4 translation(float3 t) {
    mat4 m;
    m.c[3] = float4(t, 1.0f);
    return m;
}

inline mat4 scaling(float s) {
    mat4 m;
    m.c[0].x = m.c[1].y = m.c[2].z = s;
    return m;
}

inline mat4 lookAt(float3 eye, float3 target, float3 up) {
    float3 f = normalize(target - eye);
    float3 s = normalize(cross(f, up));
    float3 u = cross(s, f);
    mat4 m;
    m.c[0] = {s.x, u.x, -f.x, 0};
    m.c[1] = {s.y, u.y, -f.y, 0};
    m.c[2] = {s.z, u.z, -f.z, 0};
    m.c[3] = {-dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    return m;
}

inline mat4 perspective(float fovY, float aspect, float near, float far) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    mat4 m;
    m.c[0] = {f / aspect, 0, 0, 0};
    m.c[1] = {0, f, 0, 0};
    m.c[2] = {0, 0, (far + near) / (near - far), -1};
    m.c[3] = {0, 0, 2.0f * far * near / (near - far), 0};
    return m;
}

inline mat4 orthographic(float l, float r, float b, float t, float n, float f) {
    mat4 m;
    m.c[0] = {2.0f / (r - l), 0, 0, 0};
    m.c[1] = {0, 2.0f / (t - b), 0, 0};
    m.c[2] = {0, 0, -2.0f / (f - n), 0};
    m.c[3] = {-(r + l) / (r - l), -(t + b) / (t - b), -(f + n) / (f - n), 1};
    return m;
}

} // namespace prism
