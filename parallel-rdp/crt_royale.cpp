// crt-royale-xm29plus multi-pass CRT post-processing runtime (implementation).
//
// This is the Sprout Class that wraps Granite/Vulkan for the royale chain so the
// legacy interface.cpp speaks the small CrtRoyalePipeline interface instead of
// Granite's. See crt_royale.hpp (interface), crt_royale_layout.hpp (pure size
// math), crt_royale_contract.md (frozen binding/generated-artifact contract), and
// the plan at
// C:\Users\CHW\.claude\plans\port-the-crt-royale-xm29plus-shader-groovy-wand.md
//
// Design notes:
//   - The 11 passes are described declaratively in a single PassDesc table; one
//     execution loop walks it (no per-pass branching).
//   - Granite does NOT auto-track render-target -> sampled hand-offs, so every
//     offscreen pass ends with an explicit image_barrier transitioning its output
//     COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL. Doing the transition
//     once, right after a pass renders, makes that output readable by *any* later
//     pass that aliases it (several outputs are sampled by 2-3 later passes).
//   - Intermediate FBOs are persistent and recreated only when the source or
//     viewport extents change (compute_layout drives their sizes).

#include "crt_royale.hpp"

#include "command_buffer.hpp"
#include "device.hpp"
#include "image.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "shader.hpp"

