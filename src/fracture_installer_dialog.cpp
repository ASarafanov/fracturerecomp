// fracture - the install wizard shown on the first start.

#include "fracture_installer_dialog.h"

#include "fracture_menu_input.h"
#include "fracture_ui.h"

#include <rex/logging.h>
#include <rex/ui/ui_event.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <shobjidl.h>
#include <xinput.h>
#endif

namespace fracture::installer {
namespace {

using namespace fracture::ui;

// Layout in a 1920x1080 frame, scaled to fit the display and centred.
constexpr float kBaseWidth = 1920.0f;
constexpr float kBaseHeight = 1080.0f;
constexpr float kLeft = 200.0f;
constexpr float kRight = 1720.0f;
constexpr float kTitleY = 96.0f;
constexpr float kIntroY = 200.0f;
constexpr float kDiscY = 330.0f;    // section heading; its field is below
constexpr float kFolderY = 540.0f;
constexpr float kFieldTop = 46.0f;  // from the heading
constexpr float kFieldH = 60.0f;
constexpr float kStatusTop = 120.0f;
constexpr float kSideButtonW = 300.0f;
constexpr float kFieldGap = 30.0f;
constexpr float kMessageY = 744.0f;
constexpr float kActionsY = 840.0f;
constexpr float kActionsH = 64.0f;
constexpr float kFooterY = 940.0f;
constexpr float kLegendY = 1004.0f;
constexpr float kBarY = 420.0f;
constexpr float kBarH = 44.0f;
constexpr float kTitleSize = 52.0f;
constexpr float kHeadingSize = 28.0f;
constexpr float kBodySize = 26.0f;
constexpr float kNoteSize = 23.0f;
constexpr float kSmallSize = 21.0f;

constexpr ImU32 kColBackground = IM_COL32(12, 14, 19, 255);
constexpr ImU32 kColErrorBg = IM_COL32(255, 110, 96, 30);
constexpr ImU32 kColPrimaryIdle = IM_COL32(255, 138, 36, 190);

// The game files on a supported disc (version 1; version 2 is 48 KB less),
// shown until the disc is read.
constexpr uint64_t kTypicalBytes = 4096365604ull;
// Headroom on top of the game files.
constexpr uint64_t kSpareBytes = 64ull << 20;
// Empty XInput slots are slow to poll, so they are only tried this often.
constexpr float kPadScanInterval = 1.0f;

template <typename T>
bool Ready(const std::future<T>& future) {
  return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

#if defined(_WIN32)
// The system's file chooser. Runs on a thread of its own (with its own COM
// apartment), so the wizard keeps drawing behind it; `owner` keeps it in
// front of the game window, also in fullscreen.
std::optional<std::filesystem::path> PickPath(void* owner, bool folder, std::wstring title,
                                              std::filesystem::path start) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  std::optional<std::filesystem::path> result;
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
               (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
    dialog->SetOptions(options);
    dialog->SetTitle(title.c_str());
    if (!folder) {
      const COMDLG_FILTERSPEC types[] = {{L"Xbox 360 disc images (*.iso)", L"*.iso"},
                                         {L"All files (*.*)", L"*.*"}};
      dialog->SetFileTypes(2, types);
    }
    // Open at `start` or its nearest existing parent.
    std::error_code ec;
    while (!start.empty() && !std::filesystem::is_directory(start, ec)) {
      const auto parent = start.parent_path();
      start = parent == start ? std::filesystem::path() : parent;
    }
    IShellItem* item = nullptr;
    if (!start.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
      dialog->SetFolder(item);
      item->Release();
    }
    if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner)))) {
      IShellItem* picked = nullptr;
      if (SUCCEEDED(dialog->GetResult(&picked))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          result = std::filesystem::path(path);
          CoTaskMemFree(path);
        }
        picked->Release();
      }
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) {
    CoUninitialize();
  }
  return result;
}

// The runtime (and with it the SDK's input drivers) does not exist yet, so
// the wizard reads controllers through XInput itself.
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn XInputGetStateProc() {
  static const XInputGetStateFn proc = [] {
    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
      if (HMODULE module = LoadLibraryW(name)) {
        if (auto get = reinterpret_cast<XInputGetStateFn>(
                reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState")))) {
          return get;
        }
      }
    }
    return XInputGetStateFn(nullptr);
  }();
  return proc;
}
#endif

}  // namespace

