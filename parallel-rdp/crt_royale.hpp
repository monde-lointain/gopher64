// crt-royale-xm29plus multi-pass CRT post-processing pipeline for gopher64.
//
// This is a Sprout Class beside the legacy render_frame/rdp_init monster methods:
// all multi-pass framebuffer state and Granite/Vulkan usage are wrapped here so
// the rest of interface.cpp speaks this small interface instead of Granite's.
// The pipeline runs the 10 offscreen royale passes into intermediate FBOs and
// hands the caller the view + program for the final geometry/AA pass, which the
// caller draws into the swapchain render pass (so UI overlays still composite on
// top unchanged).
//
// See the plan:
// C:\Users\CHW\.claude\plans\port-the-crt-royale-xm29plus-shader-groovy-wand.md
//
// Stream A.0 (contract). Header owned here; implementation in crt_royale.cpp.

#ifndef CRT_ROYALE_HPP
#define CRT_ROYALE_HPP

#include <cstdint>
#include <memory>

#include "crt_royale_layout.hpp"

namespace Vulkan {
class Device;
class CommandBuffer;
class ImageView;
class Program;
}  // namespace Vulkan

namespace crt_royale {

// Owns the royale resources (embedded LUT + slot-mask textures, per-pass programs,
// persistent intermediate FBOs, samplers) and runs the chain each frame.
//
// Lifetime contract: construct only after the Vulkan device exists (i.e. after
// wsi init in rdp_init), and destroy (reset the owning unique_ptr) in the RDP
// teardown path BEFORE the device is destroyed -- never via static-destruction
// order. One instance per session.
class CrtRoyalePipeline {
public:
    // Builds programs and uploads textures once. `device` must outlive this object.
    explicit CrtRoyalePipeline(Vulkan::Device &device);
    ~CrtRoyalePipeline();

    CrtRoyalePipeline(const CrtRoyalePipeline &) = delete;
    CrtRoyalePipeline &operator=(const CrtRoyalePipeline &) = delete;

    // Runs the 10 offscreen passes for this frame and returns the view the caller
    // binds as the source for the final geometry/AA pass (the output of pass 9).
    //
    // Preconditions:
    //   - `cmd` is an open command buffer with no render pass currently active
    //     (this method opens/closes its own offscreen render passes).
    //   - `source` is the raw, pre-LUT RDP scanout view; `source_size` is its size.
    //   - `viewport_size` is the calculate_viewport rect (the displayed image),
    //     NOT the full swapchain extent.
    // Intermediate FBOs are recreated only when source_size/viewport_size change.
    const Vulkan::ImageView &run_offscreen(Vulkan::CommandBuffer &cmd,
                                           const Vulkan::ImageView &source,
                                           Extent source_size, Extent viewport_size,
                                           uint32_t frame_count);

    // The final geometry/AA pass program. The caller binds this inside the
    // swapchain render pass, sets the source returned by run_offscreen(), pushes
    // uniforms via push_final_pass_uniforms(), and draws the fullscreen triangle.
    Vulkan::Program *final_pass_program() const;

    // Fills the final pass's size/frame uniforms. Call while the swapchain render
    // pass is active, before drawing the final pass.
    void push_final_pass_uniforms(Vulkan::CommandBuffer &cmd, Extent source_size,
                                  Extent viewport_size, uint32_t frame_count) const;

private:
    struct Impl;  // pimpl: keep Granite types and the pass table out of this header
    std::unique_ptr<Impl> impl_;
};

}  // namespace crt_royale

#endif  // CRT_ROYALE_HPP
