// Pure (Vulkan-free) framebuffer-size math for the crt-royale-xm29plus pipeline.
//
// This header has no Vulkan/Granite dependency on purpose: the per-pass size
// logic is the off-by-one-prone part of the port, so it lives in one place that
// can be reasoned about (and asserted) without a GPU.  See the plan at
// C:\Users\CHW\.claude\plans\port-the-crt-royale-xm29plus-shader-groovy-wand.md
//
// Stream A.0 (contract). Owned here; consumed by crt_royale.cpp.

#ifndef CRT_ROYALE_LAYOUT_HPP
#define CRT_ROYALE_LAYOUT_HPP

#include <array>
#include <cassert>
#include <cstdint>

namespace crt_royale {

// How one output dimension of a pass is derived.
enum class ScaleKind {
    Source,    // multiply this pass's input dimension (the previous pass output)
    Viewport,  // multiply the displayed-image (calculate_viewport) dimension
    Absolute,  // a fixed pixel count, independent of input and viewport
};

struct ScaleRule {
    ScaleKind kind_x;
    ScaleKind kind_y;
    float scale_x;
    float scale_y;
};

struct Extent {
    uint32_t width;
    uint32_t height;
};

// The offscreen passes (0..9). The final geometry/AA pass (10) always renders
// into the viewport rect and is composited by the caller in the swapchain render
// pass, so it is not sized here.
constexpr int kOffscreenPassCount = 10;

// Per-pass output-size rules, in execution order. The two manual mask-resize
// passes are dropped (mask_sample_mode=1 samples the large mask LUT directly), so
// every remaining rule is a 1.0 source/viewport copy except the absolute 320x240
// bloom-approx pass.  Keep this table in lockstep with the pass table in
// crt_royale.cpp and the plan's pass list.
inline constexpr std::array<ScaleRule, kOffscreenPassCount> kPassScaleRules = {{
    /* 0 LUT                  */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
    /* 1 linearize            */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
    /* 2 vertical-scanlines   */ {ScaleKind::Source,   ScaleKind::Viewport, 1.0f, 1.0f},
    /* 3 bloom-approx         */ {ScaleKind::Absolute, ScaleKind::Absolute, 320.0f, 240.0f},
    /* 4 blur-vertical        */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
    /* 5 halation blur-horiz  */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
    /* 6 masked-scanlines     */ {ScaleKind::Viewport, ScaleKind::Viewport, 1.0f, 1.0f},
    /* 7 brightpass           */ {ScaleKind::Viewport, ScaleKind::Viewport, 1.0f, 1.0f},
    /* 8 bloom-vertical       */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
    /* 9 bloom-h-reconstitute */ {ScaleKind::Source,   ScaleKind::Source,   1.0f, 1.0f},
}};

inline uint32_t apply_scale(ScaleKind kind, float scale, uint32_t input_dim,
                            uint32_t viewport_dim) {
    switch (kind) {
    case ScaleKind::Source:
        return static_cast<uint32_t>(static_cast<float>(input_dim) * scale);
    case ScaleKind::Viewport:
        return static_cast<uint32_t>(static_cast<float>(viewport_dim) * scale);
    case ScaleKind::Absolute:
        return static_cast<uint32_t>(scale);
    }
    return 0;  // trapped: unreachable; a new ScaleKind without a case fails loudly here
}

// Compute every offscreen pass's output extent. Pass 0's input is `source` (the
// raw RDP scanout); each later pass's input is the previous pass's output.
inline std::array<Extent, kOffscreenPassCount> compute_layout(Extent source,
                                                              Extent viewport) {
    std::array<Extent, kOffscreenPassCount> sizes{};
    Extent input = source;
    for (int pass = 0; pass < kOffscreenPassCount; ++pass) {
        const ScaleRule &rule = kPassScaleRules[pass];
        Extent out;
        out.width = apply_scale(rule.kind_x, rule.scale_x, input.width, viewport.width);
        out.height = apply_scale(rule.kind_y, rule.scale_y, input.height, viewport.height);
        // Guard against zero-size framebuffers (degenerate viewport/source).
        if (out.width == 0) out.width = 1;
        if (out.height == 0) out.height = 1;
        sizes[pass] = out;
        input = out;
    }
    return sizes;
}

// Debug-only self-checks over known boundary vectors. No side effects; compiled
// out of release builds. Call once at pipeline construction (be offensive in dev).
inline void assert_layout_invariants() {
#ifndef NDEBUG
    // N64 NTSC scanout at 1x, inside a 4:3 viewport area of a 1440p window.
    const Extent source{640, 480};
    const Extent viewport{1920, 1440};
    const auto sizes = compute_layout(source, viewport);

    // bloom-approx is always exactly 320x240, independent of source/viewport.
    assert(sizes[3].width == 320 && sizes[3].height == 240);
    // vertical-scanlines: x copied from source, y stretched to the viewport.
    assert(sizes[2].width == source.width && sizes[2].height == viewport.height);
    // source-relative blur after bloom-approx inherits 320x240.
    assert(sizes[4].width == 320 && sizes[4].height == 240);
    assert(sizes[5].width == 320 && sizes[5].height == 240);
    // viewport-scale passes match the displayed-image rect.
    assert(sizes[6].width == viewport.width && sizes[6].height == viewport.height);
    assert(sizes[7].width == viewport.width && sizes[7].height == viewport.height);
    // bloom passes after the viewport-size masked/brightpass inherit the viewport.
    assert(sizes[8].width == viewport.width && sizes[8].height == viewport.height);
    assert(sizes[9].width == viewport.width && sizes[9].height == viewport.height);

    // Absolute pass ignores both inputs entirely.
    const auto alt = compute_layout(Extent{320, 240}, Extent{2560, 1440});
    assert(alt[3].width == 320 && alt[3].height == 240);
#endif
}

}  // namespace crt_royale

#endif  // CRT_ROYALE_LAYOUT_HPP
