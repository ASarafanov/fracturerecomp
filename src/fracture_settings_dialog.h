// fracture - the in-game settings screen.

#pragma once

#include <rex/ui/imgui_dialog.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

namespace fracture::settings {

struct Fonts {
  ImFont* body = nullptr;    // labels, values, descriptions
  ImFont* strong = nullptr;  // selected items, headings
  ImFont* title = nullptr;   // the screen title
};

// Adds the faces the screen uses to the ImGui atlas: Segoe UI from the
// Windows fonts folder, so nothing is shipped. The SDK's renderer bakes the
// atlas once, so they are baked large and drawn scaled. Missing faces stay
// null and the screen falls back to ImGui's default font.
Fonts LoadFonts(ImFontAtlas* atlas);

// What the screen keeps between openings: choices that only apply at the
// next start (already written to settings.toml) and where the cursor was.
struct State {
  std::map<std::string, std::string> pending;  // cvar -> value after restart
  int category = 0;
  int row = 0;
};

// Fullscreen, console-style screen: categories on the left, setting rows with
// left/right value steppers in the middle, a description on the right, key
// hints along the bottom. Live settings apply and save on change; the others
// are saved to settings.toml and listed as pending until the game restarts.
// Created when opened and destroyed when closed (the owner does both).
class SettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  struct Callbacks {
    std::function<void()> close;    // close the screen
    std::function<void()> restart;  // restart the game to apply pending changes
    std::function<void()> quit;     // quit the game
  };

  SettingsDialog(rex::ui::ImGuiDrawer* drawer, const Fonts& fonts, State& state,
                 Callbacks callbacks);
  ~SettingsDialog() override;

  // One step back (Escape, Backspace): from the setting rows to the category
  // list, from there out of the screen.
  void NavigateBack();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Option;
  struct Row;
  struct Category;
  struct Layout;
  struct Intents;
  enum class Zone { kRail, kRows };

  void BuildCategories();
  std::string Current(const std::string& cvar) const;
  int OptionIndex(const Row& row, bool running) const;
  bool IsPending(const Row& row) const;
  bool IsSetOnCommandLine(const Row& row) const;
  void SetOption(const Row& row, int index);
  void Step(const Row& row, int direction);
  void Activate(const Row& row);
  void SelectCategory(int category);
  void SelectRow(int row);
  void RevertPending();

  void HandleInput(ImGuiIO& io, const Layout& layout);
  Intents KeyboardIntents();
  Intents PadIntents(float delta_time);
  // Returns true when the screen is closing or restarting.
  bool Apply(const Intents& intents);
  void HandleMouse(ImGuiIO& io, const Layout& layout);
  void DrawScreen(ImGuiIO& io, const Layout& layout);

  Fonts fonts_;
  State& state_;
  Callbacks callbacks_;
  std::vector<Category> categories_;
  Zone zone_ = Zone::kRail;
  int row_scroll_ = 0;
  bool row_armed_ = false;      // a confirm row was pressed once
  bool restart_armed_ = false;  // R / X was pressed once
  ImVec2 last_mouse_pos_{-1.0f, -1.0f};

  // Controller navigation.
  bool pad_seen_ = false;      // first frame read: held buttons are not presses
  bool pad_active_ = false;    // last input came from a controller (hints)
  uint16_t pad_prev_buttons_ = 0;
  int pad_held_x_ = 0;         // held direction (-1, 0, 1), for auto-repeat
  int pad_held_y_ = 0;
  float pad_repeat_x_ = 0.0f;  // seconds until the next repeat
  float pad_repeat_y_ = 0.0f;
};

}  // namespace fracture::settings