#include "crt_royale_textures.hpp"
#include "spirv_crt_royale.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace crt_royale {
namespace {

// ---------------------------------------------------------------------------
// Named constants (no bare literals beyond 0/1).
// ---------------------------------------------------------------------------

// sRGB FBOs let hardware do the linear<->sRGB conversion (gamma contract); the
// LUT pass (0) output is the only offscreen UNORM boundary.
constexpr VkFormat kSrgbFboFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat kUnormFboFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Embedded textures are linear RGBA8 (no color management) -> UNORM.
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Descriptor set 0: UBO at binding 0, sampled inputs at binding 1..N.
constexpr unsigned kDescriptorSet = 0;
constexpr unsigned kUniformBinding = 0;
constexpr unsigned kFirstInputBinding = 1;

// kPasses indices 0..9 are offscreen; index 10 is the final (geometry) pass the
// caller draws into the swapchain render pass.
constexpr int kFinalPassIndex = kOffscreenPassCount;     // 10
constexpr int kProgramCount = kOffscreenPassCount + 1;   // 11
constexpr int kLastOffscreenPass = kOffscreenPassCount - 1; // 9

// Dev-only per-pass visualizer: set env CRT_ROYALE_DEBUG_PASS=0..9 to make
// run_offscreen() return that pass's output instead of the last pass, so a wrong
// pass can be bisected on screen. Unset/-1 disables it. Read once (cached).
int debug_visualize_pass() {
    static const int pass = [] {
        const char *env = std::getenv("CRT_ROYALE_DEBUG_PASS");
        return env ? std::atoi(env) : -1;
    }();
    return pass;
}

// ---------------------------------------------------------------------------
// Pass-input description (table-driven; no 11-way branch).
// ---------------------------------------------------------------------------

// Which producer an input binding resolves to. Reserve no "invalid" slot here;
// the resolver traps any value without a case (see resolve_input).
enum class SourceKind {
    PrevPass,           // the immediately preceding pass output (pass 0: raw scanout)
    OrigInput,          // raw, pre-LUT RDP scanout
    OrigLinearized,     // pass 1 output
    VerticalScanlines,  // pass 2 output
    BloomApprox,        // pass 3 output
    HalationBlur,       // pass 5 output
    MaskedScanlines,    // pass 6 output
    Brightpass,         // pass 7 output
    SlotMaskLarge,      // embedded mipmapped slot-mask texture
    ColorLut,           // embedded color LUT texture
};

struct InputBinding {
    SourceKind kind;
    Vulkan::StockSampler sampler;
};

struct PassDesc {
    const uint32_t *vert_spirv;
    size_t vert_size;
    const uint32_t *frag_spirv;
    size_t frag_size;
    VkFormat format;            // offscreen FBO format (UNDEFINED for the final pass)
    bool generate_mips;         // true only for the last offscreen pass (9)
    const InputBinding *inputs; // binding 1..input_count (nullptr for the final pass)
    size_t input_count;
};

// Per-pass uniform buffer, std140, matching crt_royale_contract.md `Params`.
// All four fields are always uploaded in canonical order so one C++ layout
// serves every pass; shaders declare a canonical-order prefix of this block.
struct PassUniforms {
    float source_size[4];   // (w, h, 1/w, 1/h) of this pass's input          @0
    float original_size[4]; // (w, h, 1/w, 1/h) of the raw RDP scanout        @16
    float output_size[4];   // (w, h, 1/w, 1/h) of this pass's render target  @32
    uint32_t frame_count;   // interlace bob; cast to float in-shader         @48
    uint32_t padding[3];    // pad to a 16-byte multiple (std140)             @52
};
static_assert(sizeof(PassUniforms) == 64, "PassUniforms must match the std140 Params block");

// Convenience for the SPIR-V symbol pairs from spirv_crt_royale.hpp.
#define ROYALE_VERT(sym) (sym##_vert_spirv), sizeof(sym##_vert_spirv)
#define ROYALE_FRAG(sym) (sym##_frag_spirv), sizeof(sym##_frag_spirv)
#define ROYALE_COUNT(arr) (arr), (sizeof(arr) / sizeof((arr)[0]))

using SS = Vulkan::StockSampler;

// Ordered input bindings per pass (binding 1..N), derived from the verified
// preset bindings in the plan's pass table.
constexpr InputBinding kInputsLut[] = {
    {SourceKind::PrevPass, SS::LinearClamp}, // raw RDP scanout
    {SourceKind::ColorLut, SS::LinearClamp},
};
constexpr InputBinding kInputsLinearize[] = {
    {SourceKind::PrevPass, SS::NearestClamp}, // LUT output
};
constexpr InputBinding kInputsScanlinesVert[] = {
    {SourceKind::OrigLinearized, SS::LinearClamp},
};
constexpr InputBinding kInputsBloomApprox[] = {
    {SourceKind::VerticalScanlines, SS::LinearClamp},
    {SourceKind::OrigLinearized, SS::LinearClamp},
    {SourceKind::OrigInput, SS::LinearClamp},
};
constexpr InputBinding kInputsBlurVert[] = {
    {SourceKind::BloomApprox, SS::LinearClamp},
};
constexpr InputBinding kInputsHalation[] = {
    {SourceKind::PrevPass, SS::LinearClamp}, // blur-vertical output
};
constexpr InputBinding kInputsMaskedScanlines[] = {
    {SourceKind::VerticalScanlines, SS::LinearClamp},
    {SourceKind::SlotMaskLarge, SS::TrilinearWrap},
    {SourceKind::BloomApprox, SS::LinearClamp},
    {SourceKind::HalationBlur, SS::LinearClamp},
};
constexpr InputBinding kInputsBrightpass[] = {
    {SourceKind::MaskedScanlines, SS::LinearClamp},
    {SourceKind::BloomApprox, SS::LinearClamp},
};
constexpr InputBinding kInputsBloomVert[] = {
    {SourceKind::Brightpass, SS::LinearClamp},
};
constexpr InputBinding kInputsBloomReconstitute[] = {
    {SourceKind::PrevPass, SS::LinearClamp}, // bloom-vertical output
    {SourceKind::HalationBlur, SS::LinearClamp},
    {SourceKind::Brightpass, SS::LinearClamp},
    {SourceKind::MaskedScanlines, SS::LinearClamp},
};

// The full chain. Indices match kPassScaleRules / the contract's pass order.
// The final pass (10) has no offscreen FBO and its single input (the mipmapped
// pass-9 output) is bound by the caller, so its format/inputs are intentionally
// empty here; only its program is built.
constexpr PassDesc kPasses[kProgramCount] = {
    {ROYALE_VERT(royale_00_lut), ROYALE_FRAG(royale_00_lut),
     kUnormFboFormat, false, ROYALE_COUNT(kInputsLut)},
    {ROYALE_VERT(royale_01_linearize), ROYALE_FRAG(royale_01_linearize),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsLinearize)},
    {ROYALE_VERT(royale_02_scanlines_vert), ROYALE_FRAG(royale_02_scanlines_vert),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsScanlinesVert)},
    {ROYALE_VERT(royale_03_bloom_approx), ROYALE_FRAG(royale_03_bloom_approx),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsBloomApprox)},
    {ROYALE_VERT(royale_04_blur_vert), ROYALE_FRAG(royale_04_blur_vert),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsBlurVert)},
    {ROYALE_VERT(royale_05_halation), ROYALE_FRAG(royale_05_halation),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsHalation)},
    {ROYALE_VERT(royale_06_masked_scanlines), ROYALE_FRAG(royale_06_masked_scanlines),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsMaskedScanlines)},
    {ROYALE_VERT(royale_07_brightpass), ROYALE_FRAG(royale_07_brightpass),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsBrightpass)},
    {ROYALE_VERT(royale_08_bloom_vert), ROYALE_FRAG(royale_08_bloom_vert),
     kSrgbFboFormat, false, ROYALE_COUNT(kInputsBloomVert)},
    {ROYALE_VERT(royale_09_bloom_reconstitute), ROYALE_FRAG(royale_09_bloom_reconstitute),
     kSrgbFboFormat, true, ROYALE_COUNT(kInputsBloomReconstitute)},
    {ROYALE_VERT(royale_10_geometry), ROYALE_FRAG(royale_10_geometry),
     VK_FORMAT_UNDEFINED, false, nullptr, 0},
};

