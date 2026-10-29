// fracture - the in-game settings screen.

#include "fracture_settings_dialog.h"

#include "fracture_menu_input.h"
#include "fracture_settings.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <fmt/format.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <dxgi.h>
#endif

namespace fracture::settings {
namespace {

// Hardware adapters as (index, name), indexed like the d3d12_adapter cvar:
// the position in IDXGIFactory1::EnumAdapters1 order, software ones skipped.
// On hybrid systems DXGI can list one GPU twice (different LUIDs, only the
// first with the displays attached); only the first of each name and memory
// size is kept, which is also the one automatic selection picks.
std::vector<std::pair<int, std::string>> GraphicsAdapters() {
  std::vector<std::pair<int, std::string>> adapters;
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
        adapters.emplace_back(int(i), name);
      }
    }
    adapter->Release();
  }
  factory->Release();
#endif
  return adapters;
}

// Sizes the faces are baked at; text is drawn at layout size times the screen
// scale, so bake for about 1440p to stay sharp there and above.
constexpr float kBodyBakeSize = 36.0f;
constexpr float kTitleBakeSize = 64.0f;

// Layout in a 1920x1080 frame, scaled to fit the display and centred.
constexpr float kBaseWidth = 1920.0f;
constexpr float kBaseHeight = 1080.0f;
constexpr float kRailX = 120.0f;
constexpr float kRailW = 300.0f;
constexpr float kRailItemH = 60.0f;
constexpr float kTop = 190.0f;
constexpr float kRowsX = 470.0f;
constexpr float kRowsW = 840.0f;
constexpr float kRowH = 68.0f;
constexpr float kRowsBottom = 900.0f;
constexpr float kValueW = 380.0f;
constexpr float kDescX = 1360.0f;
constexpr float kDescW = 440.0f;
constexpr float kBannerY = 924.0f;
constexpr float kLegendY = 1004.0f;
constexpr float kTitleSize = 52.0f;
constexpr float kRailTextSize = 28.0f;
constexpr float kRowTextSize = 26.0f;
constexpr float kDescTextSize = 23.0f;
constexpr float kSmallTextSize = 21.0f;
constexpr int kVisibleRows = int((kRowsBottom - kTop) / kRowH);

constexpr ImU32 kColDim = IM_COL32(6, 8, 12, 222);
constexpr ImU32 kColPanel = IM_COL32(24, 28, 36, 235);
constexpr ImU32 kColSelect = IM_COL32(255, 255, 255, 26);
constexpr ImU32 kColSelectIdle = IM_COL32(255, 255, 255, 12);
constexpr ImU32 kColValueBox = IM_COL32(0, 0, 0, 70);
constexpr ImU32 kColText = IM_COL32(236, 239, 244, 255);
constexpr ImU32 kColTextDim = IM_COL32(160, 168, 182, 255);
constexpr ImU32 kColTextFaint = IM_COL32(100, 108, 122, 255);
constexpr ImU32 kColAccent = IM_COL32(255, 138, 36, 255);
constexpr ImU32 kColAccentIdle = IM_COL32(255, 138, 36, 110);
constexpr ImU32 kColWarn = IM_COL32(255, 196, 64, 255);
constexpr ImU32 kColWarnBg = IM_COL32(255, 196, 64, 30);
constexpr ImU32 kColChip = IM_COL32(236, 239, 244, 255);
constexpr ImU32 kColChipText = IM_COL32(16, 18, 22, 255);

bool Contains(ImVec2 min, ImVec2 max, ImVec2 p) {
  return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
}

ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap = 0.0f) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
}

// Shortens `text` with "..." until it fits `max_width` (long GPU names).
std::string FitText(ImFont* font, float size, std::string text, float max_width) {
  if (TextSize(font, size, text).x <= max_width) {
    return text;
  }
  while (!text.empty() && TextSize(font, size, text + "...").x > max_width) {
    text.pop_back();
  }
  return text + "...";
}

void DrawText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
              std::string_view text, float wrap = 0.0f) {
  dl->AddText(font, size, ImVec2(std::floor(pos.x), std::floor(pos.y)), col, text.data(),
              text.data() + text.size(), wrap);
}

void DrawChevron(ImDrawList* dl, ImVec2 center, float half, bool left, ImU32 col) {
  if (left) {
    dl->AddTriangleFilled(ImVec2(center.x - half * 0.6f, center.y),
                          ImVec2(center.x + half * 0.4f, center.y - half),
                          ImVec2(center.x + half * 0.4f, center.y + half), col);
  } else {
    dl->AddTriangleFilled(ImVec2(center.x + half * 0.6f, center.y),
                          ImVec2(center.x - half * 0.4f, center.y + half),
                          ImVec2(center.x - half * 0.4f, center.y - half), col);
  }
}

bool Pressed(ImGuiKey key) {
  return ImGui::IsKeyPressed(key, true);
}

