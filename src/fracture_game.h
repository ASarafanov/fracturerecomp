// fracture - picks and loads the recompiled code for the installed release.
//
// The code of each supported default.xex is a DLL next to fracture.exe
// (src/game/game_api.h). The SDK takes the image layout when the app is
// created, before any config file or the install wizard has run, so the
// release is picked here from what can be found that early; a start that
// later finds the other release's game files restarts once with
// --fracture_game_release.

#pragma once

#include <rex/image_info.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "fracture_installer.h"

namespace fracture::game {

// The releases whose DLL is next to fracture.exe, in kReleases order.
std::vector<const installer::Release*> AvailableReleases();

// Where game files are looked for when game_data_root does not name a
// folder with them: game\ next to the exe and, for an exe in
// out\build\<preset>\ (setup.py's), game\ in the repository root.
std::vector<std::filesystem::path> DefaultGameFolders();

// The release to load: --fracture_game_release when set, else the release
// of the game files the start will most likely use (game_data_root from the
// command line, settings.toml or fracture.toml next to the exe, then
// DefaultGameFolders), else the first available one. nullptr when no game
// DLL is there. `forced` tells whether the flag picked it (this process is a
// restart for other game files and must not restart again for them); the
// flag is cleared after, so no config save from this process keeps it.
const installer::Release* PickRelease(const std::vector<const installer::Release*>& available,
                                      bool& forced);

// Loads fracture_<variant>.dll and connects it to the exe. Returns the image
// layout of its code, or nullopt with a message in `error`.
std::optional<rex::PPCImageInfo> Load(const installer::Release& release, std::string& error);

}  // namespace fracture::game
