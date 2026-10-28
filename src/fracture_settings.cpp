// fracture - user settings storage (settings.toml), restart, cvar dump.

#include "fracture_settings.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_UINT32(fracture_restart_wait_pid, 0, "Fracture",
                      "Set by the settings menu's restart: wait for this "
                      "process to exit before starting");

namespace fracture::settings {
namespace {

using rex::cvar::FlagEntry;
using rex::cvar::FlagType;

constexpr std::string_view kFileName = "settings.toml";
constexpr std::string_view kHeader =
    "# fracture.exe settings chosen in the in-game settings menu.\n"
    "# Applied after fracture.toml, so values here win over it; command-line\n"
    "# flags still win over both. Delete a line to go back to the default.\n\n";

bool ParseBool(std::string_view text, bool& out) {
  if (text == "true" || text == "1") {
    out = true;
    return true;
  }
  if (text == "false" || text == "0") {
    out = false;
    return true;
  }
  return false;
}

template <typename T>
bool ParseInteger(std::string_view text, T& out) {
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc() && ptr == text.data() + text.size();
}

// Values are text in whatever form their writer produced ("60.000000" from
// the cvar macro, "60" from the menu), so compare by value, not by spelling.
bool ValuesEqual(FlagType type, std::string_view a, std::string_view b) {
  if (a == b) {
    return true;
  }
  switch (type) {
    case FlagType::Boolean: {
      bool x = false;
      bool y = false;
      return ParseBool(a, x) && ParseBool(b, y) && x == y;
    }
    case FlagType::Int32:
    case FlagType::Int64:
    case FlagType::Uint32:
    case FlagType::Uint64:
    case FlagType::Double: {
      double x = 0.0;
      double y = 0.0;
      return rex::cvar::ParseDouble(a, x) && rex::cvar::ParseDouble(b, y) && x == y;
    }
    default:
      return false;
  }
}

bool IsDefault(const FlagEntry& entry, std::string_view value) {
  return ValuesEqual(entry.type, value, entry.default_value);
}

// Stores the value with the cvar's own TOML type so the file stays easy to
// edit by hand. LoadConfig turns every value back into text for the setter,
// so a string is a safe fallback for anything else.
bool Assign(toml::table& table, const std::string& name, const FlagEntry* entry,
            std::string_view value) {
  if (!entry) {
    table.insert_or_assign(name, std::string(value));
    return true;
  }
  switch (entry->type) {
    case FlagType::Boolean: {
      bool parsed = false;
      if (!ParseBool(value, parsed)) {
        return false;
      }
      table.insert_or_assign(name, parsed);
      return true;
    }
    case FlagType::Int32:
    case FlagType::Int64:
    case FlagType::Uint32: {
      int64_t parsed = 0;
      if (!ParseInteger(value, parsed)) {
        return false;
      }
      table.insert_or_assign(name, parsed);
      return true;
    }
    case FlagType::Uint64: {
      uint64_t parsed = 0;
      if (!ParseInteger(value, parsed)) {
        return false;
      }
      // TOML integers are signed 64-bit.
      if (parsed > uint64_t(std::numeric_limits<int64_t>::max())) {
        table.insert_or_assign(name, std::string(value));
      } else {
        table.insert_or_assign(name, int64_t(parsed));
      }
      return true;
    }
    case FlagType::Double: {
      double parsed = 0.0;
      if (!rex::cvar::ParseDouble(value, parsed)) {
        return false;
      }
      table.insert_or_assign(name, parsed);
      return true;
    }
    case FlagType::String:
      table.insert_or_assign(name, std::string(value));
      return true;
    case FlagType::Command:
      return false;
  }
  return false;
}

std::string_view TypeName(FlagType type) {
  switch (type) {
    case FlagType::Boolean:
      return "bool";
    case FlagType::Int32:
      return "int32";
    case FlagType::Int64:
      return "int64";
    case FlagType::Uint32:
      return "uint32";
    case FlagType::Uint64:
      return "uint64";
    case FlagType::Double:
      return "double";
    case FlagType::String:
      return "string";
    case FlagType::Command:
      return "command";
  }
  return "?";
}

std::string_view LifecycleName(rex::cvar::Lifecycle lifecycle) {
  switch (lifecycle) {
    case rex::cvar::Lifecycle::kInitOnly:
      return "init_only";
    case rex::cvar::Lifecycle::kHotReload:
      return "live";
    case rex::cvar::Lifecycle::kRequiresRestart:
      return "restart";
  }
  return "?";
}

std::string_view SourceName(rex::cvar::Source source) {
  switch (source) {
    case rex::cvar::Source::kDefault:
      return "default";
    case rex::cvar::Source::kConfig:
      return "config";
    case rex::cvar::Source::kEnvironment:
      return "env";
    case rex::cvar::Source::kCommandLine:
      return "cmdline";
    case rex::cvar::Source::kRuntime:
      return "runtime";
  }
  return "?";
}

std::filesystem::path g_base_config_path;

// Keys the base config sets. An unreadable file sets nothing: the SDK could
// not have applied it either.
toml::table ReadBaseConfig() {
  if (g_base_config_path.empty() || !std::filesystem::exists(g_base_config_path)) {
    return {};
  }
  try {
    return toml::parse_file(g_base_config_path.string());
  } catch (const toml::parse_error&) {
    return {};
  }
}

// Keeps one cvar per line in the dump.
std::string OneLine(std::string text) {
  std::replace(text.begin(), text.end(), '\n', ' ');
  std::replace(text.begin(), text.end(), '\t', ' ');
  return text;
}

}  // namespace

std::filesystem::path Path() {
  return rex::filesystem::GetExecutableFolder() / kFileName;
}

void Load(const std::filesystem::path& base_config_path) {
  g_base_config_path = base_config_path;
  const auto path = Path();
  if (!std::filesystem::exists(path)) {
    return;
  }
  rex::cvar::LoadConfig(path);
  REXLOG_INFO("Settings: applied {}", path.string());
}

bool Save(const std::vector<std::pair<std::string, std::string>>& changes) {
  const auto path = Path();
  toml::table table;
  if (std::filesystem::exists(path)) {
    try {
      table = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
      // Keep the broken file for the user instead of silently dropping it.
      auto backup = path;
      backup += ".bad";
      std::error_code ec;
      std::filesystem::rename(path, backup, ec);
      REXLOG_WARN("Settings: {} is not valid TOML ({}), moved it to {}", path.string(),
                  err.description(), backup.string());
    }
  }

  const toml::table base_config = ReadBaseConfig();
  size_t applied = 0;
  for (const auto& [name, value] : changes) {
    const FlagEntry* entry = rex::cvar::GetFlagInfo(name);
    if (entry && IsDefault(*entry, value) && !base_config.contains(name)) {
      table.erase(name);
    } else if (!Assign(table, name, entry, value)) {
      REXLOG_WARN("Settings: not saving {} = '{}' (not a valid value)", name, value);
      continue;
    }
    ++applied;
  }

  // Write next to the file and swap it in, so a crash mid-write cannot leave
  // a truncated settings.toml behind.
  auto temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << kHeader << table << '\n';
    if (!out) {
      REXLOG_ERROR("Settings: failed to write {}", temp.string());
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    REXLOG_ERROR("Settings: failed to replace {}: {}", path.string(), ec.message());
    return false;
  }
  REXLOG_INFO("Settings: saved {} change(s) to {}", applied, path.string());
  return true;
}

bool SameValue(std::string_view name, std::string_view a, std::string_view b) {
  const FlagEntry* entry = rex::cvar::GetFlagInfo(name);
  return entry ? ValuesEqual(entry->type, a, b) : a == b;
}

#if defined(_WIN32)

namespace {
constexpr std::wstring_view kWaitFlag = L" --fracture_restart_wait_pid=";
constexpr DWORD kRestartWaitMs = 20000;
}  // namespace

bool StartRestartedProcess() {
  wchar_t exe_path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    REXLOG_ERROR("Restart: could not resolve the executable path");
    return false;
  }