bool AnyKeyboardKeyDown() {
  for (int key = ImGuiKey_Tab; key <= ImGuiKey_KeypadEqual; ++key) {
    if (ImGui::IsKeyDown(ImGuiKey(key))) {
      return true;
    }
  }
  return false;
}

// Controller auto-repeat for a held direction.
constexpr float kPadRepeatDelay = 0.35f;
constexpr float kPadRepeatInterval = 0.09f;
constexpr int16_t kPadStickThreshold = 16384;

int Repeat(int direction, int& held, float& timer, float delta_time) {
  if (direction == 0) {
    held = 0;
    return 0;
  }
  if (direction != held) {
    held = direction;
    timer = kPadRepeatDelay;
    return direction;
  }
  timer -= delta_time;
  if (timer > 0.0f) {
    return 0;
  }
  timer += kPadRepeatInterval;
  return direction;
}

}  // namespace

struct SettingsDialog::Option {
  std::string label;
  std::vector<std::pair<std::string, std::string>> values;  // cvar -> value
};

struct SettingsDialog::Row {
  std::string label;
  std::string description;
  std::vector<Option> options;  // empty for an action row
  bool restart = false;         // only read at startup
  std::function<void()> action;
  std::function<bool()> enabled;  // action rows; null means always
  bool confirm = false;           // action needs a second press
};

struct SettingsDialog::Category {
  std::string name;
  std::string description;
  std::vector<Row> rows;
};

// What one frame of keyboard or controller input asks for.
struct SettingsDialog::Intents {
  int category = 0;  // -1 / +1: previous / next category
  int move = 0;      // -1 / +1: up / down
  int step = 0;      // -1 / +1: left / right
  bool confirm = false;
  bool back = false;
  bool restart = false;

  bool Any() const { return category || move || step || confirm || back || restart; }
};

struct SettingsDialog::Layout {
  float scale;
  ImVec2 origin;

  explicit Layout(ImVec2 display) {
    scale = std::min(display.x / kBaseWidth, display.y / kBaseHeight);
    origin = ImVec2((display.x - kBaseWidth * scale) * 0.5f,
                    (display.y - kBaseHeight * scale) * 0.5f);
  }
  ImVec2 P(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
  float S(float v) const { return v * scale; }

  ImVec2 RailMin(int i) const { return P(kRailX, kTop + i * kRailItemH); }
  ImVec2 RailMax(int i) const { return P(kRailX + kRailW, kTop + (i + 1) * kRailItemH - 8.0f); }
  // `slot` counts from the first visible row.
  ImVec2 RowMin(int slot) const { return P(kRowsX, kTop + slot * kRowH); }
  ImVec2 RowMax(int slot) const { return P(kRowsX + kRowsW, kTop + (slot + 1) * kRowH - 8.0f); }
  ImVec2 ValueMin(int slot) const {
    return P(kRowsX + kRowsW - kValueW - 16.0f, kTop + slot * kRowH + 10.0f);
  }
  ImVec2 ValueMax(int slot) const {
    return P(kRowsX + kRowsW - 16.0f, kTop + (slot + 1) * kRowH - 18.0f);
  }
};

Fonts LoadFonts(ImFontAtlas* atlas) {
  Fonts fonts;
#if defined(_WIN32)
  // Basic Latin + Latin-1, dashes to ellipsis, arrows (key hints).
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0};
  wchar_t windows_dir[MAX_PATH] = {};
  const UINT length = GetWindowsDirectoryW(windows_dir, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return fonts;
  }
  const auto fonts_dir = std::filesystem::path(windows_dir) / "Fonts";
  auto load = [&](const char* file, float size) -> ImFont* {
    const auto path = fonts_dir / file;
    if (!std::filesystem::exists(path)) {
      REXLOG_WARN("Settings: font {} not found", path.string());
      return nullptr;
    }
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    return atlas->AddFontFromFileTTF(path.string().c_str(), size, &config, kRanges);
  };
  fonts.body = load("segoeui.ttf", kBodyBakeSize);
  fonts.strong = load("seguisb.ttf", kBodyBakeSize);
  fonts.title = load("seguisb.ttf", kTitleBakeSize);
#else
  (void)atlas;
#endif
  return fonts;
}

