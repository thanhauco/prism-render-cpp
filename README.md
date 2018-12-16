# prism

A small physically based renderer written from scratch in C++17. It is inspired by the
ideas behind Google's Filament (2018), but it's CPU-only and has no dependencies.

```
make render      # builds build/prism and writes prism_day.png + prism_night.png
```

Or with CMake: `cmake -B build && cmake --build build`.

```
build/prism [--scene day|night] [--width N] [--height N] [--ssaa N] [--shadow N] [-o out.png]
```

## What's inside

| Concept (from Filament) | Here |
|---|---|
| Cook-Torrance BRDF: GGX NDF, height-correlated Smith visibility, Schlick Fresnel, Lambert diffuse | `src/brdf.h` |
| Metallic/roughness material with `reflectance` → f0 remapping | `src/scene.h`, `makePixelParams` |
| Physical camera: aperture / shutter / ISO → EV100 → exposure | `src/camera.h` |
| Physical light units: sun in lux, point lights in lumens with windowed inverse-square falloff | `src/renderer.cpp` |
| IBL à la `cmgen`: SH (3 bands) irradiance + GGX-prefiltered specular levels, split-sum with analytic DFG | `src/environment.cpp` |
| Directional shadow map with normal-offset bias and 5×5 PCF | `src/renderer.cpp` |
| ACES tone mapping, sRGB output | `src/image.h` |

The engine side:

- **Rasterizer** (`src/rasterizer.cpp`): clips against the near plane, bins triangles
  into 32×32 tiles, rasterizes the tiles in parallel and interpolates attributes with
  perspective correction.
- **Deferred shading**: a G-buffer pass followed by a parallel shading pass, with
  supersampling (SSAA) and a box-filter resolve.
- **Analytic sphere ambient occlusion**, which grounds the spheres under IBL.
- **PNG writer**: CRC32, Adler32 and stored deflate blocks, so zlib isn't needed.

## Scenes

- **day**: "sunny 16" exposure (f/16, 1/125 s, ISO 100), 110,000 lux sun, blue-sky IBL.
- **night**: the same scene units at f/1.4, 1/30 s, ISO 1600. It's lit by moonlight
  and three 600 lm point lights. Each bulb's emissive luminance is derived from its
  light (L = I / πr²).

## Ideas for extending it

Shadows for point lights (cube shadow maps), a DFG lookup table with
multiscattering energy compensation, clear-coat and anisotropy material layers,
glTF/OBJ loading, loading HDR environments, and temporal AA.