struct InstallerDialog::Layout {
  float scale;
  ImVec2 origin;

  explicit Layout(ImVec2 display) {
    scale = std::min(display.x / kBaseWidth, display.y / kBaseHeight);
    origin = ImVec2((display.x - kBaseWidth * scale) * 0.5f,
                    (display.y - kBaseHeight * scale) * 0.5f);
  }
  ImVec2 P(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
  float S(float v) const { return v * scale; }
};

InstallerDialog::InstallerDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                                 const settings::Fonts& fonts, std::filesystem::path folder,
                                 Callbacks callbacks)
    : ImGuiDialog(drawer),
      window_(window),
      fonts_(fonts),
      callbacks_(std::move(callbacks)),
      initial_folder_(folder) {
  ImFontAtlas* atlas = GetIO().Fonts;
  ImFont* fallback = atlas->Fonts.empty() ? nullptr : atlas->Fonts[0];
  if (!fonts_.body) {
    fonts_.body = fallback;
  }
  if (!fonts_.strong) {
    fonts_.strong = fonts_.body;
  }
  if (!fonts_.title) {
    fonts_.title = fonts_.strong;
  }
  SetFolder(std::move(folder));
  if (window_) {
    window_->AddListener(this);
  }
  // An install that was interrupted: pick its disc image up again.
  if (std::filesystem::path disc = UnfinishedDisc(target_); !disc.empty()) {
    CheckDisc(std::move(disc));
  }
  REXLOG_INFO("Installer: no game files found, showing the install wizard");
}

InstallerDialog::~InstallerDialog() {
  if (window_) {
    window_->RemoveListener(this);
  }
  // The futures wait for their jobs when destroyed; extraction stops at its
  // next chunk and removes what it wrote.
  progress_.cancel = true;
}

bool InstallerDialog::Busy() const {
  return picker_.valid() || disc_check_.valid() || folder_check_.valid() || extract_.valid();
}

bool InstallerDialog::CanInstall() const {
  return disc_ && !Busy() && !target_.empty() &&
         (!free_bytes_ || *free_bytes_ >= needed_bytes_ + kSpareBytes);
}

std::vector<InstallerDialog::Button> InstallerDialog::Buttons(const Layout& L) const {
  std::vector<Button> buttons;
  const float actions_bottom = kActionsY + kActionsH;
  switch (screen_) {
    case Screen::kSetup: {
      const bool idle = !Busy();
      const float side_x = kRight - kSideButtonW;
      buttons.push_back({Action::kBrowseDisc, L.P(side_x, kDiscY + kFieldTop),
                         L.P(kRight, kDiscY + kFieldTop + kFieldH), "Browse...", idle});
      buttons.push_back({Action::kChangeFolder, L.P(side_x, kFolderY + kFieldTop),
                         L.P(kRight, kFolderY + kFieldTop + kFieldH), "Change...", idle});
      buttons.push_back({Action::kInstall, L.P(kLeft, kActionsY), L.P(kLeft + 360.0f, actions_bottom),
                         "Install", CanInstall(), true});
      buttons.push_back({Action::kUseFolder, L.P(kLeft + 390.0f, kActionsY),
                         L.P(kLeft + 860.0f, actions_bottom), "Use extracted files...", idle});
      buttons.push_back(
          {Action::kQuit, L.P(side_x, kActionsY), L.P(kRight, actions_bottom), "Quit", true});
      break;
    }
    case Screen::kInstalling: {
      const bool cancelling = progress_.cancel.load();
      buttons.push_back({Action::kCancel, L.P(kLeft, kActionsY), L.P(kLeft + 360.0f, actions_bottom),
                         cancelling ? "Cancelling..." : "Cancel", !cancelling});
      break;
    }
    case Screen::kDone:
      buttons.push_back({Action::kPlay, L.P(kLeft, kActionsY), L.P(kLeft + 360.0f, actions_bottom),
                         "Play", true, true});
      buttons.push_back({Action::kQuit, L.P(kRight - kSideButtonW, kActionsY),
                         L.P(kRight, actions_bottom), "Quit", true});
      break;
  }
  return buttons;
}