  // The original command line as typed, minus the wait flag an earlier
  // restart appended (always last-added in exactly this form).
  std::wstring command_line = GetCommandLineW();
  for (size_t pos; (pos = command_line.find(kWaitFlag)) != std::wstring::npos;) {
    size_t end = pos + kWaitFlag.size();
    while (end < command_line.size() && iswdigit(command_line[end])) {
      ++end;
    }
    command_line.erase(pos, end - pos);
  }
  command_line += kWaitFlag;
  command_line += std::to_wstring(GetCurrentProcessId());

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  if (!CreateProcessW(exe_path, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &startup_info, &process_info)) {
    REXLOG_ERROR("Restart: CreateProcess failed ({})", GetLastError());
    return false;
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  REXLOG_INFO("Restart: started process {}", process_info.dwProcessId);
  return true;
}

void WaitForPreviousProcess() {
  const uint32_t pid = REXCVAR_GET(fracture_restart_wait_pid);
  if (pid == 0) {
    return;
  }
  // A missing process has already exited; a timeout means it hangs on
  // shutdown, and starting anyway beats never starting.
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (process) {
    WaitForSingleObject(process, kRestartWaitMs);
    CloseHandle(process);
  }
  // Back to the default, so a config save from this process (the F4
  // overlay's SaveConfig writes every non-default cvar) cannot persist the
  // pid and make every later start wait on it.
  REXCVAR_SET(fracture_restart_wait_pid, 0);
}

#else

bool StartRestartedProcess() {
  REXLOG_ERROR("Restart: not implemented on this platform");
  return false;
}

void WaitForPreviousProcess() {}

#endif

bool DumpCvars(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  auto names = rex::cvar::ListFlags();
  std::sort(names.begin(), names.end());
  out << "name\ttype\tlifecycle\tsource\tcategory\tdefault\tcurrent\tconstraints\tdescription\n";
  for (const auto& name : names) {
    const FlagEntry* entry = rex::cvar::GetFlagInfo(name);
    if (!entry) {
      continue;
    }
    std::string constraints;
    const auto& c = entry->constraints;
    if (c.min) {
      constraints += fmt::format("min={} ", *c.min);
    }
    if (c.max) {
      constraints += fmt::format("max={} ", *c.max);
    }
    if (!c.allowed_values.empty()) {
      constraints += "allowed=" + fmt::format("{}", fmt::join(c.allowed_values, "|")) + " ";
    }
    if (c.custom_validator) {
      constraints += "validator ";
    }
    if (entry->is_debug_only) {
      constraints += "debug_only ";
    }
    const std::string current = entry->type == FlagType::Command ? "" : entry->getter();
    out << entry->name << '\t' << TypeName(entry->type) << '\t'
        << LifecycleName(entry->lifecycle) << '\t' << SourceName(entry->source) << '\t'
        << entry->category << '\t' << OneLine(entry->default_value) << '\t' << OneLine(current)
        << '\t' << constraints << '\t' << OneLine(entry->description) << '\n';
  }
  return bool(out);
}

}  // namespace fracture::settings
