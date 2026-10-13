// Blockfall - small math library (row-vector convention: v' = v * M)
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace bf {

constexpr float PI = 3.14159265358979f;
inline float deg2rad(float d) { return d * PI / 180.0f; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float lengthXZ(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-6f ? v / l : Vec3(0, 0, 0);
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// 4x4 matrix, row-major storage, row-vector convention (like DirectXMath).
struct Mat4 {
    float m[4][4];
    static Mat4 identity() {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r.m[i][j] = (i == j) ? 1.0f : 0.0f;
        return r;
    }
    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r = identity();
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat4 rotationY(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c;  r.m[0][2] = -s;
        r.m[2][0] = s;  r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationX(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[1][1] = c;  r.m[1][2] = s;
        r.m[2][1] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationZ(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c;  r.m[0][1] = s;
        r.m[1][0] = -s; r.m[1][1] = c;
        return r;
    }
    // Left-handed look-at (DirectX style).
    static Mat4 lookAtLH(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 z = normalize(at - eye);
        Vec3 x = normalize(cross(up, z));
        Vec3 y = cross(z, x);
        Mat4 r = identity();
        r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
        r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
        r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
        r.m[3][0] = -dot(x, eye); r.m[3][1] = -dot(y, eye); r.m[3][2] = -dot(z, eye);
        return r;
    }
    // Left-handed orthographic, depth 0..1.
    static Mat4 orthoLH(float w, float h, float zn, float zf) {
        Mat4 r{};
        r.m[0][0] = 2.0f / w;
        r.m[1][1] = 2.0f / h;
        r.m[2][2] = 1.0f / (zf - zn);
        r.m[3][2] = -zn / (zf - zn);
        r.m[3][3] = 1.0f;
        return r;
    }
    // Left-handed perspective, depth 0..1 (DirectX style).
    static Mat4 perspectiveLH(float fovY, float aspect, float zn, float zf) {
        Mat4 r{};
        float ys = 1.0f / std::tan(fovY * 0.5f);
        float xs = ys / aspect;
        r.m[0][0] = xs;
        r.m[1][1] = ys;
        r.m[2][2] = zf / (zf - zn);
        r.m[2][3] = 1.0f;
        r.m[3][2] = -zn * zf / (zf - zn);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

// General 4x4 inverse (cofactor method). Returns identity if singular.
inline Mat4 inverse(const Mat4& a) {
    const float* m = &a.m[0][0];
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return Mat4::identity();
    Mat4 r;
    float* o = &r.m[0][0];
    for (int i = 0; i < 16; ++i) o[i] = inv[i] / det;
    return r;
}

inline Vec4 transform(const Vec4& v, const Mat4& m) {
    return {
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2],
        v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3],
    };
}
inline Vec3 transformPoint(const Vec3& p, const Mat4& m) {
    Vec4 r = transform(Vec4(p.x, p.y, p.z, 1.0f), m);
    return {r.x, r.y, r.z};
}

struct AABB {
    Vec3 min, max;
    AABB() = default;
    AABB(const Vec3& a, const Vec3& b) : min(a), max(b) {}
    static AABB fromCenter(const Vec3& c, const Vec3& half) { return {c - half, c + half}; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x &&
               min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
};

struct Ray {
    Vec3 origin;
    Vec3 dir;  // normalized
    Vec3 at(float t) const { return origin + dir * t; }
};

// Slab test. Returns entry distance in tOut and outward normal of the hit face.
inline bool rayAABB(const Ray& r, const AABB& b, float maxT, float& tOut, Vec3& nOut) {
    float tmin = 0.0f, tmax = maxT;
    Vec3 n(0, 0, 0);
    const float o[3] = {r.origin.x, r.origin.y, r.origin.z};
    const float d[3] = {r.dir.x, r.dir.y, r.dir.z};
    const float lo[3] = {b.min.x, b.min.y, b.min.z};
    const float hi[3] = {b.max.x, b.max.y, b.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        float inv = 1.0f / d[i];
        float t1 = (lo[i] - o[i]) * inv;
        float t2 = (hi[i] - o[i]) * inv;
        float sign = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
        if (t1 > tmin) {
            tmin = t1;
            n = Vec3(0, 0, 0);
            if (i == 0) n.x = sign; else if (i == 1) n.y = sign; else n.z = sign;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    tOut = tmin;
    nOut = n;
    return true;
}

// Moller-Trumbore, two-sided.
inline bool rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c, float maxT,
                        float& tOut) {
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = cross(r.dir, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-8f) return false;
    float inv = 1.0f / det;
    Vec3 s = r.origin - a;
    float u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = cross(s, e1);
    float v = dot(r.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = dot(e2, q) * inv;
    if (t < 0.0f || t > maxT) return false;
    tOut = t;
    return true;
}

}  // namespace bf