void InstallerDialog::Activate(Action action) {
  switch (action) {
    case Action::kBrowseDisc:
      StartPick(Pick::kDisc);
      break;
    case Action::kChangeFolder:
      StartPick(Pick::kInstallFolder);
      break;
    case Action::kUseFolder:
      StartPick(Pick::kGameFolder);
      break;
    case Action::kInstall:
      StartInstall();
      break;
    case Action::kCancel:
      progress_.cancel = true;
      break;
    case Action::kPlay:
      if (callbacks_.play) {
        callbacks_.play(game_dir_);
      }
      break;
    case Action::kQuit:
      if (callbacks_.quit) {
        callbacks_.quit();
      }
      break;
  }
}

void InstallerDialog::StartPick(Pick pick) {
  if (Busy()) {
    return;
  }
#if defined(_WIN32)
  pick_ = pick;
  void* owner = window_ ? window_->GetNativeWindowHandle() : nullptr;
  std::filesystem::path start;
  std::wstring title;
  switch (pick) {
    case Pick::kDisc:
      start = disc_path_.empty() ? std::filesystem::path() : disc_path_.parent_path();
      title = L"Choose your Fracture (Xbox 360) disc image";
      break;
    case Pick::kInstallFolder:
      start = folder_;
      title = L"Choose where to install Fracture";
      break;
    case Pick::kGameFolder:
      start = game_folder_.empty() ? folder_ : game_folder_;
      title = L"Choose the folder with the extracted game files (default.xex)";
      break;
  }
  picker_ = std::async(std::launch::async, PickPath, owner, pick != Pick::kDisc, std::move(title),
                       std::move(start));
#else
  (void)pick;
  message_ = "Drop the disc image onto this window.";
  message_is_error_ = false;
#endif
}

void InstallerDialog::CheckDisc(std::filesystem::path image) {
  disc_path_ = image;
  disc_.reset();
  disc_error_.clear();
  message_.clear();
  disc_check_ = std::async(std::launch::async, [image = std::move(image)] {
    Disc disc;
    std::string error = installer::CheckDisc(image, disc);
    return std::pair<std::string, Disc>(std::move(error), std::move(disc));
  });
}

void InstallerDialog::CheckFolder(std::filesystem::path dir) {
  game_folder_ = dir;
  message_.clear();
  folder_check_ = std::async(std::launch::async,
                             [dir = std::move(dir)] { return CheckGameFolder(dir); });
}

void InstallerDialog::SetFolder(std::filesystem::path folder) {
  folder_ = std::move(folder);
  target_ = InstallTarget(folder_);
  message_.clear();
  UpdateSpace();
}

void InstallerDialog::UpdateSpace() {
  free_bytes_ = FreeSpace(target_);
  if (!disc_) {
    needed_bytes_ = kTypicalBytes;
    return;
  }
  // Files a stopped install already finished stay and need no room.
  needed_bytes_ = 0;
  std::error_code ec;
  for (const DiscFile& file : disc_->files) {
    const auto path = target_ / PathFromText(file.path);
    if (!std::filesystem::is_regular_file(path, ec) ||
        std::filesystem::file_size(path, ec) != file.size) {
      needed_bytes_ += file.size;
    }
  }
}

void InstallerDialog::StartInstall() {
  if (!CanInstall()) {
    return;
  }
  progress_.done_bytes = 0;
  progress_.done_files = 0;
  progress_.cancel = false;
  extract_error_.clear();
  message_.clear();
  if (callbacks_.remember) {
    callbacks_.remember(target_);
  }
  screen_ = Screen::kInstalling;
  focus_ = Action::kCancel;
  // disc_ and target_ stay put while the job runs: every control that
  // changes them is disabled while Busy().
  extract_ = std::async(std::launch::async,
                        [this] { return Extract(*disc_, target_, progress_, extract_error_); });
}

