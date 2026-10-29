// fracture - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "fracture_frame_rate.h"
#include "fracture_game.h"
#include "fracture_graphics.h"
#include "fracture_installer.h"
#include "fracture_installer_dialog.h"
#include "fracture_menu_input.h"
#include "fracture_settings.h"
#include "fracture_settings_dialog.h"

REXCVAR_DECLARE(std::string, fracture_dump_image);
REXCVAR_DECLARE(std::string, fracture_dump_cvars);

class FractureApp : public rex::ReXApp {
 public:
  FractureApp(rex::ui::WindowedAppContext& ctx, std::string_view name,
              const rex::PPCImageInfo& image_info, const fracture::installer::Release& release,
              bool release_forced)
      : rex::ReXApp(ctx, name, image_info),
        image_info_(image_info),
        release_(release),
        release_forced_(release_forced) {}

  // Debug aid: write the decrypted, decompressed guest image to the file named
  // by --fracture_dump_image before any game code runs. The dump is retail
  // code and data, so keep it out of the repository.
  void OnPostLoadXexImage() override {
    const std::string path = REXCVAR_GET(fracture_dump_image);
    if (path.empty()) {
      return;
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(runtime()->virtual_membase() +
                                            image_info_.image_base),
              image_info_.image_size);
  }

  // The game code is a DLL per release (fracture_game.h). The SDK takes its
  // image layout in the constructor, so the DLL is picked and loaded here.
  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    namespace game = fracture::game;
    const auto available = game::AvailableReleases();
    fracture::installer::SetAvailableReleases(available);
    bool forced = false;
    const auto* release = game::PickRelease(available, forced);
    std::string error = "The game code (fracture_v1.dll or fracture_v2.dll) is missing next to "
                        "fracture.exe. Unpack the whole download again.";
    std::optional<rex::PPCImageInfo> image_info;
    if (release) {
      image_info = game::Load(*release, error);
    }
    if (!image_info) {
      REXLOG_ERROR("{}", error);
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, error);
      std::exit(EXIT_FAILURE);
    }
    return std::make_unique<FractureApp>(ctx, "fracture", *image_info, *release, forced);
  }

  // Fracture has no native renderer, so default to the emulated Xenos GPU
  // unless the gpu_plugin cvar picks something else. The plugin is loaded
  // here (the SDK skips its own load then) to pick Direct3D 12 or Vulkan.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    if (!config.graphics) {
      config.graphics = fracture::graphics::LoadPlugin(config.gpu_plugin);
    }
  }

  // The SDK reads game_data_root before it loads fracture.toml, so a value
  // set there (tools/setup.py writes one) would be ignored. Pick it up here,
  // after the config files have been applied. It is UTF-8 text; a relative
  // path counts from the exe's folder (Steam starts games in another one).
  //
  // An absolute path is gone once the folder is moved or copied to another
  // machine (a Steam Deck sees it through Proton under another drive). Then
  // look for game\ where the install wizard puts it, next to the exe, or,
  // for an exe in out\build\<preset>\, where setup.py does, the repository
  // root.
  //
  // The game files must be the release whose code was loaded at start; for
  // the other release this process starts the program again with its code.
  // With no usable game files anywhere, show the install wizard and continue
  // once it is done.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    namespace installer = fracture::installer;
    rex::PathConfig paths = defaults;
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    const std::string configured = REXCVAR_GET(game_data_root);
    if (!configured.empty()) {
      paths.game_data_root = fracture::PathFromText(configured);
      if (paths.game_data_root.is_relative()) {
        paths.game_data_root = exe_dir / paths.game_data_root;
      }
    }
    std::vector<std::filesystem::path> candidates = {paths.game_data_root};
    for (const auto& folder : fracture::game::DefaultGameFolders()) {
      candidates.push_back(folder);
    }
    for (const auto& candidate : candidates) {
      if (!installer::HasGameData(candidate)) {
        continue;
      }
      switch (CheckRelease(candidate, /*may_restart=*/!release_forced_)) {
        case ReleaseCheck::kMatches:
          if (candidate != paths.game_data_root) {
            REXLOG_INFO("Game data not found at '{}', using {}",
                        fracture::PathText(paths.game_data_root), fracture::PathText(candidate));
            paths.game_data_root = candidate;
          }
          return paths;
        case ReleaseCheck::kRestarting:
          return std::nullopt;  // the window closes
        case ReleaseCheck::kUnusable:
          break;
      }
    }
    if (!imgui_drawer()) {
      return paths;  // the SDK reports the missing game files
    }

    // Come back to an unfinished install in the configured folder; start
    // with game\ next to the exe otherwise.
    const auto default_dir = exe_dir / "game";
    const auto folder = installer::IsUnfinishedInstall(paths.game_data_root)
                            ? paths.game_data_root
                            : default_dir;
    finalize_paths_ = paths;
    resume_ = std::move(resume);
    installer::InstallerDialog::Callbacks callbacks;
    callbacks.remember = [default_dir](const std::filesystem::path& dir) {
      // game\ next to the exe is found without a setting.
      const bool is_default = dir.lexically_normal() == default_dir.lexically_normal();
      const std::u8string text = dir.generic_u8string();
      fracture::settings::Save(
          {{"game_data_root", is_default ? std::string() : std::string(text.begin(), text.end())}});
    };
    // Both are called from the wizard's draw; it is destroyed afterwards.
    callbacks.play = [this](const std::filesystem::path& dir) {
      app_context().CallInUIThreadDeferred([this, dir] {
        if (!installer_) {
          return;  // Play was pressed twice
        }
        // The wizard takes either release; the other one's code needs a
        // fresh start (game_data_root is saved by now).
        switch (CheckRelease(dir, /*may_restart=*/true)) {
          case ReleaseCheck::kMatches:
            break;
          case ReleaseCheck::kRestarting:
            installer_.reset();
            return;
          case ReleaseCheck::kUnusable:
            installer_.reset();
            rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,
                                      "The game could not be started. See the log in the logs "
                                      "folder next to fracture.exe.");
            CloseWindow();
            return;
        }
        installer_.reset();
        if (window()) {
          window()->SetCursorVisibility(cursor_before_installer_);
        }
        rex::PathConfig paths = finalize_paths_;
        paths.game_data_root = dir;
        REXLOG_INFO("Installer: starting the game from {}", fracture::PathText(dir));
        auto resume = std::move(resume_);
        resume(std::move(paths));
      });
    };
    callbacks.quit = [this] { CloseWindow(); };
    installer_ = std::make_unique<installer::InstallerDialog>(imgui_drawer(), window(),
                                                              settings_fonts_, folder,
                                                              std::move(callbacks));
    if (window()) {
      cursor_before_installer_ = window()->GetCursorVisibility();
      window()->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    }
    return std::nullopt;
  }

  // A restarted process must not start while the old one still holds the
  // log files, so wait here, before logging is initialized.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    fracture::settings::WaitForPreviousProcess();
    base_config_path_ = paths.config_path;
  }

  // fracture.toml has just been applied; settings.toml goes on top of it,
  // before the window and the GPU plugin read their cvars.
  void OnPostInitLogging() override {
    // Create ran before the log file existed.
    REXLOG_INFO("Game code: fracture_{}.dll, Fracture {}", release_.variant, release_.name);
    fracture::settings::Load(base_config_path_);
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    settings_fonts_ = fracture::settings::LoadFonts(atlas);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    // Escape opens the settings screen and, while it is open, goes back one
    // level (rows -> categories -> closed).
    rex::ui::RegisterBind("bind_fracture_settings", "Escape", "Settings screen (open / back)",
                          [this] {
                            if (settings_dialog_) {
                              settings_dialog_->NavigateBack();
                            } else {
                              OpenSettings();
                            }
                          });
    rex::ui::RegisterBind("bind_fracture_settings_alt", "F1", "Settings screen (open / close)",
                          [this] { ToggleSettings(); });

    // Debug aid: --fracture_dump_cvars=<file> lists every cvar. The GPU
    // plugin is loaded and the binds above are registered by now, so their
    // cvars are included.
    const std::string path = REXCVAR_GET(fracture_dump_cvars);
    if (!path.empty() && !fracture::settings::DumpCvars(path)) {
      REXLOG_ERROR("Failed to write cvar dump to {}", path);
    }
  }

  // Both hooks run on guest threads and cache their cvars here, before the
  // guest starts. The menu chord (RB + Start by default) is seen in the
  // game's own controller read; the screen lives on the UI thread.
  void OnPostSetup() override {
    fracture::frame_rate::Init();
    fracture::menu_input::Init([this] {
      app_context().CallInUIThreadDeferred([this] { ToggleSettings(); });
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_fracture_settings");
    rex::ui::UnregisterBind("bind_fracture_settings_alt");
    settings_dialog_.reset();
    installer_.reset();
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;

 private:
  enum class ReleaseCheck { kMatches, kRestarting, kUnusable };

  // Whether the game files in `dir` are the release whose code is loaded.
  // For the other supported release this starts the program again with that
  // release's code (when `may_restart`) and closes this one.
  ReleaseCheck CheckRelease(const std::filesystem::path& dir, bool may_restart) {
    namespace installer = fracture::installer;
    const installer::Release* installed = installer::InstalledRelease(dir);
    if (installed == &release_) {
      return ReleaseCheck::kMatches;
    }
    if (!installed) {
      REXLOG_ERROR("The game files in {} are not a release of Fracture this program supports",
                   fracture::PathText(dir));
      return ReleaseCheck::kUnusable;
    }
    if (!installer::IsAvailable(*installed)) {
      REXLOG_ERROR("The game files in {} are Fracture {}, which this program does not include",
                   fracture::PathText(dir), installed->name);
      return ReleaseCheck::kUnusable;
    }
    if (!may_restart) {
      REXLOG_ERROR("The game files in {} are Fracture {}, but this start was made for {}",
                   fracture::PathText(dir), installed->name, release_.name);
      return ReleaseCheck::kUnusable;
    }
    REXLOG_INFO("The game files in {} are Fracture {}: starting again with its code",
                fracture::PathText(dir), installed->name);
    if (!fracture::settings::StartRestartedProcess(installed->variant)) {
      return ReleaseCheck::kUnusable;
    }
    CloseWindow();
    return ReleaseCheck::kRestarting;
  }

  void CloseWindow() {
    app_context().CallInUIThreadDeferred([this] {
      if (window()) {
        window()->RequestClose();
      }
    });
  }

  void OpenSettings() {
    // Not over the install wizard: the game has not started yet.
    if (settings_dialog_ || installer_ || !imgui_drawer()) {
      return;
    }
    fracture::settings::SettingsDialog::Callbacks callbacks;
    // The screen asks to close from inside its own draw; destroy it after.
    callbacks.close = [this] {
      app_context().CallInUIThreadDeferred([this] { CloseSettings(); });
    };
    callbacks.restart = [this] { RestartGame(); };
    callbacks.quit = [this] { CloseWindow(); };
    settings_dialog_ = std::make_unique<fracture::settings::SettingsDialog>(
        imgui_drawer(), settings_fonts_, settings_state_, std::move(callbacks));
    REXLOG_DEBUG("Settings: screen opened");
    // The game reads an untouched controller from now on, and mouse look lets
    // go of the cursor so the mouse can drive the screen.
    fracture::menu_input::SetMenuOpen(true);
    mouse_look_before_settings_ = rex::input::mnk::IsMouseLookActive();
    rex::input::mnk::SetMouseLookActive(false);
    if (window()) {
      cursor_before_settings_ = window()->GetCursorVisibility();
      window()->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    }
  }

  void CloseSettings() {
    if (!settings_dialog_) {
      return;
    }
    settings_dialog_.reset();
    REXLOG_DEBUG("Settings: screen closed");
    fracture::menu_input::SetMenuOpen(false);
    rex::input::mnk::SetMouseLookActive(mouse_look_before_settings_);
    if (window()) {
      window()->SetCursorVisibility(cursor_before_settings_);
    }
  }

  void ToggleSettings() {
    if (settings_dialog_) {
      CloseSettings();
    } else {
      OpenSettings();
    }
  }

  // Starts a fresh fracture.exe (it waits for this one to exit) and closes
  // this one, so settings that need a restart take effect.
  void RestartGame() {
    if (!fracture::settings::StartRestartedProcess()) {
      return;
    }
    CloseWindow();
  }

  // fracture.toml, as the SDK resolved it.
  std::filesystem::path base_config_path_;

  fracture::settings::Fonts settings_fonts_;
  fracture::settings::State settings_state_;
  std::unique_ptr<fracture::settings::SettingsDialog> settings_dialog_;
  rex::ui::Window::CursorVisibility cursor_before_settings_ =
      rex::ui::Window::CursorVisibility::kVisible;
  bool mouse_look_before_settings_ = true;

  // The install wizard, while no game files are found, and what it needs to
  // continue the start.
  std::unique_ptr<fracture::installer::InstallerDialog> installer_;
  rex::PathConfig finalize_paths_;
  std::function<void(rex::PathConfig)> resume_;
  rex::ui::Window::CursorVisibility cursor_before_installer_ =
      rex::ui::Window::CursorVisibility::kVisible;

  // The release whose code was loaded (fracture_<variant>.dll) and the
  // layout of its image; whether --fracture_game_release chose it.
  rex::PPCImageInfo image_info_;
  const fracture::installer::Release& release_;
  bool release_forced_ = false;
};
