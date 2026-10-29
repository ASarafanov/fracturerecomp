// fracture - graphics API choice (Direct3D 12 or Vulkan) and the GPU lists
// the settings screen offers.

#include "fracture_graphics.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/graphics.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <dxgi.h>
#endif

REXCVAR_DEFINE_STRING(fracture_graphics_api, "auto", "Fracture",
                      "Graphics API: auto (Vulkan under Proton / Wine, Direct3D 12 on "
                      "Windows), d3d12 or vulkan")
    .allowed({"auto", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fracture::graphics {
namespace {

std::string_view g_active_api = "d3d12";

#if defined(_WIN32)
bool RunningUnderWine() {
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version");
}
#else
bool RunningUnderWine() {
  return false;
}
#endif

// Just enough of the Vulkan API to list physical devices through the system
// loader, so the exe needs no Vulkan headers or import library (the prebuilt
// SDK ships neither).
namespace vk {
using Result = int32_t;
using Instance = void*;
using PhysicalDevice = void*;
using VoidFunction = void (*)();
using GetInstanceProcAddr = VoidFunction (*)(Instance, const char*);

struct ApplicationInfo {
  int32_t type = 0;  // VK_STRUCTURE_TYPE_APPLICATION_INFO
  const void* next = nullptr;
  const char* application_name = nullptr;
  uint32_t application_version = 0;
  const char* engine_name = nullptr;
  uint32_t engine_version = 0;
  uint32_t api_version = (1u << 22) | (1u << 12);  // 1.1
};

struct InstanceCreateInfo {
  int32_t type = 1;  // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
  const void* next = nullptr;
  uint32_t flags = 0;
  const ApplicationInfo* application_info = nullptr;
  uint32_t layer_count = 0;
  const char* const* layers = nullptr;
  uint32_t extension_count = 0;
  const char* const* extensions = nullptr;
};

using CreateInstance = Result (*)(const InstanceCreateInfo*, const void*, Instance*);
using DestroyInstance = void (*)(Instance, const void*);
using EnumeratePhysicalDevices = Result (*)(Instance, uint32_t*, PhysicalDevice*);
// VkPhysicalDeviceProperties is 824 bytes; only its stable prefix is read.
using GetPhysicalDeviceProperties = void (*)(PhysicalDevice, void*);
constexpr size_t kPropertiesSize = 824;
constexpr size_t kDeviceTypeOffset = 16;
constexpr size_t kDeviceNameOffset = 20;
constexpr int32_t kDeviceTypeCpu = 4;
}  // namespace vk

std::vector<Gpu> EnumerateVulkanDevices() {
  std::vector<Gpu> devices;
#if defined(_WIN32)
  const HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
  if (!loader) {
    return devices;
  }
  const auto get_proc =
      reinterpret_cast<vk::GetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
  const auto create =
      get_proc ? reinterpret_cast<vk::CreateInstance>(get_proc(nullptr, "vkCreateInstance"))
               : nullptr;
  vk::Instance instance = nullptr;
  vk::ApplicationInfo app;
  app.application_name = "fracture";
  vk::InstanceCreateInfo info;
  info.application_info = &app;
  if (!create || create(&info, nullptr, &instance) != 0 || !instance) {
    FreeLibrary(loader);
    return devices;
  }
  const auto destroy =
      reinterpret_cast<vk::DestroyInstance>(get_proc(instance, "vkDestroyInstance"));
  const auto enumerate = reinterpret_cast<vk::EnumeratePhysicalDevices>(
      get_proc(instance, "vkEnumeratePhysicalDevices"));
  const auto properties = reinterpret_cast<vk::GetPhysicalDeviceProperties>(
      get_proc(instance, "vkGetPhysicalDeviceProperties"));
  uint32_t count = 0;
  if (enumerate && properties && enumerate(instance, &count, nullptr) == 0 && count) {
    std::vector<vk::PhysicalDevice> handles(count);
    if (enumerate(instance, &count, handles.data()) >= 0) {
      for (uint32_t i = 0; i < count; ++i) {
        alignas(8) unsigned char props[vk::kPropertiesSize] = {};
        properties(handles[i], props);
        int32_t type = 0;
        std::memcpy(&type, props + vk::kDeviceTypeOffset, sizeof(type));
        if (type != vk::kDeviceTypeCpu) {
          devices.push_back({int(i), reinterpret_cast<const char*>(props + vk::kDeviceNameOffset)});
        }
      }
    }
  }
  if (destroy) {
    destroy(instance, nullptr);
  }
  FreeLibrary(loader);
#endif
  return devices;
}

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> LoadPlugin(std::string_view plugin) {
  std::string api = REXCVAR_GET(fracture_graphics_api);
  if (api == "auto") {
    api = RunningUnderWine() ? "vulkan" : "d3d12";
  }
  std::unique_ptr<rex::system::IGraphicsSystem> graphics;
  if (api == "vulkan") {
    if (VulkanDevices().empty()) {
      REXLOG_WARN("Graphics: no Vulkan driver or GPU found, using Direct3D 12");
    } else {
      graphics = rex::system::LoadGpuPlugin(plugin, "vulkan");
      if (!graphics) {
        REXLOG_WARN("Graphics: the GPU plugin has no Vulkan backend, using Direct3D 12");
      }
    }
  }
  if (graphics) {
    g_active_api = "vulkan";
  } else {
    graphics = rex::system::LoadGpuPlugin(plugin, "d3d12");
    g_active_api = "d3d12";
  }
  if (graphics) {
    REXLOG_INFO("Graphics: {} ({} requested)", g_active_api,
                REXCVAR_GET(fracture_graphics_api));
  }
  return graphics;
}

std::string_view ActiveApi() {
  return g_active_api;
}

bool PluginHasVulkan() {
  return rex::cvar::GetFlagInfo("render_target_path_vulkan") != nullptr;
}

// On hybrid systems DXGI can list one GPU twice (different LUIDs, only the
// first with the displays attached); only the first of each name and memory
// size is kept, which is also the one automatic selection picks.
std::vector<Gpu> D3D12Adapters() {
  std::vector<Gpu> adapters;
#if defined(_WIN32)
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return adapters;
  }
  std::vector<std::pair<std::string, SIZE_T>> seen;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      char name[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr,
                          nullptr);
      std::pair<std::string, SIZE_T> key{name, desc.DedicatedVideoMemory};
      if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
        seen.push_back(key);
        adapters.push_back({int(i), name});
      }
    }
    adapter->Release();
  }
  factory->Release();
#endif
  return adapters;
}

const std::vector<Gpu>& VulkanDevices() {
  static const std::vector<Gpu> devices = EnumerateVulkanDevices();
  return devices;
}

}  // namespace fracture::graphics
