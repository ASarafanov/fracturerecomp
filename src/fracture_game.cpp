// fracture - picks and loads the recompiled code for the installed release.

#include "fracture_game.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#include <algorithm>

#include "fracture_frame_rate.h"
#include "fracture_menu_input.h"
#include "fracture_paths.h"
#include "game/game_api.h"

#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(fracture_game_release, "", "Fracture",
                      "Recompiled code to load: v1 or v2 (the default.xex version). Empty "
                      "picks the release of the game files; the program sets it when it "
                      "restarts for the other release's files");

namespace fracture::game {
namespace {

const HostApi kHost = {kApiVersion, menu_input::GuestXamInputGetState, frame_rate::Interval};

std::filesystem::path DllPath(const installer::Release& release) {
  return rex::filesystem::GetExecutableFolder() / fmt::format("fracture_{}.dll", release.variant);
}

// game_data_root from a config file next to the exe, or "". The SDK loads
// these files only after the app exists, so they are read here directly.
std::string ConfiguredRoot(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) {
    return {};
  }
  try {
    const toml::table table = toml::parse_file(file.u8string());
    return table["game_data_root"].value_or(std::string());
  } catch (const toml::parse_error&) {
    return {};
  }
}

}  // namespace

std::vector<const installer::Release*> AvailableReleases() {
  std::vector<const installer::Release*> available;
  std::error_code ec;
  for (const installer::Release& release : installer::kReleases) {
    if (std::filesystem::is_regular_file(DllPath(release), ec)) {
      available.push_back(&release);
    }
  }
  return available;
}

std::vector<std::filesystem::path> DefaultGameFolders() {
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::vector<std::filesystem::path> folders = {exe_dir / "game"};
  const auto build_dir = exe_dir.parent_path();
  if (build_dir.filename() == "build" && build_dir.parent_path().filename() == "out") {
    folders.push_back(build_dir.parent_path().parent_path() / "game");
  }
  return folders;
}

const installer::Release* PickRelease(const std::vector<const installer::Release*>& available,
                                      bool& forced) {
  forced = false;
  const std::string flag = REXCVAR_GET(fracture_game_release);
  REXCVAR_SET(fracture_game_release, std::string());
  if (available.empty()) {
    return nullptr;
  }
  auto is_available = [&](const installer::Release* release) {
    return release && std::find(available.begin(), available.end(), release) != available.end();
  };
  if (!flag.empty()) {
    if (const auto* release = installer::FindRelease(flag); is_available(release)) {
      forced = true;
      return release;
    }
  }
  // Command-line flags are parsed by now; settings.toml wins over
  // fracture.toml, as when the SDK loads them.
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::vector<std::filesystem::path> folders;
  for (const std::string& configured :
       {std::string(REXCVAR_GET(game_data_root)), ConfiguredRoot(exe_dir / "settings.toml"),
        ConfiguredRoot(exe_dir / "fracture.toml")}) {
    if (!configured.empty()) {
      std::filesystem::path dir = PathFromText(configured);
      folders.push_back(dir.is_relative() ? exe_dir / dir : dir);
      break;
    }
  }
  for (const auto& dir : DefaultGameFolders()) {
    folders.push_back(dir);
  }
  for (const auto& dir : folders) {
    if (const auto* release = installer::InstalledRelease(dir); is_available(release)) {
      return release;
    }
  }
  return available.front();
}

std::optional<rex::PPCImageInfo> Load(const installer::Release& release, std::string& error) {
#if defined(_WIN32)
  const std::filesystem::path path = DllPath(release);
  HMODULE module = LoadLibraryW(path.c_str());
  if (!module) {
    error = fmt::format("Could not load {} (error {}).", PathText(path), GetLastError());
    return std::nullopt;
  }
  // Stays loaded for the life of the process: the runtime keeps pointers
  // into it.
  const auto image_info =
      reinterpret_cast<ImageInfoFunc>(GetProcAddress(module, kImageInfoExport));
  const auto set_host = reinterpret_cast<SetHostFunc>(GetProcAddress(module, kSetHostExport));
  if (!image_info || !set_host || !image_info()) {
    error = fmt::format("{} is not a Fracture game module.", PathText(path));
    return std::nullopt;
  }
  if (!set_host(&kHost)) {
    error = fmt::format("{} belongs to another version of fracture.exe.", PathText(path));
    return std::nullopt;
  }
  REXLOG_INFO("Game code: {} ({})", PathText(path.filename()), release.name);
  return *image_info();
#else
  (void)release;
  error = "Loading the game code is only implemented on Windows.";
  return std::nullopt;
#endif
}

}  // namespace fracture::game
