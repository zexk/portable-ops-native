#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace mgs::fulton_geometry {
using Point = std::array<float, 3>;
using Matrix = std::array<float, 16>; // basis columns, then translation

inline Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

inline Point transform(const Matrix &matrix, const Point &point) {
    Point result{};
    for (unsigned i = 0; i < 3; ++i)
        result[i] = matrix[12 + i] + matrix[i] * point[0] + matrix[4 + i] * point[1] + matrix[8 + i] * point[2];
    return result;
}

template<class Memory>
std::optional<Matrix> bone(const Memory &memory, std::uint32_t object, unsigned index) {
    if (!object || !memory.contains(object + 0xacu, 4)) return {};
    const auto bones = memory.load32(object + 0xacu);
    if (!bones || !memory.contains(bones, (index + 1) * 64)) return {};
    Matrix matrix;
    for (unsigned i = 0; i < matrix.size(); ++i) {
        matrix[i] = std::bit_cast<float>(memory.load32(bones + index * 64 + i * 4));
        if (!std::isfinite(matrix[i])) return {};
    }
    for (unsigned i = 0; i < 3; ++i) {
        const float length = matrix[i * 4] * matrix[i * 4] + matrix[i * 4 + 1] * matrix[i * 4 + 1] +
                             matrix[i * 4 + 2] * matrix[i * 4 + 2];
        if (length < 0.25f || length > 4.0f) return {};
    }
    return matrix;
}

// PW MC 0x247E10: bone 2 local (0,100,-200), bone 4 local (0,100,-300).
// 0xC3C90 aims from the second point toward the harness, with zero roll;
// 0x2476D0 constructs Ry * Rx, then 0xAB090 supplies model Rx(-1.57).
inline Matrix harness(const Point &anchor, const Point &other) {
    const float x = anchor[0] - other[0], y = anchor[1] - other[1], z = anchor[2] - other[2];
    const float yaw = std::atan2(x, z);
    const float pitch = std::atan2(-y, std::hypot(x, z)) - 1.57f;
    const float sy = std::sin(yaw), cy = std::cos(yaw), sx = std::sin(pitch), cx = std::cos(pitch);
    return {cy, 0, -sy, 0, sy * sx, cx, cy * sx, 0, sy * cx, -sx, cy * cx, 0,
            anchor[0], anchor[1], anchor[2], 1};
}

// Smooth the shortest arc. One-eighth per simulation tick approximates the
// captured convergence; the exact PW integer update is not yet mapped.
inline std::uint16_t turn(std::uint16_t current, std::uint16_t target) {
    const unsigned raw = (unsigned(target) - unsigned(current)) & 0xffffu;
    const int delta = raw >= 0x8000u ? int(raw) - 0x10000 : int(raw);
    if (!delta) return current;
    int step = delta / 8;
    if (!step) step = delta > 0 ? 1 : -1;
    return std::uint16_t(int(current) + step);
}

inline Matrix scaled(Matrix matrix, float scale) {
    for (unsigned column = 0; column < 3; ++column)
        for (unsigned row = 0; row < 3; ++row) matrix[column * 4 + row] *= scale;
    return matrix;
}
} // namespace mgs::fulton_geometry
