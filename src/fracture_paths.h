// fracture - paths as UTF-8 text.
//
// On Windows, path::string() and path(std::string) use the ANSI code page
// and string() throws for characters it cannot represent, while cvars, TOML
// and ImGui use UTF-8. Players unpack the game anywhere, often under a user
// folder with a non-Latin name, so convert explicitly.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace fracture {

// UTF-8 text of a path (logs, UI, TOML).
inline std::string PathText(const std::filesystem::path& path) {
  try {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
  } catch (const std::exception&) {
    return "?";
  }
}

// A path from UTF-8 text (cvars, TOML).
inline std::filesystem::path PathFromText(std::string_view text) {
  try {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
  } catch (const std::exception&) {
    return {};
  }
}

}  // namespace fracture
