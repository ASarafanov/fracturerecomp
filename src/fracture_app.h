// fracture - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/window.h>

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include "fracture_settings.h"

REXCVAR_DECLARE(std::string, fracture_dump_image);
REXCVAR_DECLARE(std::string, fracture_dump_cvars);

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

  // The SDK reads game_data_root before it loads fracture.toml, so a value
  // set there (tools/setup.py writes one) would be ignored. Pick it up here,
  // after the config file has been applied.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    rex::PathConfig paths = defaults;
    if (paths.game_data_root.empty()) {
      paths.game_data_root = REXCVAR_GET(game_data_root);
    }
    return paths;
  }

  // A restarted process must not start while the old one still holds the
  // log files, so wait here, before logging is initialized.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    fracture::settings::WaitForPreviousProcess();
    base_config_path_ = paths.config_path;
  }

  // fracture.toml has just been applied; settings.toml goes on top of it,
  // before the window and the GPU plugin read their cvars.
  void OnPostInitLogging() override { fracture::settings::Load(base_config_path_); }

  // Debug aid: --fracture_dump_cvars=<file> lists every cvar. The GPU plugin
  // is loaded by now, so its cvars are included.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    const std::string path = REXCVAR_GET(fracture_dump_cvars);
    if (!path.empty() && !fracture::settings::DumpCvars(path)) {
      REXLOG_ERROR("Failed to write cvar dump to {}", path);
    }
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

 private:
  // Starts a fresh fracture.exe (it waits for this one to exit) and closes
  // this one, so settings that need a restart take effect.
  void RestartGame() {
    if (!fracture::settings::StartRestartedProcess()) {
      return;
    }
    app_context().CallInUIThreadDeferred([this]() {
      if (window()) {
        window()->RequestClose();
      }
    });
  }

  // fracture.toml, as the SDK resolved it.
  std::filesystem::path base_config_path_;
};