// ---------------------------------------------------------------------------
// Small pure helpers.
// ---------------------------------------------------------------------------

// (w, h, 1/w, 1/h); guard the reciprocals against a degenerate zero dimension.
void write_size_vec4(float out[4], Extent extent) {
    const float width = static_cast<float>(extent.width == 0 ? 1u : extent.width);
    const float height = static_cast<float>(extent.height == 0 ? 1u : extent.height);
    out[0] = width;
    out[1] = height;
    out[2] = 1.0f / width;
    out[3] = 1.0f / height;
}

void fill_uniforms(PassUniforms &uniforms, Extent input, Extent original, Extent output,
                   uint32_t frame_count) {
    write_size_vec4(uniforms.source_size, input);
    write_size_vec4(uniforms.original_size, original);
    write_size_vec4(uniforms.output_size, output);
    uniforms.frame_count = frame_count;
    uniforms.padding[0] = 0;
    uniforms.padding[1] = 0;
    uniforms.padding[2] = 0;
}

// A sampled color-attachment FBO. TRANSFER usage lets the last offscreen pass
// build its mip chain via blit. `mipmapped` requests a full mip chain (levels=0
// asks Granite to compute the count).
Vulkan::ImageHandle create_fbo(Vulkan::Device &device, Extent size, VkFormat format,
                               bool mipmapped) {
    Vulkan::ImageCreateInfo info = {};
    info.domain = Vulkan::ImageDomain::Physical;
    info.width = size.width;
    info.height = size.height;
    info.depth = 1;
    info.levels = mipmapped ? 0u : 1u;
    info.format = format;
    info.type = VK_IMAGE_TYPE_2D;
    info.layers = 1;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.misc = 0; // mips are generated mid-frame, not at upload, so no GENERATE_MIPS here
    info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return device.create_image(info, nullptr);
}

Vulkan::ImageHandle upload_texture(Vulkan::Device &device, const uint8_t *data, uint32_t width,
                                   uint32_t height, bool mipmapped) {
    Vulkan::ImageCreateInfo info =
        Vulkan::ImageCreateInfo::immutable_2d_image(width, height, kTextureFormat, mipmapped);
    Vulkan::ImageInitialData initial = {};
    initial.data = data;
    initial.row_length = 0;  // tightly packed
    initial.image_height = 0;
    return device.create_image(info, &initial);
}