void InstallerDialog::PollJobs() {
  if (Ready(picker_)) {
    if (auto picked = picker_.get()) {
      switch (pick_) {
        case Pick::kDisc:
          CheckDisc(std::move(*picked));
          break;
        case Pick::kInstallFolder:
          SetFolder(std::move(*picked));
          break;
        case Pick::kGameFolder:
          CheckFolder(std::move(*picked));
          break;
      }
    }
  }
  if (Ready(disc_check_)) {
    auto [error, disc] = disc_check_.get();
    if (error.empty()) {
      disc_ = std::move(disc);
      UpdateSpace();
      focus_ = Action::kInstall;
    } else {
      disc_error_ = std::move(error);
      REXLOG_WARN("Installer: {}: {}", PathText(disc_path_), disc_error_);
    }
  }
  if (Ready(folder_check_)) {
    const std::string error = folder_check_.get();
    if (error.empty()) {
      game_dir_ = game_folder_;
      installed_now_ = false;
      if (callbacks_.remember) {
        callbacks_.remember(game_dir_);
      }
      screen_ = Screen::kDone;
      focus_ = Action::kPlay;
      REXLOG_INFO("Installer: using the game files in {}", PathText(game_dir_));
    } else {
      message_ = fmt::format("{} cannot be used. {}", PathText(game_folder_), error);
      message_is_error_ = true;
      REXLOG_WARN("Installer: {}", message_);
    }
  }
  if (Ready(extract_)) {
    switch (extract_.get()) {
      case Result::kDone:
        game_dir_ = target_;
        installed_now_ = true;
        screen_ = Screen::kDone;
        focus_ = Action::kPlay;
        break;
      case Result::kCancelled:
        if (callbacks_.remember) {
          callbacks_.remember(initial_folder_);
        }
        screen_ = Screen::kSetup;
        focus_ = Action::kInstall;
        message_ = "Installation cancelled. The files it had copied were removed.";
        message_is_error_ = false;
        UpdateSpace();
        break;
      case Result::kFailed:
        screen_ = Screen::kSetup;
        focus_ = Action::kInstall;
        message_ = "Installation failed. " + extract_error_;
        message_is_error_ = true;
        UpdateSpace();
        break;
    }
  }
}

void InstallerDialog::OnFileDrop(rex::ui::FileDropEvent& e) {
  if (screen_ != Screen::kSetup || Busy()) {
    return;
  }
  std::error_code ec;
  if (std::filesystem::is_directory(e.filename(), ec)) {
    CheckFolder(e.filename());
  } else {
    CheckDisc(e.filename());
  }
}

void InstallerDialog::OnDraw(ImGuiIO& io) {
  PollJobs();
  const Layout layout(io.DisplaySize);
  std::vector<Button> buttons = Buttons(layout);
  // Keep the focus on a button of this screen; a disabled one keeps it (it
  // is usually busy for a moment), so Enter never lands on Quit by itself.
  if (std::none_of(buttons.begin(), buttons.end(),
                   [&](const Button& b) { return b.action == focus_; })) {
    focus_ = buttons.front().action;
  }
  HandleInput(io, buttons);
  // Input may have changed the screen.
  buttons = Buttons(layout);
  DrawScreen(io, layout, buttons);
}

void InstallerDialog::MoveFocus(const std::vector<Button>& buttons, int direction) {
  const int count = int(buttons.size());
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (buttons[i].action == focus_) {
      index = i;
    }
  }
  for (int step = 1; step < count; ++step) {
    const Button& next = buttons[((index + direction * step) % count + count) % count];
    if (next.enabled) {
      focus_ = next.action;
      return;
    }
  }
}

int InstallerDialog::PadDirection(float delta_time, bool& confirm) {
  confirm = false;
#if defined(_WIN32)
  const XInputGetStateFn get = XInputGetStateProc();
  if (!get) {
    return 0;
  }
  pad_scan_ -= delta_time;
  const bool scan = pad_scan_ <= 0.0f;
  if (scan) {
    pad_scan_ = kPadScanInterval;
  }
  menu_input::Pad pad;
  for (DWORD user = 0; user < 4; ++user) {
    if (!scan && !(pad_connected_ & (1u << user))) {
      continue;
    }
    XINPUT_STATE state = {};
    if (get(user, &state) != ERROR_SUCCESS) {
      pad_connected_ &= uint8_t(~(1u << user));
      continue;
    }
    pad_connected_ |= uint8_t(1u << user);
    pad.buttons |= state.Gamepad.wButtons;
    if (std::abs(state.Gamepad.sThumbLX) > std::abs(pad.thumb_lx)) {
      pad.thumb_lx = state.Gamepad.sThumbLX;
    }
    if (std::abs(state.Gamepad.sThumbLY) > std::abs(pad.thumb_ly)) {
      pad.thumb_ly = state.Gamepad.sThumbLY;
    }
  }
  using namespace menu_input;
  const uint16_t pressed = pad.buttons & ~pad_prev_buttons_;
  pad_prev_buttons_ = pad.buttons;
  const int dx = ((pad.buttons & kDpadRight) || pad.thumb_lx > kPadStickThreshold ? 1 : 0) -
                 ((pad.buttons & kDpadLeft) || pad.thumb_lx < -kPadStickThreshold ? 1 : 0);
  const int dy = ((pad.buttons & kDpadDown) || pad.thumb_ly < -kPadStickThreshold ? 1 : 0) -
                 ((pad.buttons & kDpadUp) || pad.thumb_ly > kPadStickThreshold ? 1 : 0);
  // What is held at the first read is not a press.
  if (!pad_seen_) {
    pad_seen_ = true;
    pad_held_ = dy ? dy : dx;
    pad_repeat_ = kPadRepeatDelay;
    return 0;
  }
  confirm = (pressed & (kA | kStart)) != 0;
  return Repeat(dy ? dy : dx, pad_held_, pad_repeat_, delta_time);
#else
  (void)delta_time;
  return 0;
#endif
}

