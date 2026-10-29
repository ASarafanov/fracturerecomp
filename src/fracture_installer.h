// fracture - installs the game files from the player's Xbox 360 disc image.
//
// The first start (no game files found) shows the install wizard
// (fracture_installer_dialog.h); this is the part without UI: the disc
// reader (XDVDFS, as tools/xdvdfs.py), the check that the disc carries
// exactly the default.xex this build was recompiled from, and the
// extraction. Nothing here touches the UI; Extract runs on a worker thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fracture_paths.h"

namespace fracture::installer {

// The recompiled code was made from this exact executable.
inline constexpr uint32_t kTitleId = 0x4C4107D4;
inline constexpr std::string_view kXexSha256 =
    "13fa6817089233d6d2b30adeca5c8bf74871ea28d0c620253aade6ee89557db2";

struct DiscFile {
  std::string path;     // as on the disc, '/' separated: "resources/x.big"
  uint64_t offset = 0;  // absolute byte offset in the image
  uint64_t size = 0;
};

struct Disc {
  std::filesystem::path image;
  std::vector<DiscFile> files;  // in disc order, default.xex last
  uint64_t total_bytes = 0;
};

// Reads the file table of `image` and checks its default.xex (title ID and
// SHA-256). Returns "" and fills `disc`, or a message for the player.
std::string CheckDisc(const std::filesystem::path& image, Disc& disc);

// The same check for game files that are already extracted.
std::string CheckGameFolder(const std::filesystem::path& dir);

// True when `dir` holds default.xex. It is written last, so an interrupted
// install never counts.
bool HasGameData(const std::filesystem::path& dir);

// Where the files go when the player picks `folder`: the folder itself when
// it is missing, empty or holds (part of) an install of this game, otherwise
// a "Fracture" folder inside it, so 4 GB never spill into a full folder.
std::filesystem::path InstallTarget(const std::filesystem::path& folder);

// True when an install into `dir` was started and has not finished.
bool IsUnfinishedInstall(const std::filesystem::path& dir);

// The disc image that unfinished install was started from, if it still
// exists, so the wizard can pick it up again.
std::filesystem::path UnfinishedDisc(const std::filesystem::path& dir);

// Free bytes on the drive that holds `path` or its nearest existing parent.
std::optional<uint64_t> FreeSpace(const std::filesystem::path& path);

struct Progress {
  std::atomic<uint64_t> done_bytes{0};
  std::atomic<int> done_files{0};
  std::atomic<bool> cancel{false};
};

enum class Result { kDone, kCancelled, kFailed };

// Extracts `disc` into `target`. Every file is written as <name>.part and
// renamed once complete; files already there with the right size are kept,
// so a second run resumes an interrupted one. On cancel or failure it
// removes the files this run wrote. `error` gets a message on kFailed.
Result Extract(const Disc& disc, const std::filesystem::path& target, Progress& progress,
               std::string& error);

// "4.12 GB", "830 MB".
std::string SizeText(uint64_t bytes);

}  // namespace fracture::installer