// Make a freshly-rendered offscreen output readable by later passes. For the
// last offscreen pass, generate its mip chain first (the final pass samples it
// trilinear), which leaves the whole image in TRANSFER_SRC before the read.
void transition_output_to_sampled(Vulkan::CommandBuffer &cmd, const Vulkan::Image &image,
                                  bool generate_mips) {
    if (generate_mips) {
        cmd.barrier_prepare_generate_mipmap(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, true);
        cmd.generate_mipmap(image);
        cmd.image_barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } else {
        cmd.image_barrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Pimpl: owns textures, programs, persistent FBOs, and the cached layout.
// ---------------------------------------------------------------------------

struct CrtRoyalePipeline::Impl {
    explicit Impl(Vulkan::Device &device);

    // Recreate the offscreen FBOs only when the extents change.
    void ensure_framebuffers(Extent source_size, Extent viewport_size);
    // Run one offscreen pass: render the fullscreen triangle into its FBO and
    // hand the output off to SHADER_READ_ONLY for later passes.
    void run_pass(Vulkan::CommandBuffer &cmd, int pass, const Vulkan::ImageView &source,
                  Extent source_size, uint32_t frame_count);
    // Resolve an input binding to the concrete view it samples.
    const Vulkan::ImageView &resolve_input(SourceKind kind, int pass,
                                           const Vulkan::ImageView &source) const;

    Vulkan::Device &device;

    Vulkan::ImageHandle color_lut;
    Vulkan::ImageHandle slot_mask_large;
    Vulkan::Program *programs[kProgramCount] = {};

    Vulkan::ImageHandle framebuffers[kOffscreenPassCount];
    std::array<Extent, kOffscreenPassCount> sizes = {};
    Extent cached_source = {0, 0};
    Extent cached_viewport = {0, 0};
};

CrtRoyalePipeline::Impl::Impl(Vulkan::Device &device_) : device(device_) {
    // Be offensive in dev: validate the pure size math once at construction.
    assert_layout_invariants();

    color_lut = upload_texture(device, royale_color_lut_data, royale_color_lut_width,
                               royale_color_lut_height, /*mipmapped=*/false);
    slot_mask_large = upload_texture(device, royale_slot_mask_large_data,
                                     royale_slot_mask_large_width,
                                     royale_slot_mask_large_height, /*mipmapped=*/true);

    // This Granite build does NOT reflect a ResourceLayout from the SPIR-V when
    // none is passed -- it builds an empty layout, so the descriptors are never
    // bound and every draw is a no-op (black screen). Hand-build the layout per
    // pass instead: a UBO at binding 0 (read by both stages) plus the sampled
    // inputs at bindings 1..N. Declaring a binding the (DCE'd) SPIR-V doesn't use
    // is harmless; declaring too few is what fails validation.
    for (int pass = 0; pass < kProgramCount; ++pass) {
        const PassDesc &desc = kPasses[pass];
        // The final (geometry) pass samples one caller-bound input at binding 1;
        // offscreen passes sample input_count inputs at bindings 1..N.
        const unsigned sampler_count =
            (pass == kFinalPassIndex) ? 1u : static_cast<unsigned>(desc.input_count);

        Vulkan::ResourceLayout vertex_layout = {};
        Vulkan::ResourceLayout fragment_layout = {};
        vertex_layout.sets[kDescriptorSet].uniform_buffer_mask = 1u << kUniformBinding;
        fragment_layout.sets[kDescriptorSet].uniform_buffer_mask = 1u << kUniformBinding;
        for (unsigned i = 0; i < sampler_count; ++i) {
            fragment_layout.sets[kDescriptorSet].sampled_image_mask |=
                1u << (kFirstInputBinding + i);
        }
        fragment_layout.output_mask = 1u << 0;

        programs[pass] = device.request_program(desc.vert_spirv, desc.vert_size,
                                                 desc.frag_spirv, desc.frag_size,
                                                 &vertex_layout, &fragment_layout);
    }
}

void CrtRoyalePipeline::Impl::ensure_framebuffers(Extent source_size, Extent viewport_size) {
    const bool unchanged = framebuffers[0] && source_size.width == cached_source.width &&
                           source_size.height == cached_source.height &&
                           viewport_size.width == cached_viewport.width &&
                           viewport_size.height == cached_viewport.height;
    if (unchanged) {
        return;
    }

    cached_source = source_size;
    cached_viewport = viewport_size;
    sizes = compute_layout(source_size, viewport_size);
    for (int pass = 0; pass < kOffscreenPassCount; ++pass) {
        const PassDesc &desc = kPasses[pass];
        framebuffers[pass] = create_fbo(device, sizes[pass], desc.format, desc.generate_mips);
    }
}

const Vulkan::ImageView &CrtRoyalePipeline::Impl::resolve_input(
    SourceKind kind, int pass, const Vulkan::ImageView &source) const {
    switch (kind) {
    case SourceKind::PrevPass:
        return pass == 0 ? source : framebuffers[pass - 1]->get_view();
    case SourceKind::OrigInput:
        return source;
    case SourceKind::OrigLinearized:
        return framebuffers[1]->get_view();
    case SourceKind::VerticalScanlines:
        return framebuffers[2]->get_view();
    case SourceKind::BloomApprox:
        return framebuffers[3]->get_view();
    case SourceKind::HalationBlur:
        return framebuffers[5]->get_view();
    case SourceKind::MaskedScanlines:
        return framebuffers[6]->get_view();
    case SourceKind::Brightpass:
        return framebuffers[7]->get_view();
    case SourceKind::SlotMaskLarge:
        return slot_mask_large->get_view();
    case SourceKind::ColorLut:
        return color_lut->get_view();
    }
    // Trapped default: a new SourceKind without a case must fail loudly in dev;
    // fall back to a valid reference in release rather than dereference garbage.
    assert(false && "crt_royale: unhandled SourceKind");
    return source;
}

void CrtRoyalePipeline::Impl::run_pass(Vulkan::CommandBuffer &cmd, int pass,
                                       const Vulkan::ImageView &source, Extent source_size,
                                       uint32_t frame_count) {
    const PassDesc &desc = kPasses[pass];
    const Extent output_size = sizes[pass];

    // Transition the FBO to the attachment layout before rendering. After the
    // previous frame it is in SHADER_READ_ONLY (later passes / the final pass
    // sampled it), but the render pass needs COLOR_ATTACHMENT_OPTIMAL. Use
    // UNDEFINED as the old layout: it is valid from any state and discards the
    // stale contents, which is fine because the fullscreen triangle overwrites
    // every texel. Without this the layouts desync (validation error; black).
    cmd.image_barrier(*framebuffers[pass], VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Render into this pass's FBO. load/clear are left off (DONT_CARE): the
    // fullscreen triangle overwrites every texel.
    Vulkan::RenderPassInfo render_pass = {};
    render_pass.num_color_attachments = 1;
    render_pass.color_attachments[0] = &framebuffers[pass]->get_view();
    render_pass.store_attachments = 1u << 0;
    cmd.begin_render_pass(render_pass);

    cmd.set_program(programs[pass]);
    cmd.set_opaque_state();
    cmd.set_depth_test(false, false);
    cmd.set_cull_mode(VK_CULL_MODE_NONE);

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(output_size.width);
    viewport.height = static_cast<float>(output_size.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.set_viewport(viewport);
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {output_size.width, output_size.height};
    cmd.set_scissor(scissor);

    // Per-pass uniforms at set 0 / binding 0 (reflected as visible to both
    // stages). "Source" is always the previous pass output (pass 0: raw scanout).
    const Extent input_size = (pass == 0) ? source_size : sizes[pass - 1];
    PassUniforms *uniforms =
        cmd.allocate_typed_constant_data<PassUniforms>(kDescriptorSet, kUniformBinding, 1);
    fill_uniforms(*uniforms, input_size, source_size, output_size, frame_count);

    // Sampled inputs at set 0 / binding 1..N, in table order.
    for (size_t input = 0; input < desc.input_count; ++input) {
        const InputBinding &binding = desc.inputs[input];
        cmd.set_texture(kDescriptorSet, kFirstInputBinding + static_cast<unsigned>(input),
                        resolve_input(binding.kind, pass, source), binding.sampler);
    }

    cmd.draw(3);
    cmd.end_render_pass();

    transition_output_to_sampled(cmd, *framebuffers[pass], desc.generate_mips);
}

// ---------------------------------------------------------------------------
// Public interface (delegates to Impl).
// ---------------------------------------------------------------------------

CrtRoyalePipeline::CrtRoyalePipeline(Vulkan::Device &device)
    : impl_(std::make_unique<Impl>(device)) {}

CrtRoyalePipeline::~CrtRoyalePipeline() = default;

const Vulkan::ImageView &CrtRoyalePipeline::run_offscreen(Vulkan::CommandBuffer &cmd,
                                                          const Vulkan::ImageView &source,
                                                          Extent source_size, Extent viewport_size,
                                                          uint32_t frame_count) {
    impl_->ensure_framebuffers(source_size, viewport_size);
    for (int pass = 0; pass < kOffscreenPassCount; ++pass) {
        impl_->run_pass(cmd, pass, source, source_size, frame_count);
    }

    const int debug_pass = debug_visualize_pass();
    if (debug_pass >= 0 && debug_pass < kOffscreenPassCount) {
        return impl_->framebuffers[debug_pass]->get_view();
    }
    return impl_->framebuffers[kLastOffscreenPass]->get_view();
}

Vulkan::Program *CrtRoyalePipeline::final_pass_program() const {
    return impl_->programs[kFinalPassIndex];
}

void CrtRoyalePipeline::push_final_pass_uniforms(Vulkan::CommandBuffer &cmd, Extent source_size,
                                                 Extent viewport_size,
                                                 uint32_t frame_count) const {
    // The final pass samples the last offscreen output (viewport-sized) and
    // renders into the viewport rect; OriginalSize carries the raw scanout size.
    const Extent final_input = impl_->sizes[kLastOffscreenPass];
    PassUniforms *uniforms =
        cmd.allocate_typed_constant_data<PassUniforms>(kDescriptorSet, kUniformBinding, 1);
    fill_uniforms(*uniforms, final_input, source_size, viewport_size, frame_count);
}

}  // namespace crt_royale