SettingsDialog::SettingsDialog(rex::ui::ImGuiDrawer* drawer, const Fonts& fonts, State& state,
                               Callbacks callbacks)
    : ImGuiDialog(drawer), fonts_(fonts), state_(state), callbacks_(std::move(callbacks)) {
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
  BuildCategories();
  state_.category = std::clamp(state_.category, 0, int(categories_.size()) - 1);
  SelectRow(state_.row);
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::BuildCategories() {
  // One option per value of a single cvar.
  auto choice = [](std::string label, std::string description, const std::string& cvar,
                   std::vector<std::pair<std::string, std::string>> options) {
    Row row{std::move(label), std::move(description)};
    for (auto& [option_label, value] : options) {
      row.options.push_back({std::move(option_label), {{cvar, std::move(value)}}});
    }
    return row;
  };
  auto action = [](std::string label, std::string description, std::function<void()> fn,
                   std::function<bool()> enabled = nullptr) {
    Row row{std::move(label), std::move(description)};
    row.action = std::move(fn);
    row.enabled = std::move(enabled);
    return row;
  };

  Category display{"Display", "Window, monitor and how the picture reaches the screen."};
  display.rows.push_back(choice("Fullscreen", "Switch between a window and fullscreen.",
                                "fullscreen", {{"Off", "false"}, {"On", "true"}}));
  display.rows.push_back(choice("Monitor", "The monitor the game opens on.", "monitor",
                                {{"Default", "0"},
                                 {"Monitor 1", "1"},
                                 {"Monitor 2", "2"},
                                 {"Monitor 3", "3"},
                                 {"Monitor 4", "4"}}));
  {
    Row row{"Window Size",
            "Size of the game window when it is not fullscreen, in Windows' scaled pixels "
            "(at 150% scaling, 1280 x 720 covers 1920 x 1080 screen pixels)."};
    const std::pair<const char*, const char*> sizes[] = {{"Default (1280 x 720)", "0x0"},
                                                         {"1600 x 900", "1600x900"},
                                                         {"1920 x 1080", "1920x1080"},
                                                         {"2560 x 1440", "2560x1440"}};
    for (const auto& [label, size] : sizes) {
      const std::string_view text(size);
      const size_t x = text.find('x');
      row.options.push_back({label,
                             {{"window_width", std::string(text.substr(0, x))},
                              {"window_height", std::string(text.substr(x + 1))}}});
    }
    display.rows.push_back(std::move(row));
  }
  display.rows.push_back(choice("Keep Aspect Ratio",
                                "Show the 16:9 picture with black bars instead of stretching "
                                "it to the window shape.",
                                "present_letterbox", {{"Off", "false"}, {"On", "true"}}));
  display.rows.push_back(choice("Allow Tearing",
                                "Show each frame as soon as it is ready instead of waiting for "
                                "the display refresh. Lower latency; needed for G-Sync and "
                                "FreeSync.",
                                "d3d12_allow_variable_refresh_rate_and_tearing",
                                {{"Off", "false"}, {"On", "true"}}));

  Category graphics{"Graphics", "Rendering resolution and image quality."};
  if (const auto adapters = GraphicsAdapters(); !adapters.empty()) {
    std::vector<std::pair<std::string, std::string>> options = {{"Automatic", "-1"}};
    for (const auto& [index, name] : adapters) {
      options.emplace_back(name, std::to_string(index));
    }
    graphics.rows.push_back(choice("Graphics Card",
                                   "The GPU the game renders on. Automatic picks the first one "
                                   "Windows lists, and follows a GPU change.",
                                   "d3d12_adapter", std::move(options)));
  }
  {
    Row row{"Internal Resolution",
            "Renders the game at a multiple of the Xbox 360's 1280x720 and scales the result "
            "to the window. Higher is sharper and needs more GPU time and memory."};
    const std::pair<const char*, const char*> scales[] = {
        {"1x (720p)", "1"}, {"2x (1440p)", "2"}, {"3x (2160p)", "3"}, {"4x (2880p)", "4"}};
    for (const auto& [label, scale] : scales) {
      row.options.push_back({label,
                             {{"resolution_scale", scale},
                              {"draw_resolution_scale_x", scale},
                              {"draw_resolution_scale_y", scale}}});
    }
    graphics.rows.push_back(std::move(row));
  }
  graphics.rows.push_back(choice("Anti-Aliasing",
                                 "Smooths jagged edges with FXAA after the frame is drawn. "
                                 "Extreme smooths more and blurs fine detail a little more.",
                                 "swap_post_effect",
                                 {{"Off", "none"}, {"FXAA", "fxaa"}, {"FXAA Extreme", "fxaa_extreme"}}));
  graphics.rows.push_back(choice("Texture Filtering",
                                 "Anisotropic filtering keeps textures sharp at steep viewing "
                                 "angles, such as the ground ahead.",
                                 "anisotropic_override",
                                 {{"Game Default", "-1"},
                                  {"2x", "2"},
                                  {"4x", "3"},
                                  {"8x", "4"},
                                  {"16x", "5"}}));
  graphics.rows.push_back(choice("Render Path",
                                 "How the Xbox 360's EDRAM render targets are emulated. Fast "
                                 "uses the GPU's own render targets. Accurate emulates them in "
                                 "pixel shaders: it fixes some blending and format errors and is "
                                 "much slower. Automatic picks Fast (Accurate on Intel GPUs).",
                                 "render_target_path_d3d12",
                                 {{"Automatic", ""}, {"Fast (RTV)", "rtv"}, {"Accurate (ROV)", "rov"}}));

  Category controls{"Controls", "Controllers, keyboard and mouse."};
  {
    Row row = choice("Controller API",
                     "SDL supports Xbox, PlayStation and most other controllers. XInput "
                     "supports Xbox controllers only.",
                     "input_backend", {{"SDL", "sdl"}, {"XInput", "xinput"}});
    // The cvar reads as live, but the input drivers are created at startup.
    row.restart = true;
    controls.rows.push_back(std::move(row));
  }
  controls.rows.push_back(choice("Settings Shortcut",
                                 "Controller buttons that open and close this screen. Escape "
                                 "and F1 always work.",
                                 "fracture_menu_chord",
                                 {{"RB + Start", "rb+start"},
                                  {"LB + RB + Start", "lb+rb+start"},
                                  {"Back + Start", "back+start"},
                                  {"L3 + R3", "l3+r3"},
                                  {"Off", "none"}}));
  controls.rows.push_back(choice("Keyboard and Mouse",
                                 "Play with the keyboard as a controller (keybind_* settings).",
                                 "mnk_mode", {{"Off", "false"}, {"On", "true"}}));
  controls.rows.push_back(choice("Mouse Look", "Use the mouse for the right stick.", "mnk_mouse",
                                 {{"Off", "false"}, {"On", "true"}}));
  controls.rows.push_back(choice("Mouse Sensitivity", "How far the camera turns per mouse move.",
                                 "mnk_sensitivity",
                                 {{"0.25", "0.25"},
                                  {"0.5", "0.5"},
                                  {"0.75", "0.75"},
                                  {"1.0", "1.0"},
                                  {"1.25", "1.25"},
                                  {"1.5", "1.5"},
                                  {"2.0", "2.0"},
                                  {"3.0", "3.0"}}));

  Category audio{"Audio", "Sound output."};
  audio.rows.push_back(choice("Mute", "Silence all game audio.", "audio_mute",
                              {{"Off", "false"}, {"On", "true"}}));
  {
    Row row = choice("Audio Latency",
                     "How many audio frames are queued ahead. Lower means less delay; raise "
                     "it if the sound crackles or stutters.",
                     "audio_maxqframes",
                     {{"Low (4)", "4"}, {"Default (8)", "8"}, {"Stable (16)", "16"},
                      {"Most Stable (32)", "32"}});
    // The cvar reads as live, but the audio system sizes its queue at startup.
    row.restart = true;
    audio.rows.push_back(std::move(row));
  }

  Category system{"System", "Pending changes, closing the screen and quitting."};
  system.rows.push_back(action(
      "Restart Now",
      "Restart the game to apply the changes that need a restart. Unsaved progress is lost.",
      [this] {
        if (callbacks_.restart) {
          callbacks_.restart();
        }
      },
      [this] { return !state_.pending.empty(); }));
  system.rows.back().confirm = true;
  system.rows.push_back(action(
      "Revert Pending Changes",
      "Undo the changes that are waiting for a restart; the game keeps its current settings.",
      [this] { RevertPending(); }, [this] { return !state_.pending.empty(); }));
  system.rows.push_back(action("Close Settings", "Back to the game.", [this] {
    if (callbacks_.close) {
      callbacks_.close();
    }
  }));
  system.rows.push_back(action("Quit Game", "Close Fracture. Unsaved progress is lost.", [this] {
    if (callbacks_.quit) {
      callbacks_.quit();
    }
  }));
  system.rows.back().confirm = true;

  for (Category* category : {&display, &graphics, &controls, &audio, &system}) {
    // Drop rows whose cvars this build does not have, and mark the ones the
    // cvar registry says only apply at startup.
    std::erase_if(category->rows, [](Row& row) {
      for (const Option& option : row.options) {
        for (const auto& [cvar, value] : option.values) {
          const auto* entry = rex::cvar::GetFlagInfo(cvar);
          if (!entry) {
            REXLOG_WARN("Settings: hiding '{}', cvar {} does not exist", row.label, cvar);
            return true;
          }
          if (entry->lifecycle != rex::cvar::Lifecycle::kHotReload) {
            row.restart = true;
          }
        }
      }
      return false;
    });
    if (!category->rows.empty()) {
      categories_.push_back(std::move(*category));
    }
  }
}

std::string SettingsDialog::Current(const std::string& cvar) const {
  auto it = state_.pending.find(cvar);
  return it != state_.pending.end() ? it->second : rex::cvar::GetFlagByName(cvar);
}

int SettingsDialog::OptionIndex(const Row& row, bool running) const {
  for (int i = 0; i < int(row.options.size()); ++i) {
    bool all = true;
    for (const auto& [cvar, value] : row.options[i].values) {
      const std::string current = running ? rex::cvar::GetFlagByName(cvar) : Current(cvar);
      if (!SameValue(cvar, current, value)) {
        all = false;
        break;
      }
    }
    if (all) {
      return i;
    }
  }
  return -1;
}

bool SettingsDialog::IsPending(const Row& row) const {
  for (const Option& option : row.options) {
    for (const auto& [cvar, value] : option.values) {
      if (state_.pending.contains(cvar)) {
        return true;
      }
    }
  }
  return false;
}

bool SettingsDialog::IsSetOnCommandLine(const Row& row) const {
  if (row.options.empty()) {
    return false;
  }
  for (const auto& [cvar, value] : row.options[0].values) {
    if (rex::cvar::GetFlagSource(cvar) == rex::cvar::Source::kCommandLine) {
      return true;
    }
  }
  return false;
}

void SettingsDialog::SetOption(const Row& row, int index) {
  const Option& option = row.options[index];
  if (row.restart) {
    for (const auto& [cvar, value] : option.values) {
      if (SameValue(cvar, rex::cvar::GetFlagByName(cvar), value)) {
        state_.pending.erase(cvar);
      } else {
        state_.pending[cvar] = value;
      }
    }
  } else {
    for (const auto& [cvar, value] : option.values) {
      if (!rex::cvar::SetFlagByName(cvar, value)) {
        REXLOG_WARN("Settings: {} rejected value '{}'", cvar, value);
      }
    }
  }
  Save(option.values);
}

void SettingsDialog::Step(const Row& row, int direction) {
  if (row.options.empty()) {
    return;
  }
  const int count = int(row.options.size());
  const int current = OptionIndex(row, false);
  const int next =
      current < 0 ? (direction > 0 ? 0 : count - 1) : std::clamp(current + direction, 0, count - 1);
  if (next != current) {
    SetOption(row, next);
  }
}

void SettingsDialog::Activate(const Row& row) {
  if (row.action) {
    if (row.enabled && !row.enabled()) {
      return;
    }
    if (row.confirm && !row_armed_) {
      row_armed_ = true;
      return;
    }
    row_armed_ = false;
    row.action();
    return;
  }
  const int current = OptionIndex(row, false);
  SetOption(row, (current + 1) % int(row.options.size()));
}

void SettingsDialog::SelectCategory(int category) {
  category = std::clamp(category, 0, int(categories_.size()) - 1);
  if (category != state_.category) {
    state_.category = category;
    row_scroll_ = 0;
    restart_armed_ = false;
    SelectRow(0);
  }
}

void SettingsDialog::SelectRow(int row) {
  const int count = int(categories_[state_.category].rows.size());
  row = std::clamp(row, 0, std::max(count - 1, 0));
  if (row != state_.row) {
    row_armed_ = false;
    restart_armed_ = false;
  }
  state_.row = row;
  if (row < row_scroll_) {
    row_scroll_ = row;
  } else if (row >= row_scroll_ + kVisibleRows) {
    row_scroll_ = row - kVisibleRows + 1;
  }
}

void SettingsDialog::RevertPending() {
  std::vector<std::pair<std::string, std::string>> changes;
  for (const auto& [cvar, value] : state_.pending) {
    changes.emplace_back(cvar, rex::cvar::GetFlagByName(cvar));
  }
  state_.pending.clear();
  Save(changes);
}

void SettingsDialog::NavigateBack() {
  if (zone_ == Zone::kRows) {
    zone_ = Zone::kRail;
    row_armed_ = false;
    restart_armed_ = false;
  } else if (callbacks_.close) {
    callbacks_.close();
  }
}

void SettingsDialog::OnDraw(ImGuiIO& io) {
  const Layout layout(io.DisplaySize);
  HandleInput(io, layout);
  DrawScreen(io, layout);
}

void SettingsDialog::HandleInput(ImGuiIO& io, const Layout& layout) {
  // This screen draws on the background list and owns no ImGui window, so
  // ImGui only wants the keyboard or mouse when another overlay (console,
  // F4 settings) has it; typing there must not change settings here.
  if (!io.WantCaptureKeyboard) {
    const Intents keys = KeyboardIntents();
    if (keys.Any()) {
      pad_active_ = false;
    }
    if (Apply(keys)) {
      return;
    }
  }
  const Intents pad = PadIntents(io.DeltaTime);
  if (pad.Any()) {
    pad_active_ = true;
  }
  if (Apply(pad)) {
    return;
  }
  if (!io.WantCaptureMouse) {
    HandleMouse(io, layout);
  }
}

SettingsDialog::Intents SettingsDialog::KeyboardIntents() {
  // Escape is not read here: it arrives through the app's keybind, which
  // calls NavigateBack.
  Intents in;
  in.category = (Pressed(ImGuiKey_E) || Pressed(ImGuiKey_PageDown) ? 1 : 0) -
                (Pressed(ImGuiKey_Q) || Pressed(ImGuiKey_PageUp) ? 1 : 0);
  in.move = (Pressed(ImGuiKey_DownArrow) ? 1 : 0) - (Pressed(ImGuiKey_UpArrow) ? 1 : 0);
  in.step = (Pressed(ImGuiKey_RightArrow) ? 1 : 0) - (Pressed(ImGuiKey_LeftArrow) ? 1 : 0);
  in.confirm = Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_Space);
  in.back = Pressed(ImGuiKey_Backspace);
  in.restart = ImGui::IsKeyPressed(ImGuiKey_R, false);
  return in;
}

