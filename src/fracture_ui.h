// fracture - drawing helpers shared by the settings screen and the install
// wizard: colours, text, chevrons, key presses, controller auto-repeat.

#pragma once

#include <imgui.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace fracture::ui {

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
constexpr ImU32 kColError = IM_COL32(255, 110, 96, 255);
constexpr ImU32 kColGood = IM_COL32(120, 214, 140, 255);
constexpr ImU32 kColChip = IM_COL32(236, 239, 244, 255);
constexpr ImU32 kColChipText = IM_COL32(16, 18, 22, 255);

inline bool Contains(ImVec2 min, ImVec2 max, ImVec2 p) {
  return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
}

inline ImVec2 TextSize(ImFont* font, float size, std::string_view text, float wrap = 0.0f) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
}

// Removes the last UTF-8 code point.
inline void PopCodePoint(std::string& text) {
  while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
    text.pop_back();
  }
  if (!text.empty()) {
    text.pop_back();
  }
}

// Shortens `text` with "..." until it fits `max_width` (long GPU names).
inline std::string FitText(ImFont* font, float size, std::string text, float max_width) {
  if (TextSize(font, size, text).x <= max_width) {
    return text;
  }
  while (!text.empty() && TextSize(font, size, text + "...").x > max_width) {
    PopCodePoint(text);
  }
  return text + "...";
}

// Like FitText, but keeps the end, which is the telling part of a path:
// "...\Downloads\Fracture.iso".
inline std::string FitTextStart(ImFont* font, float size, std::string text, float max_width) {
  if (TextSize(font, size, text).x <= max_width) {
    return text;
  }
  while (!text.empty() && TextSize(font, size, "..." + text).x > max_width) {
    size_t length = 1;
    while (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
      ++length;
    }
    text.erase(0, length);
  }
  return "..." + text;
}

inline void DrawText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col,
                     std::string_view text, float wrap = 0.0f) {
  dl->AddText(font, size, ImVec2(std::floor(pos.x), std::floor(pos.y)), col, text.data(),
              text.data() + text.size(), wrap);
}

inline void DrawChevron(ImDrawList* dl, ImVec2 center, float half, bool left, ImU32 col) {
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

inline bool Pressed(ImGuiKey key) {
  return ImGui::IsKeyPressed(key, true);
}

inline bool AnyKeyboardKeyDown() {
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

inline int Repeat(int direction, int& held, float& timer, float delta_time) {
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

}  // namespace fracture::ui
