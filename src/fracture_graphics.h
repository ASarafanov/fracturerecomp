// fracture - graphics API choice (Direct3D 12 or Vulkan) and the GPU lists
// the settings screen offers.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rex::system {
class IGraphicsSystem;
}

namespace fracture::graphics {

// Loads the GPU plugin with the API the fracture_graphics_api cvar asks for.
// "auto" means Vulkan when running under Proton / Wine (Steam Deck, Linux)
// and Direct3D 12 on Windows. Vulkan is only used when the plugin has it and
// a Vulkan driver lists a GPU; otherwise this falls back to Direct3D 12, so
// a bad choice cannot keep the game from starting. Null if nothing loaded
// (the SDK then tries itself and reports the error).
std::unique_ptr<rex::system::IGraphicsSystem> LoadPlugin(std::string_view plugin);

// The API LoadPlugin ended up with: "d3d12" or "vulkan".
std::string_view ActiveApi();

// Whether the loaded GPU plugin was built with Vulkan (its cvars exist).
bool PluginHasVulkan();

struct Gpu {
  int index;  // as the d3d12_adapter / vulkan_device cvar counts
  std::string name;
};

// Hardware adapters in IDXGIFactory1::EnumAdapters1 order (d3d12_adapter).
std::vector<Gpu> D3D12Adapters();

// Physical devices in vkEnumeratePhysicalDevices order (vulkan_device),
// CPU implementations skipped. Empty without a Vulkan driver. Cached: the
// first call creates a Vulkan instance.
const std::vector<Gpu>& VulkanDevices();

}  // namespace fracture::graphics
