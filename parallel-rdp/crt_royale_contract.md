# crt-royale-xm29plus port — generated-artifact & binding contract (Stream A.0)

Frozen interface every stream codes against. Source of truth for the design is the
plan: `C:\Users\CHW\.claude\plans\port-the-crt-royale-xm29plus-shader-groovy-wand.md`.

## Files

| File | Owner stream | Kind |
|------|--------------|------|
| `parallel-rdp/crt_royale_layout.hpp` | A.0 | pure size math (Vulkan-free) |
| `parallel-rdp/crt_royale.hpp` | A.0 | pipeline interface |
| `parallel-rdp/crt_royale.cpp` | D.0 | pipeline implementation |
| `parallel-rdp/spirv_crt_royale.hpp` | B.Z | generated SPIR-V arrays |
| `parallel-rdp/crt_royale_textures.hpp` | C.0 | generated texture arrays |
| `data/shader/crt-royale/*.{vert,frag,inc}` | B.0/B.1-* | GLSL sources |
| `data/shader/crt-royale/output.sh` | B.0 | glslc → SPIR-V header |
| `data/shader/crt-royale/embed_textures.py` | C.0 | PNG → RGBA8 header |

## Pass order (11 passes; 10 offscreen + 1 final)

Index matches `kPassScaleRules` in `crt_royale_layout.hpp`.

| idx | short name | offscreen? |
|----:|------------|------------|
| 0 | `lut` | yes |
| 1 | `linearize` | yes |
| 2 | `scanlines_vert` | yes |
| 3 | `bloom_approx` | yes |
| 4 | `blur_vert` | yes |
| 5 | `halation` | yes |
| 6 | `masked_scanlines` | yes |
| 7 | `brightpass` | yes |
| 8 | `bloom_vert` | yes |
| 9 | `bloom_reconstitute` | yes |
| 10 | `geometry` | no (drawn to swapchain by caller) |

## Generated SPIR-V symbols (`spirv_crt_royale.hpp`, B.Z)

For each pass index `NN` (zero-padded) and short name:

```
static const uint32_t royale_<NN>_<name>_vert_spirv[];
static const uint32_t royale_<NN>_<name>_frag_spirv[];
```

e.g. `royale_06_masked_scanlines_frag_spirv`. Sizes via `sizeof(...)`.

## Generated texture symbols (`crt_royale_textures.hpp`, C.0)

RGBA8, linear data (no color management), row-major, tightly packed:

```
static const uint8_t  royale_color_lut_data[];      // 1024 x 32  (NEC_XM29plus_capture)
static const uint32_t royale_color_lut_width  = 1024;
static const uint32_t royale_color_lut_height = 32;

static const uint8_t  royale_slot_mask_large_data[];  // 512 x 512 (slot mask, mipmapped)
static const uint32_t royale_slot_mask_large_width  = 512;
static const uint32_t royale_slot_mask_large_height = 512;
```

## Shader binding model (B.1-*, must match D.0's ResourceLayout / set_texture calls)

Per pass, descriptor set 0:

- binding 0: UBO `Params` (std140), bound to BOTH vertex and fragment stages.
- bindings 1..N: `sampler2D` inputs, in the order listed in the pass's
  `InputBinding[]` table in `crt_royale.cpp`.

`Params` carries only the size/frame uniforms a pass actually reads:

```glsl
layout(std140, set = 0, binding = 0) uniform Params {
    vec4  SourceSize;     // (w, h, 1/w, 1/h) of this pass's input
    vec4  OriginalSize;   // raw RDP scanout size (passes that sample OrigInput)
    vec4  OutputSize;     // (w, h, 1/w, 1/h) of this pass's render target
    uint  FrameCount;     // interlace bob; cast to float in-shader
} params;
```

Unused fields may be omitted per pass as long as the C++ side fills what the
shader declares. No `global`/MVP UBO (the fullscreen triangle needs no matrix).

## Gamma contract

- sRGB-format FBOs (`VK_FORMAT_R8G8B8A8_SRGB`) do the linear<->sRGB conversion in
  hardware. For those passes, `gamma-management.h` must be configured so
  `encode_output` is a passthrough and `tex2D_linearize` does not re-decode.
- The only manual-gamma boundaries are UNORM: the LUT pass (0) output and the
  final pass (10) writing to the UNORM swapchain (`encode_output` active there).

## Baked parameters

All 47 `.slangp` params are compile-time constants in the shaders; `RUNTIME_*`
options are OFF. Deviation from the preset: `geom_aspect_ratio` is set to N64 4:3
(not the preset's SNES 432:329). `geom_mode_runtime = 0` (flat, no curvature).
