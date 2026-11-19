#pragma once
// Per-vertex PSP lighting, adapted from Bigmission/Yakumo's MIT ge.vert.
// Shared by the Direct3D 11 and Vulkan renderers.
//
// Everything that depends only on the draw (material and light colours, the
// direction and half vector of a directional light, a spot light's axis) is
// prepared once per draw. The per-vertex arithmetic keeps the original order
// of operations, so lit colours are unchanged bit for bit.
#include "gpu/ge_state.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sims2::gpu::vertex_lighting {
using V4 = std::array<float, 4>;

inline V4 multiply(const std::array<float, 16> &matrix, const V4 &v) {
    V4 out{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            out[row] += matrix[col * 4 + row] * v[col];
    return out;
}
inline V4 rgba(std::uint32_t color) {
    return {float(color & 255) / 255, float((color >> 8) & 255) / 255, float((color >> 16) & 255) / 255,
            float(color >> 24) / 255};
}
inline V4 normalize(V4 vector) {
    const auto length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (length > 0)
        for (unsigned i = 0; i < 3; ++i)
            vector[i] /= length;
    else
        vector = {0, 0, 1, 0};
    return vector;
}
inline float dot(const V4 &a, const V4 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

class Lighting {
    struct Light {
        std::uint32_t type{}, kind{};
        V4 position{};  // light position, or the normalized direction of a directional light
        V4 half{};      // a directional light's half vector
        V4 axis{};      // a spot light's normalized axis
        std::array<float, 3> attenuation{};
        float spot_cutoff{}, spot_exponent{};
        V4 ambient{}, diffuse{}, specular{};
    };
    std::array<Light, 4> lights_{};
    unsigned count_{};
    const std::array<float, 16> *world_{};
    V4 material_ambient_{}, material_diffuse_{}, material_specular_{}, global_{}, emissive_{};
    bool vertex_ambient_{}, vertex_diffuse_{}, vertex_specular_{}, reverse_normals_{};
    float specular_power_{}, ambient_alpha_{};
    std::uint32_t mode_{};

public:
    explicit Lighting(const DrawCall &call) : world_(&call.world) {
        const auto &l = call.lighting;
        vertex_ambient_ = call.has_vertex_color && (l.material_update & 1u);
        vertex_diffuse_ = call.has_vertex_color && (l.material_update & 2u);
        vertex_specular_ = call.has_vertex_color && (l.material_update & 4u);
        material_ambient_ = rgba(call.material_color);
        material_diffuse_ = rgba(l.material_diffuse);
        material_specular_ = rgba(l.material_specular);
        global_ = rgba(l.ambient_color);
        emissive_ = rgba(l.material_emissive);
        reverse_normals_ = l.reverse_normals;
        specular_power_ = l.specular_power;
        ambient_alpha_ = float(l.ambient_alpha);
        mode_ = l.mode;
        for (const auto &source : l.lights) {
            if (!source.enabled)
                continue;
            auto &light = lights_[count_++];
            light.type = source.type;
            light.kind = source.kind;
            light.position = {source.position[0], source.position[1], source.position[2], 0};
            if (light.type == 0) {
                light.position = normalize(light.position);
                light.half = light.position;
                light.half[2] += 1;
                light.half = normalize(light.half);
            }
            if (light.type == 2)
                light.axis = normalize({source.direction[0], source.direction[1], source.direction[2], 0});
            light.attenuation = source.attenuation;
            light.spot_cutoff = source.spot_cutoff;
            light.spot_exponent = source.spot_exponent;
            light.ambient = rgba(source.ambient);
            light.diffuse = rgba(source.diffuse);
            light.specular = rgba(source.specular);
        }
    }
    void apply(const Vertex &v, const V4 &world, V4 &color, V4 &extra) const {
        const auto vertex = rgba(v.color);
        const auto &ambient = vertex_ambient_ ? vertex : material_ambient_;
        const auto &diffuse = vertex_diffuse_ ? vertex : material_diffuse_;
        const auto &specular = vertex_specular_ ? vertex : material_specular_;
        auto normal = normalize(multiply(*world_, {v.normal[0], v.normal[1], v.normal[2], 0}));
        if (reverse_normals_)
            for (unsigned i = 0; i < 3; ++i)
                normal[i] = -normal[i];
        V4 sum{}, shine{};
        for (unsigned i = 0; i < 3; ++i)
            sum[i] = emissive_[i] + global_[i] * ambient[i];
        for (unsigned index = 0; index < count_; ++index) {
            const auto &light = lights_[index];
            V4 direction = light.position;
            float attenuation = 1;
            if (light.type != 0) {
                for (unsigned i = 0; i < 3; ++i)
                    direction[i] -= world[i];
                const auto distance = std::sqrt(dot(direction, direction));
                const auto &k = light.attenuation;
                attenuation = std::clamp(
                    1.0f / std::max(k[0] + k[1] * distance + k[2] * distance * distance, 1e-20f), 0.0f, 1.0f);
                direction = normalize(direction);
            }
            if (light.type == 2) {
                const auto angle = -dot(light.axis, direction);
                attenuation *=
                    angle >= light.spot_cutoff ? std::pow(std::max(angle, 0.0f), light.spot_exponent) : 0;
            }
            const auto ndotl = dot(normal, direction);
            auto lambert = std::max(ndotl, 0.0f);
            if (light.kind == 2)
                lambert = std::pow(lambert, specular_power_);
            float highlight = 0;
            if (light.kind == 1 && ndotl >= 0) {
                V4 half = light.half;
                if (light.type != 0) {
                    half = direction;
                    half[2] += 1;
                    half = normalize(half);
                }
                highlight = std::pow(std::max(dot(normal, half), 0.0f), specular_power_);
            }
            for (unsigned i = 0; i < 3; ++i) {
                sum[i] += (light.ambient[i] * ambient[i] + light.diffuse[i] * diffuse[i] * lambert) * attenuation;
                shine[i] += light.specular[i] * specular[i] * highlight * attenuation;
            }
        }
        for (unsigned i = 0; i < 3; ++i) {
            color[i] = std::clamp(sum[i] + (mode_ == 1 ? 0 : shine[i]), 0.0f, 1.0f);
            extra[i] = mode_ == 1 ? std::clamp(shine[i], 0.0f, 1.0f) : 0;
        }
        color[3] = ambient[3] * ambient_alpha_ / 255.0f;
    }
};
}
