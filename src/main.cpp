#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "renderer.h"

using namespace prism;

namespace {

enum class SceneKind { Day, Night };

// Three rows of spheres (gold, red plastic, aluminium) with roughness increasing
// left to right, sitting on a checkered ground plane.
void addMaterialSpheres(Scene& scene) {
    const Mesh* sphere = scene.addMesh(makeSphere(64, 40));
    const Mesh* ground = scene.addMesh(makePlane(40.0f));

    Material floor;
    floor.baseColor = {0.5f, 0.5f, 0.52f};
    floor.checkerColor = {0.32f, 0.32f, 0.34f};
    floor.checkerSize = 1.0f;
    floor.roughness = 0.75f;
    scene.renderables.push_back({ground, mat4{}, scene.addMaterial(floor), false, 0.0f});

    struct Row {
        float3 color;
        float metallic;
        float z;
    };
    const Row rows[] = {
        {{1.0f, 0.766f, 0.336f}, 1.0f, -1.5f},     // gold
        {{0.75f, 0.06f, 0.05f}, 0.0f, 0.0f},       // red plastic
        {{0.913f, 0.922f, 0.924f}, 1.0f, 1.5f},    // aluminium
    };
    constexpr int kColumns = 7;
    constexpr float kRadius = 0.42f;
    for (const Row& row : rows) {
        for (int i = 0; i < kColumns; ++i) {
            Material m;
            m.baseColor = row.color;
            m.metallic = row.metallic;
            m.roughness = static_cast<float>(i) / (kColumns - 1);
            float3 center{(i - (kColumns - 1) * 0.5f) * 1.0f, kRadius, row.z};
            scene.renderables.push_back(
                {sphere, translation(center) * scaling(kRadius), scene.addMaterial(m), true, kRadius});
        }
    }
}

// Bright afternoon: "sunny 16" exposure, 110,000 lux sun, blue sky IBL.
void buildDay(Scene& scene, Camera& camera) {
    addMaterialSpheres(scene);

    float3 toSun = normalize(float3{-0.6f, 0.7f, 0.35f});
    scene.sun = DirectionalLight{-toSun, {1.0f, 0.96f, 0.9f}, 110000.0f, true};

    SkyParams sky;
    sky.sunDirection = toSun;
    scene.environment = std::make_unique<Environment>(sky);

    camera.aperture = 16.0f;
    camera.shutterSpeed = 1.0f / 125.0f;
    camera.sensitivity = 100.0f;
}

// Night: moonlight, a dim sky and three colored point lights. The camera opens up to
// f/1.4, 1/30s, ISO 1600 — the same scene units, just a different exposure.
void buildNight(Scene& scene, Camera& camera) {
    addMaterialSpheres(scene);

    float3 toMoon = normalize(float3{0.5f, 0.7f, -0.6f});
    scene.sun = DirectionalLight{-toMoon, {0.7f, 0.8f, 1.0f}, 0.4f, true};

    SkyParams sky;
    sky.zenith = {0.05f, 0.08f, 0.2f};
    sky.horizon = {0.2f, 0.14f, 0.3f};
    sky.ground = {0.05f, 0.05f, 0.06f};
    sky.intensity = 1.0f;
    sky.sunDirection = toMoon;
    sky.sunColor = {0.8f, 0.85f, 1.0f};
    sky.sunGlow = 0.3f;
    sky.sunDiskLuminance = 400.0f;
    scene.environment = std::make_unique<Environment>(sky);

    const Mesh* bulb = scene.meshes[0].get();  // reuse the sphere mesh
    struct Lamp {
        float3 position;
        float3 color;
    };
    const Lamp lamps[] = {
        {{-2.0f, 0.9f, -0.75f}, {1.0f, 0.55f, 0.2f}},
        {{0.5f, 0.9f, 0.75f}, {0.2f, 0.7f, 1.0f}},
        {{2.5f, 0.9f, -0.75f}, {1.0f, 0.25f, 0.6f}},
    };
    constexpr float kLumens = 600.0f;
    constexpr float kBulbRadius = 0.06f;
    for (const Lamp& lamp : lamps) {
        scene.pointLights.push_back({lamp.position, lamp.color, kLumens, 8.0f});
        // Emissive bulb whose luminance matches the light: L = I / (pi r^2).
        Material glow;
        glow.baseColor = float3(0.0f);
        glow.emissive = lamp.color * (kLumens / (4.0f * kPi) / (kPi * kBulbRadius * kBulbRadius));
        scene.renderables.push_back({bulb, translation(lamp.position) * scaling(kBulbRadius),
                                     scene.addMaterial(glow), false, 0.0f});
    }

    camera.aperture = 1.4f;
    camera.shutterSpeed = 1.0f / 30.0f;
    camera.sensitivity = 1600.0f;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [--scene day|night] [--width N] [--height N] [--ssaa N] [--shadow N] [-o out.png]\n",
                 argv0);
}

} // namespace

int main(int argc, char** argv) {
    RenderSettings settings;
    SceneKind kind = SceneKind::Day;
    std::string output;

    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                std::exit(1);
            }
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--scene")) {
            std::string s = next();
            if (s == "day") kind = SceneKind::Day;
            else if (s == "night") kind = SceneKind::Night;
            else { usage(argv[0]); return 1; }
        } else if (!std::strcmp(argv[i], "--width")) settings.width = std::atoi(next());
        else if (!std::strcmp(argv[i], "--height")) settings.height = std::atoi(next());
        else if (!std::strcmp(argv[i], "--ssaa")) settings.ssaa = std::atoi(next());
        else if (!std::strcmp(argv[i], "--shadow")) settings.shadowMapSize = std::atoi(next());
        else if (!std::strcmp(argv[i], "-o")) output = next();
        else { usage(argv[0]); return 1; }
    }
    if (settings.width <= 0 || settings.height <= 0 || settings.ssaa <= 0 || settings.shadowMapSize <= 0) {
        usage(argv[0]);
        return 1;
    }
    if (output.empty()) output = kind == SceneKind::Day ? "prism_day.png" : "prism_night.png";

    auto start = std::chrono::steady_clock::now();
    Scene scene;
    Camera camera;
    camera.position = {0.0f, 3.4f, 7.6f};
    camera.target = {0.0f, 0.1f, -0.2f};
    camera.fovY = radians(36.0f);
    if (kind == SceneKind::Day) buildDay(scene, camera);
    else buildNight(scene, camera);
    double setupMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    Renderer renderer(settings);
    Image image = renderer.render(scene, camera);
    const RenderStats& s = renderer.stats();

    std::printf("prism: %dx%d (%dx SSAA), EV100 %.2f\n", settings.width, settings.height, settings.ssaa,
                camera.ev100());
    std::printf("  scene + IBL bake %7.1f ms\n", setupMs);
    std::printf("  shadow pass      %7.1f ms\n", s.shadowMs);
    std::printf("  geometry pass    %7.1f ms  (%zu triangles)\n", s.geometryMs, s.triangles);
    std::printf("  shading pass     %7.1f ms\n", s.shadingMs);
    std::printf("  resolve          %7.1f ms\n", s.resolveMs);

    if (!writePNG(output, image)) {
        std::fprintf(stderr, "failed to write %s\n", output.c_str());
        return 1;
    }
    std::printf("wrote %s\n", output.c_str());
    return 0;
}
