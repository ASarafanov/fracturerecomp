// fracture - user settings storage (settings.toml), restart, cvar dump.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fracture::settings {

// settings.toml next to fracture.exe holds what the user chose in the in-game
// settings menu. It is applied after fracture.toml (which tools/setup.py
// writes and the menu never touches), so it wins over that file; command-line
// flags still win over both.
std::filesystem::path Path();

// Applies settings.toml. Call after the base config (fracture.toml) is loaded
// and before the window and the GPU plugin are created (OnPostInitLogging);
// the SDK holds values for cvars the plugin registers later and applies them
// on load. Remembers `base_config_path` for Save.
void Load(const std::filesystem::path& base_config_path);

// Merges cvar name -> value (as text) into settings.toml. A value equal to the
// cvar's default removes the key, so the file only lists real choices and
// follows SDK default changes, unless the base config sets that key: then
// removing it would bring the base config's value back, so the default is
// written. Live cvar values are not touched: settings that need a restart are
// only written here. Returns false if the file could not be written.
bool Save(const std::vector<std::pair<std::string, std::string>>& changes);

// True when two texts mean the same value of cvar `name`, e.g. "60" and
// "60.000000" for a double. Unknown cvars compare as text.
bool SameValue(std::string_view name, std::string_view a, std::string_view b);

// Starts another fracture.exe with this process's command line plus
// --fracture_restart_wait_pid, so it waits for this one to exit first, and
// --fracture_game_release=`game_release` when that is not empty (the other
// release's game files were found). The caller then closes the window.
// Returns false if nothing was started.
bool StartRestartedProcess(std::string_view game_release = {});

// In a process started by StartRestartedProcess: waits (bounded) until the
// previous one has exited, so the two never share the log files, the shader
// cache or the audio device. Call before logging is initialized.
void WaitForPreviousProcess();

// Writes every registered cvar with its type, lifecycle, source, default,
// current value and constraints as tab-separated lines. --help stops at a
// CLI11 parse error, so this is how to see what the SDK and the GPU plugin
// offer; call it once the plugin is loaded.
bool DumpCvars(const std::filesystem::path& path);

}  // namespace fracture::settings