SettingsDialog::Intents SettingsDialog::PadIntents(float delta_time) {
  using namespace menu_input;
  const Pad pad = LatestPad();
  const uint16_t pressed = pad.buttons & ~pad_prev_buttons_;
  pad_prev_buttons_ = pad.buttons;
  const int dx = ((pad.buttons & kDpadRight) || pad.thumb_lx > kPadStickThreshold ? 1 : 0) -
                 ((pad.buttons & kDpadLeft) || pad.thumb_lx < -kPadStickThreshold ? 1 : 0);
  const int dy = ((pad.buttons & kDpadDown) || pad.thumb_ly < -kPadStickThreshold ? 1 : 0) -
                 ((pad.buttons & kDpadUp) || pad.thumb_ly > kPadStickThreshold ? 1 : 0);
  // Ignored: what was already held when the screen opened (the chord that
  // opened it), the chord itself (it closes the screen), and, with keyboard
  // and mouse mode on, pad input while keys are down (the keyboard drives
  // the pad then, and its keys already act here directly).
  const uint16_t chord = ChordMask();
  const bool skip = !pad_seen_ || (chord != 0 && (pad.buttons & chord) == chord) ||
                    (rex::cvar::Query<bool>("mnk_mode") && AnyKeyboardKeyDown());
  if (!pad_seen_ && pad.buttons != 0) {
    pad_active_ = true;  // opened from the controller: show its hints
  }
  pad_seen_ = true;
  if (skip) {
    pad_held_x_ = dx;
    pad_held_y_ = dy;
    pad_repeat_x_ = pad_repeat_y_ = kPadRepeatDelay;
    return {};
  }
  Intents in;
  in.category = (pressed & kRightShoulder ? 1 : 0) - (pressed & kLeftShoulder ? 1 : 0);
  in.move = Repeat(dy, pad_held_y_, pad_repeat_y_, delta_time);
  in.step = Repeat(dx, pad_held_x_, pad_repeat_x_, delta_time);
  in.confirm = (pressed & (kA | kStart)) != 0;
  in.back = (pressed & kB) != 0;
  in.restart = (pressed & kX) != 0;
  return in;
}

