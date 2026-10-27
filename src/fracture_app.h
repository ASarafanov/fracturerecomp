// fracture - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>

#include <cstdint>
#include <fstream>
#include <string>

REXCVAR_DECLARE(std::string, fracture_dump_image);

class FractureApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  // Debug aid: write the decrypted, decompressed guest image to the file named
  // by --fracture_dump_image before any game code runs. The dump is retail
  // code and data, so keep it out of the repository.
  void OnPostLoadXexImage() override {
    const std::string path = REXCVAR_GET(fracture_dump_image);
    if (path.empty()) {
      return;
    }
    constexpr uint32_t kImageBase = 0x82000000;
    constexpr uint32_t kImageEnd = 0x83710000;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(runtime()->virtual_membase() +
                                            kImageBase),
              kImageEnd - kImageBase);
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FractureApp>(new FractureApp(ctx, "fracture",
        PPCImageConfig));
  }

  // Fracture has no native renderer, so default to the emulated Xenos GPU
  // unless the gpu_plugin cvar picks something else.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
