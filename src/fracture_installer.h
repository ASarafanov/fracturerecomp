// fracture - installs the game files from the player's Xbox 360 disc image.
//
// The first start (no game files found) shows the install wizard
// (fracture_installer_dialog.h); this is the part without UI: the disc
// reader (XDVDFS, as tools/xdvdfs.py), the check that the disc carries
// exactly one of the default.xex files this program was recompiled from,
// and the extraction. Nothing here touches the UI; Extract runs on a worker
// thread.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fracture_paths.h"

namespace fracture::installer {

inline constexpr uint32_t kTitleId = 0x4C4107D4;

// A release of Fracture whose default.xex has recompiled code in this
// program. Each release's code is a DLL of its own next to fracture.exe
// (fracture_<variant>.dll); the game files of the two releases differ too,
// so they never mix.
struct Release {
  std::string_view variant;  // "v1", "v2": the DLL, codegen output, configs
  uint32_t media_id;         // from the XEX execution info header
  std::string_view xex_sha256;
  std::string_view name;     // for messages and logs
};

inline constexpr Release kReleases[] = {
    {"v1", 0x20E2A1F5, "13fa6817089233d6d2b30adeca5c8bf74871ea28d0c620253aade6ee89557db2",
     "default.xex version 1"},
    {"v2", 0x43445F92, "6e1f8972281e02175617eb94bb9bde8baea1a1f767768dbfb0b4bed11b7bafee",
     "default.xex version 2 (USA, Europe, 7 languages)"},
};

const Release* FindRelease(std::string_view variant);

// The releases whose recompiled code this program carries (a source build
// only has the release it was built from). Set once at startup.
void SetAvailableReleases(std::span<const Release* const> releases);
bool IsAvailable(const Release& release);

struct DiscFile {
  std::string path;     // as on the disc, '/' separated: "resources/x.big"
  uint64_t offset = 0;  // absolute byte offset in the image
  uint64_t size = 0;
};

struct Disc {
  std::filesystem::path image;
  const Release* release = nullptr;
  std::vector<DiscFile> files;  // in disc order, default.xex last
  uint64_t total_bytes = 0;
};

// Reads the file table of `image` and checks its default.xex (title ID and
// SHA-256). Returns "" and fills `disc`, or a message for the player.
std::string CheckDisc(const std::filesystem::path& image, Disc& disc);

// The same check for game files that are already extracted; `release` gets
// the release they are.
std::string CheckGameFolder(const std::filesystem::path& dir, const Release** release = nullptr);

// True when `dir` holds default.xex. It is written last, so an interrupted
// install never counts.
bool HasGameData(const std::filesystem::path& dir);

// The release of the game files in `dir`, from default.xex's header (title
// and media ID; no hash, this runs on every start), or nullptr.
const Release* InstalledRelease(const std::filesystem::path& dir);

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
// renamed once complete. When `target` holds an unfinished install of the
// same release, files already there with the right size are kept, so a
// second run resumes it; otherwise whatever lies at the disc's paths is
// removed first (the releases have files of equal size and different
// content, so nothing is kept across releases). On cancel or failure it
// removes the files this run wrote. `error` gets a message on kFailed.
Result Extract(const Disc& disc, const std::filesystem::path& target, Progress& progress,
               std::string& error);

// "4.12 GB", "830 MB".
std::string SizeText(uint64_t bytes);

}  // namespace fracture::installer
