// fracture - the install wizard shown on the first start.

#pragma once

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "fracture_installer.h"
#include "fracture_settings_dialog.h"

namespace fracture::installer {

// Fullscreen screen in the style of the settings screen, shown before the
// runtime starts when no game files are found: pick the disc image (or
// drop it on the window) and the install folder, extract with a progress
// bar, then play. Already extracted files can be used instead. Works with
// the mouse, the keyboard and an XInput controller; the file choosers are
// the system's own dialogs.
class InstallerDialog final : public rex::ui::ImGuiDialog, public rex::ui::WindowListener {
 public:
  struct Callbacks {
    // The game files are in this folder; start the game. Called from the
    // dialog's draw, so the owner must destroy the dialog later.
    std::function<void(const std::filesystem::path&)> play;
    // Store the folder the game files are (being) put in, so an interrupted
    // install comes back to it.
    std::function<void(const std::filesystem::path&)> remember;
    std::function<void()> quit;
  };

  InstallerDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                  const settings::Fonts& fonts, std::filesystem::path folder,
                  Callbacks callbacks);
  ~InstallerDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnFileDrop(rex::ui::FileDropEvent& e) override;

 private:
  enum class Screen { kSetup, kInstalling, kDone };
  enum class Action { kBrowseDisc, kChangeFolder, kInstall, kUseFolder, kQuit, kCancel, kPlay };
  enum class Pick { kDisc, kInstallFolder, kGameFolder };
  struct Button {
    Action action;
    ImVec2 min;
    ImVec2 max;
    std::string label;
    bool enabled = true;
    bool primary = false;
  };
  struct Layout;

  std::vector<Button> Buttons(const Layout& layout) const;
  bool Busy() const;
  bool CanInstall() const;
  void Activate(Action action);
  void StartPick(Pick pick);
  void CheckDisc(std::filesystem::path image);
  void CheckFolder(std::filesystem::path dir);
  void SetFolder(std::filesystem::path folder);
  void UpdateSpace();
  void StartInstall();
  void PollJobs();
  void HandleInput(ImGuiIO& io, const std::vector<Button>& buttons);
  void MoveFocus(const std::vector<Button>& buttons, int direction);
  int PadDirection(float delta_time, bool& confirm);
  void DrawScreen(ImGuiIO& io, const Layout& layout, const std::vector<Button>& buttons);

  rex::ui::Window* window_;
  settings::Fonts fonts_;
  Callbacks callbacks_;
  Screen screen_ = Screen::kSetup;
  Action focus_ = Action::kBrowseDisc;
  ImVec2 last_mouse_pos_{-1.0f, -1.0f};

  // Disc image: the chosen file, its check result.
  std::filesystem::path disc_path_;
  std::optional<Disc> disc_;
  std::string disc_error_;
  std::future<std::pair<std::string, Disc>> disc_check_;

  // Install folder as picked, where the files go (InstallTarget of it), and
  // the folder the wizard started with (restored on cancel).
  std::filesystem::path folder_;
  std::filesystem::path target_;
  std::filesystem::path initial_folder_;
  std::optional<uint64_t> free_bytes_;
  uint64_t needed_bytes_ = 0;  // what is not in the target yet

  // Already extracted game files being checked.
  std::filesystem::path game_folder_;
  std::future<std::string> folder_check_;

  // A system file chooser is open.
  Pick pick_ = Pick::kDisc;
  std::future<std::optional<std::filesystem::path>> picker_;

  // Extraction.
  Progress progress_;
  std::string extract_error_;
  std::future<Result> extract_;

  // Shown under the buttons: why the last step failed, or that it was
  // cancelled.
  std::string message_;
  bool message_is_error_ = false;

  // Where the game files ended up (done screen).
  std::filesystem::path game_dir_;
  bool installed_now_ = false;

  // XInput controller.
  bool pad_active_ = false;
  bool pad_seen_ = false;
  uint16_t pad_prev_buttons_ = 0;
  int pad_held_ = 0;
  float pad_repeat_ = 0.0f;
  uint8_t pad_connected_ = 0;  // XInput slots that answered
  float pad_scan_ = 0.0f;      // seconds until empty slots are polled again
};

}  // namespace fracture::installer