bool SettingsDialog::Apply(const Intents& in) {
  if (in.category) {
    SelectCategory(state_.category + in.category);
  }
  if (zone_ == Zone::kRail) {
    if (in.move) {
      SelectCategory(state_.category + in.move);
    }
    if (in.step > 0 || in.confirm) {
      zone_ = Zone::kRows;
    }
  } else {
    // Categories are never empty (BuildCategories drops those).
    const auto& rows = categories_[state_.category].rows;
    if (in.move) {
      SelectRow(state_.row + in.move);
    }
    if (in.step) {
      Step(rows[state_.row], in.step);
    }
    if (in.confirm) {
      Activate(rows[state_.row]);
    }
  }
  if (in.back) {
    NavigateBack();
    return true;
  }
  if (in.restart && !state_.pending.empty()) {
    if (!restart_armed_) {
      restart_armed_ = true;
    } else if (callbacks_.restart) {
      restart_armed_ = false;
      callbacks_.restart();
      return true;
    }
  }
  return false;
}

void SettingsDialog::HandleMouse(ImGuiIO& io, const Layout& layout) {
  // Hovering moves the selection only when the mouse actually moved, so a
  // resting cursor does not fight the keyboard.
  const auto& rows = categories_[state_.category].rows;
  const ImVec2 mouse = io.MousePos;
  const bool mouse_valid = mouse.x > -FLT_MAX * 0.5f && mouse.y > -FLT_MAX * 0.5f;
  const bool mouse_moved =
      mouse_valid && (mouse.x != last_mouse_pos_.x || mouse.y != last_mouse_pos_.y);
  last_mouse_pos_ = mouse;
  if (!mouse_valid) {
    return;
  }
  const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  if (mouse_moved || clicked) {
    pad_active_ = false;
  }
  for (int i = 0; i < int(categories_.size()); ++i) {
    if (Contains(layout.RailMin(i), layout.RailMax(i), mouse) && clicked) {
      SelectCategory(i);
      zone_ = Zone::kRail;
      return;
    }
  }
  const int last_slot = std::min(int(rows.size()) - row_scroll_, kVisibleRows);
  for (int slot = 0; slot < last_slot; ++slot) {
    if (!Contains(layout.RowMin(slot), layout.RowMax(slot), mouse)) {
      continue;
    }
    const int index = row_scroll_ + slot;
    if (mouse_moved || clicked) {
      zone_ = Zone::kRows;
      SelectRow(index);
    }
    if (clicked) {
      const Row& row = rows[index];
      const ImVec2 value_min = layout.ValueMin(slot);
      const ImVec2 value_max = layout.ValueMax(slot);
      const float third = (value_max.x - value_min.x) / 3.0f;
      if (!row.options.empty() && mouse.x >= value_min.x && mouse.x < value_min.x + third) {
        Step(row, -1);
      } else if (!row.options.empty() && mouse.x >= value_max.x - third &&
                 mouse.x < value_max.x) {
        Step(row, +1);
      } else {
        Activate(row);
      }
    }
    break;
  }
  if (io.MouseWheel != 0.0f &&
      Contains(layout.P(kRowsX, kTop), layout.P(kRowsX + kRowsW, kRowsBottom), mouse)) {
    zone_ = Zone::kRows;
    SelectRow(state_.row + (io.MouseWheel > 0.0f ? -1 : 1));
  }
}