void InstallerDialog::HandleInput(ImGuiIO& io, const std::vector<Button>& buttons) {
  // A system file chooser is open and owns the input.
  if (picker_.valid()) {
    return;
  }
  auto activate_focus = [&] {
    for (const Button& b : buttons) {
      if (b.action == focus_ && b.enabled) {
        Activate(b.action);
      }
    }
  };

  if (!io.WantCaptureKeyboard) {
    const bool tab = Pressed(ImGuiKey_Tab);
    const int move = (Pressed(ImGuiKey_DownArrow) || Pressed(ImGuiKey_RightArrow) ||
                              (tab && !io.KeyShift)
                          ? 1
                          : 0) -
                     (Pressed(ImGuiKey_UpArrow) || Pressed(ImGuiKey_LeftArrow) || (tab && io.KeyShift)
                          ? 1
                          : 0);
    const bool confirm =
        ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (move || confirm) {
      pad_active_ = false;
    }
    if (move) {
      MoveFocus(buttons, move);
    }
    if (confirm) {
      activate_focus();
      return;
    }
  }

  bool pad_confirm = false;
  const int pad_move = PadDirection(io.DeltaTime, pad_confirm);
  if (pad_move || pad_confirm) {
    pad_active_ = true;
  }
  if (pad_move) {
    MoveFocus(buttons, pad_move);
  }
  if (pad_confirm) {
    activate_focus();
    return;
  }

  if (io.WantCaptureMouse) {
    return;
  }
  const ImVec2 mouse = io.MousePos;
  const bool mouse_valid = mouse.x > -FLT_MAX * 0.5f && mouse.y > -FLT_MAX * 0.5f;
  const bool mouse_moved =
      mouse_valid && (mouse.x != last_mouse_pos_.x || mouse.y != last_mouse_pos_.y);
  last_mouse_pos_ = mouse;
  if (!mouse_valid) {
    return;
  }
  const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  for (const Button& b : buttons) {
    if (!Contains(b.min, b.max, mouse)) {
      continue;
    }
    if (mouse_moved || clicked) {
      focus_ = b.action;
      pad_active_ = false;
    }
    if (clicked && b.enabled) {
      Activate(b.action);
    }
    break;
  }
}

