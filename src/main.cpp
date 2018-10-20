#include <cstdio>

#include "renderer.h"

using namespace prism;

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

int main() {
    RenderSettings settings;
    settings.width = 640;
    settings.height = 360;
    settings.ssaa = 2;

    Scene scene;
    addMaterialSpheres(scene);
    // Sunny 16: the sun delivers ~110,000 lux on a clear day.
    scene.sun = DirectionalLight{normalize(float3{0.6f, -0.7f, -0.35f}), {1.0f, 0.96f, 0.9f}, 110000.0f, true};

    // Clear blue sky, fed to the renderer as an image based light.
    SkyParams sky;
    sky.sunDirection = normalize(float3{-0.6f, 0.7f, 0.35f});
    scene.environment = std::make_unique<Environment>(sky);

    Camera camera;
    camera.position = {0.0f, 3.4f, 7.6f};
    camera.target = {0.0f, 0.1f, -0.2f};
    camera.fovY = radians(36.0f);
    camera.aperture = 16.0f;
    camera.shutterSpeed = 1.0f / 125.0f;
    camera.sensitivity = 100.0f;

    Renderer renderer(settings);
    Image image = renderer.render(scene, camera);
    const RenderStats& s = renderer.stats();

    std::printf("prism: %dx%d (%dx SSAA), EV100 %.2f\n", settings.width, settings.height, settings.ssaa,
                camera.ev100());
    std::printf("  geometry pass  %7.1f ms  (%zu triangles)\n", s.geometryMs, s.triangles);
    std::printf("  shading pass   %7.1f ms\n", s.shadingMs);
    std::printf("  resolve        %7.1f ms\n", s.resolveMs);

    if (!writePNG("prism.png", image)) {
        std::fprintf(stderr, "failed to write prism.png\n");
        return 1;
    }
    std::printf("wrote prism.png\n");
    return 0;
}