void SettingsDialog::DrawScreen(ImGuiIO& io, const Layout& L) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const Category& category = categories_[state_.category];

  dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, kColDim);

  // Title.
  DrawText(dl, fonts_.title, L.S(kTitleSize), L.P(kRailX, 82.0f), kColText, "Settings");
  dl->AddRectFilled(L.P(kRailX, 150.0f), L.P(kRailX + 64.0f, 154.0f), kColAccent);

  // Category rail.
  for (int i = 0; i < int(categories_.size()); ++i) {
    const ImVec2 min = L.RailMin(i);
    const ImVec2 max = L.RailMax(i);
    const bool selected = i == state_.category;
    if (selected) {
      dl->AddRectFilled(min, max, zone_ == Zone::kRail ? kColSelect : kColSelectIdle,
                        L.S(4.0f));
      dl->AddRectFilled(min, ImVec2(min.x + L.S(4.0f), max.y),
                        zone_ == Zone::kRail ? kColAccent : kColAccentIdle);
    }
    const float size = L.S(kRailTextSize);
    DrawText(dl, selected ? fonts_.strong : fonts_.body, size,
             ImVec2(min.x + L.S(24.0f), (min.y + max.y - size) * 0.5f),
             selected ? kColText : kColTextDim, categories_[i].name);
  }

  // Setting rows.
  const int last_slot = std::min(int(category.rows.size()) - row_scroll_, kVisibleRows);
  for (int slot = 0; slot < last_slot; ++slot) {
    const int index = row_scroll_ + slot;
    const Row& row = category.rows[index];
    const bool selected = zone_ == Zone::kRows && index == state_.row;
    const ImVec2 min = L.RowMin(slot);
    const ImVec2 max = L.RowMax(slot);
    if (selected) {
      dl->AddRectFilled(min, max, kColSelect, L.S(4.0f));
      dl->AddRectFilled(min, ImVec2(min.x + L.S(4.0f), max.y), kColAccent);
    }
    const float size = L.S(kRowTextSize);
    const float text_y = (min.y + max.y - size) * 0.5f;
    const bool enabled = !row.action || !row.enabled || row.enabled();
    std::string label = row.label;
    ImU32 label_col = enabled ? (selected ? kColText : kColTextDim) : kColTextFaint;
    if (row.confirm && row_armed_ && selected) {
      label += " - press again to confirm";
      label_col = kColWarn;
    }
    DrawText(dl, selected ? fonts_.strong : fonts_.body, size, ImVec2(min.x + L.S(28.0f), text_y),
             label_col, label);
    if (row.options.empty()) {
      continue;
    }

    const ImVec2 vmin = L.ValueMin(slot);
    const ImVec2 vmax = L.ValueMax(slot);
    dl->AddRectFilled(vmin, vmax, kColValueBox, L.S(4.0f));
    const int current = OptionIndex(row, false);
    const int count = int(row.options.size());
    std::string value = current >= 0 ? row.options[current].label
                                     : "Custom (" + Current(row.options[0].values[0].first) + ")";
    if (IsSetOnCommandLine(row)) {
      value += " *";
    }
    const ImU32 chevron_on = selected ? kColAccent : kColTextDim;
    const float half = L.S(9.0f);
    const float cy = (vmin.y + vmax.y) * 0.5f;
    DrawChevron(dl, ImVec2(vmin.x + L.S(22.0f), cy), half, true,
                current != 0 ? chevron_on : kColTextFaint);
    DrawChevron(dl, ImVec2(vmax.x - L.S(22.0f), cy), half, false,
                current != count - 1 ? chevron_on : kColTextFaint);
    // Room between the two chevrons.
    value = FitText(fonts_.body, size, std::move(value), (vmax.x - vmin.x) - L.S(88.0f));
    const ImVec2 value_size = TextSize(fonts_.body, size, value);
    DrawText(dl, fonts_.body, size,
             ImVec2((vmin.x + vmax.x - value_size.x) * 0.5f, cy - size * 0.5f),
             IsPending(row) ? kColWarn : kColText, value);
  }

  // Description panel.
  {
    const ImVec2 min = L.P(kDescX, kTop);
    const ImVec2 max = L.P(kDescX + kDescW, kRowsBottom - 8.0f);
    dl->AddRectFilled(min, max, kColPanel, L.S(6.0f));
    const float pad = L.S(28.0f);
    const float wrap = (max.x - min.x) - pad * 2.0f;
    float y = min.y + pad;
    const bool on_row = zone_ == Zone::kRows && !category.rows.empty();
    const Row* row = on_row ? &category.rows[state_.row] : nullptr;
    const std::string& heading = row ? row->label : category.name;
    const std::string& text = row ? row->description : category.description;
    const float heading_size = L.S(kRailTextSize);
    DrawText(dl, fonts_.strong, heading_size, ImVec2(min.x + pad, y), kColText, heading);
    y += heading_size + L.S(16.0f);
    const float text_size = L.S(kDescTextSize);
    DrawText(dl, fonts_.body, text_size, ImVec2(min.x + pad, y), kColTextDim, text, wrap);
    y += TextSize(fonts_.body, text_size, text, wrap).y + L.S(24.0f);

    auto note = [&](ImU32 col, const std::string& line) {
      DrawText(dl, fonts_.body, text_size, ImVec2(min.x + pad, y), col, line, wrap);
      y += TextSize(fonts_.body, text_size, line, wrap).y + L.S(12.0f);
    };
    if (row && !row->options.empty()) {
      note(kColTextFaint, row->restart ? "Applies after a restart." : "Applies immediately.");
      if (IsPending(*row)) {
        const int running = OptionIndex(*row, true);
        note(kColWarn, "Pending until the game restarts. Now: " +
                           (running >= 0 ? row->options[running].label : std::string("Custom")) +
                           ".");
      }
      if (IsSetOnCommandLine(*row)) {
        note(kColWarn,
             "* Set on the command line, which wins over this menu at every start.");
      }
    }
  }

  // Pending banner.
  if (!state_.pending.empty()) {
    const ImVec2 min = L.P(kRowsX, kBannerY);
    const ImVec2 max = L.P(kRowsX + kRowsW, kBannerY + 46.0f);
    dl->AddRectFilled(min, max, kColWarnBg, L.S(4.0f));
    const float size = L.S(kDescTextSize);
    const size_t count = state_.pending.size();
    DrawText(dl, fonts_.body, size, ImVec2(min.x + L.S(20.0f), (min.y + max.y - size) * 0.5f),
             kColWarn,
             restart_armed_
                 ? fmt::format("Press {} again to restart now. Unsaved progress is lost.",
                               pad_active_ ? "X" : "R")
                 : fmt::format("{} setting{} will change after a restart. Press {} to restart now.",
                               count, count == 1 ? "" : "s", pad_active_ ? "X" : "R"));
  }

  // Key hints.
  {
    struct Hint {
      const char* key;
      const char* label;
    };
    std::vector<Hint> hints;
    if (pad_active_) {
      hints = {{"\xE2\x86\x91\xE2\x86\x93", "Select"},
               {"\xE2\x86\x90\xE2\x86\x92", "Change"},
               {"A", "Confirm"},
               {"LB / RB", "Category"},
               {"B", "Back"}};
    } else {
      hints = {{"\xE2\x86\x91\xE2\x86\x93", "Select"},
               {"\xE2\x86\x90\xE2\x86\x92", "Change"},
               {"Enter", "Confirm"},
               {"Q / E", "Category"},
               {"Esc", "Back"}};
    }
    if (!state_.pending.empty()) {
      hints.push_back({pad_active_ ? "X" : "R", "Restart"});
    }
    const float size = L.S(kSmallTextSize);
    const float chip_pad = L.S(9.0f);
    ImVec2 pos = L.P(kRailX, kLegendY);
    for (const Hint& hint : hints) {
      const ImVec2 key_size = TextSize(fonts_.strong, size, hint.key);
      const ImVec2 chip_max(pos.x + key_size.x + chip_pad * 2.0f, pos.y + size + L.S(8.0f));
      dl->AddRectFilled(pos, chip_max, kColChip, L.S(4.0f));
      DrawText(dl, fonts_.strong, size, ImVec2(pos.x + chip_pad, pos.y + L.S(4.0f)), kColChipText,
               hint.key);
      const float label_x = chip_max.x + L.S(10.0f);
      DrawText(dl, fonts_.body, size, ImVec2(label_x, pos.y + L.S(4.0f)), kColTextDim,
               hint.label);
      pos.x = label_x + TextSize(fonts_.body, size, hint.label).x + L.S(34.0f);
    }
  }
}

}  // namespace fracture::settings