void InstallerDialog::DrawScreen(ImGuiIO& io, const Layout& L, const std::vector<Button>& buttons) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, kColBackground);
  const float wrap = L.S(kRight - kLeft);

  const char* title = "Fracture Recomp";
  if (screen_ == Screen::kInstalling) {
    title = "Installing Fracture";
  } else if (screen_ == Screen::kDone) {
    title = installed_now_ ? "Fracture is installed" : "Game files found";
  }
  DrawText(dl, fonts_.title, L.S(kTitleSize), L.P(kLeft, kTitleY), kColText, title);
  dl->AddRectFilled(L.P(kLeft, kTitleY + 70.0f), L.P(kLeft + 64.0f, kTitleY + 74.0f), kColAccent);

  const float body = L.S(kBodySize);
  const float note = L.S(kNoteSize);
  auto paragraph = [&](float y, ImU32 col, const std::string& text, float size) {
    DrawText(dl, fonts_.body, size, L.P(kLeft, y), col, text, wrap);
  };
  // A heading, a field showing a path and the status line below it.
  auto field = [&](float y, const char* heading, const std::string& value, bool placeholder,
                   ImU32 status_col, const std::string& status) {
    DrawText(dl, fonts_.strong, L.S(kHeadingSize), L.P(kLeft, y), kColText, heading);
    const ImVec2 min = L.P(kLeft, y + kFieldTop);
    const ImVec2 max = L.P(kRight - kSideButtonW - kFieldGap, y + kFieldTop + kFieldH);
    dl->AddRectFilled(min, max, kColValueBox, L.S(4.0f));
    const float pad = L.S(20.0f);
    const std::string shown = FitTextStart(fonts_.body, body, value, (max.x - min.x) - pad * 2.0f);
    DrawText(dl, fonts_.body, body, ImVec2(min.x + pad, (min.y + max.y - body) * 0.5f),
             placeholder ? kColTextFaint : kColText, shown);
    DrawText(dl, fonts_.body, note, L.P(kLeft, y + kStatusTop), status_col, status, wrap);
  };

  switch (screen_) {
    case Screen::kSetup: {
      paragraph(kIntroY, kColTextDim,
                "Before the first start, the game files have to be copied from your own Xbox 360 "
                "disc image of Fracture. The disc image is only read, and nothing is downloaded.",
                body);

      std::string disc_status;
      ImU32 disc_col = kColTextFaint;
      if (disc_check_.valid()) {
        disc_status = "Checking the disc image...";
        disc_col = kColTextDim;
      } else if (disc_) {
        disc_status = fmt::format("Fracture, {}: supported. {} files, {}.", disc_->release->name,
                                  disc_->files.size(), SizeText(disc_->total_bytes));
        disc_col = kColGood;
      } else if (!disc_error_.empty()) {
        disc_status = disc_error_;
        disc_col = kColError;
      } else {
        disc_status = "Choose the .iso file, or drop it onto this window.";
      }
      field(kDiscY, "Disc image", disc_path_.empty() ? "No disc image chosen" : PathText(disc_path_),
            disc_path_.empty(), disc_col, disc_status);

      std::string space_status;
      ImU32 space_col = kColTextDim;
      const uint64_t needed = needed_bytes_ + kSpareBytes;
      if (!free_bytes_) {
        space_status = "Free space on this drive is unknown.";
        space_col = kColTextFaint;
      } else if (*free_bytes_ < needed) {
        space_status = fmt::format("Not enough free space: needs {}, {} free on this drive.",
                                   SizeText(needed), SizeText(*free_bytes_));
        space_col = kColError;
      } else {
        space_status = fmt::format("Needs {}{}. {} free on this drive.", disc_ ? "" : "about ",
                                   SizeText(needed_bytes_), SizeText(*free_bytes_));
      }
      if (target_ != folder_) {
        space_status += " A Fracture folder is made in the folder you chose.";
      }
      field(kFolderY, "Install folder", PathText(target_), false, space_col, space_status);

      if (!message_.empty()) {
        const ImVec2 min = L.P(kLeft, kMessageY);
        const float text_h = TextSize(fonts_.body, note, message_, wrap - L.S(40.0f)).y;
        const ImVec2 max(L.P(kRight, 0).x, min.y + text_h + L.S(24.0f));
        dl->AddRectFilled(min, max, message_is_error_ ? kColErrorBg : kColWarnBg, L.S(4.0f));
        DrawText(dl, fonts_.body, note, ImVec2(min.x + L.S(20.0f), min.y + L.S(12.0f)),
                 message_is_error_ ? kColError : kColWarn, message_, wrap - L.S(40.0f));
      }
      paragraph(kFooterY, kColTextFaint,
                "Saves go to Documents\\fracture. Settings and logs are kept next to fracture.exe.",
                note);
      break;
    }
    case Screen::kInstalling: {
      paragraph(kIntroY, kColTextDim, "Copying the game files into", body);
      paragraph(kIntroY + 40.0f, kColText, PathText(target_), body);
      const uint64_t total = disc_ ? disc_->total_bytes : 0;
      const uint64_t done = std::min(progress_.done_bytes.load(), total);
      const float fraction = total ? float(double(done) / double(total)) : 0.0f;
      const ImVec2 min = L.P(kLeft, kBarY);
      const ImVec2 max = L.P(kRight, kBarY + kBarH);
      dl->AddRectFilled(min, max, kColValueBox, L.S(4.0f));
      if (fraction > 0.0f) {
        dl->AddRectFilled(min, ImVec2(min.x + (max.x - min.x) * fraction, max.y), kColAccent,
                          L.S(4.0f));
      }
      const std::string amount = fmt::format("{} of {}", SizeText(done), SizeText(total));
      DrawText(dl, fonts_.body, body, L.P(kLeft, kBarY + kBarH + 18.0f), kColText, amount);
      const std::string percent = fmt::format("{}%", int(fraction * 100.0f));
      const ImVec2 percent_size = TextSize(fonts_.strong, body, percent);
      DrawText(dl, fonts_.strong, body,
               ImVec2(max.x - percent_size.x, L.P(0, kBarY + kBarH + 18.0f).y), kColText, percent);
      const int files = disc_ ? int(disc_->files.size()) : 0;
      DrawText(dl, fonts_.body, note, L.P(kLeft, kBarY + kBarH + 64.0f), kColTextDim,
               fmt::format("File {} of {}", std::min(progress_.done_files.load() + 1, files), files));
      paragraph(kFooterY, kColTextFaint,
                "If the game is closed now, the next start continues where the copy stopped.",
                note);
      break;
    }
    case Screen::kDone: {
      if (installed_now_) {
        paragraph(kIntroY, kColTextDim,
                  "The game files are in the folder below. The disc image is no longer needed "
                  "to play.",
                  body);
      } else {
        paragraph(kIntroY, kColTextDim, "The game will use the files in this folder.", body);
      }
      paragraph(kIntroY + 80.0f, kColText, PathText(game_dir_), body);
      paragraph(kIntroY + 180.0f, kColTextDim,
                "In the game, press Esc or F1, or RB + Start on a controller, for the settings "
                "screen.",
                note);
      break;
    }
  }

  // Buttons.
  const float button_text = L.S(kBodySize);
  for (const Button& b : buttons) {
    const bool focused = b.action == focus_;
    ImU32 fill = kColValueBox;
    ImU32 text = b.enabled ? kColText : kColTextFaint;
    if (b.primary && b.enabled) {
      fill = focused ? kColAccent : kColPrimaryIdle;
      text = kColChipText;
    } else if (focused && b.enabled) {
      fill = kColSelect;
    }
    dl->AddRectFilled(b.min, b.max, fill, L.S(4.0f));
    if (focused) {
      dl->AddRect(b.min, b.max, b.enabled ? kColText : kColTextFaint, L.S(4.0f), 0, L.S(2.0f));
    }
    const std::string label = FitText(fonts_.strong, button_text, b.label,
                                      (b.max.x - b.min.x) - L.S(24.0f));
    const ImVec2 size = TextSize(fonts_.strong, button_text, label);
    DrawText(dl, fonts_.strong, button_text,
             ImVec2((b.min.x + b.max.x - size.x) * 0.5f, (b.min.y + b.max.y - size.y) * 0.5f), text,
             label);
  }

  // Key hints.
  {
    struct Hint {
      const char* key;
      const char* label;
    };
    const Hint hints[] = {
        {pad_active_ ? "\xE2\x86\x91\xE2\x86\x93" : "Tab / \xE2\x86\x91\xE2\x86\x93", "Select"},
        {pad_active_ ? "A" : "Enter", "Confirm"}};
    const float size = L.S(kSmallSize);
    const float chip_pad = L.S(9.0f);
    ImVec2 pos = L.P(kLeft, kLegendY);
    for (const Hint& hint : hints) {
      const ImVec2 key_size = TextSize(fonts_.strong, size, hint.key);
      const ImVec2 chip_max(pos.x + key_size.x + chip_pad * 2.0f, pos.y + size + L.S(8.0f));
      dl->AddRectFilled(pos, chip_max, kColChip, L.S(4.0f));
      DrawText(dl, fonts_.strong, size, ImVec2(pos.x + chip_pad, pos.y + L.S(4.0f)), kColChipText,
               hint.key);
      const float label_x = chip_max.x + L.S(10.0f);
      DrawText(dl, fonts_.body, size, ImVec2(label_x, pos.y + L.S(4.0f)), kColTextDim, hint.label);
      pos.x = label_x + TextSize(fonts_.body, size, hint.label).x + L.S(34.0f);
    }
  }

  // A system file chooser is open on top.
  if (picker_.valid()) {
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, kColDim);
    const char* text = "Choose in the window that opened.";
    const ImVec2 size = TextSize(fonts_.strong, body, text);
    DrawText(dl, fonts_.strong, body,
             ImVec2((io.DisplaySize.x - size.x) * 0.5f, (io.DisplaySize.y - size.y) * 0.5f),
             kColText, text);
  }
}

}  // namespace fracture::installer
