// fracture - installs the game files from the player's Xbox 360 disc image.

#include "fracture_installer.h"

#include <rex/logging.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>

namespace fracture::installer {
namespace {

constexpr uint64_t kSector = 2048;
constexpr std::string_view kMagic = "MICROSOFT*XBOX*MEDIA";
// Game partition offsets: plain XISO, XGD3, XGD2, XGD1.
constexpr std::array<uint64_t, 4> kPartitionOffsets = {0, 0x2080000, 0xFD90000, 0x18300000};
constexpr uint8_t kAttrDirectory = 0x10;
// The console dashboard update on the disc, not game data.
constexpr std::string_view kSkippedDir = "$systemupdate/";
// Sanity limits for the file table and default.xex (the real ones are a
// few KB and 7.4 MB).
constexpr uint32_t kMaxDirectorySize = 16u << 20;
constexpr uint64_t kMaxXexSize = 64ull << 20;
constexpr size_t kMaxEntries = 100000;
// Left in the target while an install is unfinished, so the folder is
// recognised as ours when the wizard comes back.
constexpr std::string_view kMarker = ".fracture-install";
constexpr size_t kCopyChunk = 4u << 20;

uint16_t Le16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}
uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}

bool ReadAt(std::ifstream& in, uint64_t offset, void* data, size_t size) {
  in.clear();
  in.seekg(std::streamoff(offset));
  in.read(static_cast<char*>(data), std::streamsize(size));
  return in.gcount() == std::streamsize(size);
}

// SHA-256 of a whole buffer (FIPS 180-4).
std::string Sha256Hex(const std::vector<uint8_t>& data) {
  static constexpr uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  auto block = [&](const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = Be32(p + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 =
          hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  };
  const size_t full = data.size() / 64 * 64;
  for (size_t i = 0; i < full; i += 64) {
    block(data.data() + i);
  }
  // Padding: 0x80, zeros, then the length in bits (big-endian).
  uint8_t tail[128] = {};
  const size_t rest = data.size() - full;
  std::copy(data.begin() + full, data.end(), tail);
  tail[rest] = 0x80;
  const size_t tail_size = rest < 56 ? 64 : 128;
  const uint64_t bits = uint64_t(data.size()) * 8;
  for (int i = 0; i < 8; ++i) {
    tail[tail_size - 1 - i] = uint8_t(bits >> (i * 8));
  }
  for (size_t i = 0; i < tail_size; i += 64) {
    block(tail + i);
  }
  std::string hex;
  for (uint32_t word : h) {
    hex += fmt::format("{:08x}", word);
  }
  return hex;
}

// Title ID from the XEX2 execution info header, or 0.
uint32_t XexTitleId(const std::vector<uint8_t>& data) {
  if (data.size() < 24 || std::string_view(reinterpret_cast<const char*>(data.data()), 4) != "XEX2") {
    return 0;
  }
  const uint32_t count = Be32(data.data() + 20);
  for (uint64_t i = 0; i < count && 24 + i * 8 + 8 <= data.size(); ++i) {
    const uint8_t* header = data.data() + 24 + i * 8;
    if (Be32(header) == 0x00040006) {
      const uint64_t value = Be32(header + 4);
      return value + 16 <= data.size() ? Be32(data.data() + value + 12) : 0;
    }
  }
  return 0;
}

std::string CheckXex(const std::vector<uint8_t>& data) {
  const uint32_t title_id = XexTitleId(data);
  if (title_id != kTitleId) {
    return title_id ? fmt::format("This is not Fracture (title ID {:08X}).", title_id)
                    : std::string("default.xex is not an Xbox 360 executable.");
  }
  if (Sha256Hex(data) != kXexSha256) {
    return "This is a different release of Fracture than the one this build was made from "
           "(default.xex differs), so the game would not run.";
  }
  return {};
}

// A file name from the disc that is safe to create: printable ASCII, no
// path separators or characters Windows forbids, not "." or "..".
bool SafeName(std::string_view name) {
  if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') {
    return false;
  }
  for (char c : name) {
    if (c < 0x20 || c > 0x7E || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// Walks the XDVDFS directory trees. Returns "" or a message.
std::string ReadFileTable(std::ifstream& in, uint64_t image_size, std::vector<DiscFile>& files) {
  uint64_t base = 0;
  uint32_t root_sector = 0;
  uint32_t root_size = 0;
  bool found = false;
  for (uint64_t offset : kPartitionOffsets) {
    uint8_t header[28];
    if (ReadAt(in, offset + 32 * kSector, header, sizeof(header)) &&
        std::string_view(reinterpret_cast<const char*>(header), kMagic.size()) == kMagic) {
      base = offset;
      root_sector = Le32(header + 20);
      root_size = Le32(header + 24);
      found = true;
      break;
    }
  }
  if (!found) {
    return "This is not an Xbox 360 disc image.";
  }

  struct Directory {
    uint32_t sector;
    uint32_t size;
    std::string prefix;
  };
  std::vector<Directory> directories{{root_sector, root_size, ""}};
  size_t entries = 0;
  while (!directories.empty()) {
    const Directory dir = std::move(directories.back());
    directories.pop_back();
    if (dir.size == 0) {
      continue;
    }
    if (dir.size > kMaxDirectorySize || dir.prefix.size() > 400) {
      return "The disc image is damaged (bad directory).";
    }
    std::vector<uint8_t> data(dir.size);
    if (!ReadAt(in, base + uint64_t(dir.sector) * kSector, data.data(), data.size())) {
      return "The disc image is incomplete or damaged.";
    }
    // Each directory is a binary tree of entries; offsets count 4 bytes.
    std::vector<uint32_t> stack{0};
    std::set<uint32_t> seen;
    while (!stack.empty()) {
      const uint32_t off = stack.back();
      stack.pop_back();
      if (!seen.insert(off).second || off + 14 > data.size()) {
        continue;
      }
      if (++entries > kMaxEntries) {
        return "The disc image is damaged (too many files).";
      }
      const uint8_t* entry = data.data() + off;
      const uint16_t left = Le16(entry);
      const uint16_t right = Le16(entry + 2);
      if (left == 0xFFFF) {  // sector padding
        continue;
      }
      const uint32_t start = Le32(entry + 4);
      const uint32_t length = Le32(entry + 8);
      const uint8_t attributes = entry[12];
      const uint8_t name_length = entry[13];
      if (off + 14 + name_length > data.size()) {
        continue;
      }
      const std::string name(reinterpret_cast<const char*>(entry + 14), name_length);
      if (!SafeName(name)) {
        return "The disc image contains an unexpected file name.";
      }
      if (attributes & kAttrDirectory) {
        directories.push_back({start, length, dir.prefix + name + "/"});
      } else {
        const uint64_t offset = base + uint64_t(start) * kSector;
        if (offset + length > image_size) {
          return "The disc image is incomplete (the file is shorter than the disc).";
        }
        files.push_back({dir.prefix + name, offset, length});
      }
      if (left) {
        stack.push_back(uint32_t(left) * 4);
      }
      if (right) {
        stack.push_back(uint32_t(right) * 4);
      }
    }
  }
  return {};
}

std::string ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& data) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxXexSize) {
    return "default.xex could not be read.";
  }
  std::ifstream in(path, std::ios::binary);
  data.resize(size_t(size));
  if (!in || !ReadAt(in, 0, data.data(), data.size())) {
    return "default.xex could not be read.";
  }
  return {};
}

}  // namespace

std::string SizeText(uint64_t bytes) {
  // Binary units, as Explorer shows drive space.
  if (bytes >= (1ull << 30)) {
    return fmt::format("{:.2f} GB", double(bytes) / double(1ull << 30));
  }
  return fmt::format("{} MB", (bytes + (1ull << 20) - 1) >> 20);
}

std::string CheckDisc(const std::filesystem::path& image, Disc& disc) {
  disc = {};
  std::error_code ec;
  const uint64_t image_size = std::filesystem::file_size(image, ec);
  std::ifstream in(image, std::ios::binary);
  if (ec || !in) {
    return "The file could not be opened.";
  }
  std::vector<DiscFile> all;
  if (std::string error = ReadFileTable(in, image_size, all); !error.empty()) {
    return error;
  }
  const DiscFile* xex = nullptr;
  for (const DiscFile& file : all) {
    if (Lower(file.path) == "default.xex") {
      xex = &file;
    }
  }
  if (!xex) {
    return "This disc image has no default.xex.";
  }
  if (xex->size > kMaxXexSize) {
    return "default.xex on this disc is not an Xbox 360 executable.";
  }
  std::vector<uint8_t> data(size_t(xex->size));
  if (!ReadAt(in, xex->offset, data.data(), data.size())) {
    return "The disc image could not be read.";
  }
  if (std::string error = CheckXex(data); !error.empty()) {
    return error;
  }

  disc.image = image;
  const DiscFile xex_file = *xex;
  for (DiscFile& file : all) {
    if (Lower(file.path).starts_with(kSkippedDir) || Lower(file.path) == "default.xex") {
      continue;
    }
    disc.total_bytes += file.size;
    disc.files.push_back(std::move(file));
  }
  // Disc order reads the image front to back; default.xex goes last, so a
  // folder only looks installed once everything else is there.
  std::sort(disc.files.begin(), disc.files.end(),
            [](const DiscFile& a, const DiscFile& b) { return a.offset < b.offset; });
  disc.files.push_back(xex_file);
  disc.total_bytes += xex_file.size;
  REXLOG_INFO("Installer: {} is Fracture ({} files, {})", PathText(image), disc.files.size(),
              SizeText(disc.total_bytes));
  return {};
}

std::string CheckGameFolder(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(dir / "default.xex", ec)) {
    return "This folder has no default.xex. Choose the folder that contains default.xex and "
           "the resources folder.";
  }
  if (!std::filesystem::is_directory(dir / "resources", ec)) {
    return "This folder has no resources folder, so the game files are incomplete.";
  }
  std::vector<uint8_t> data;
  if (std::string error = ReadWholeFile(dir / "default.xex", data); !error.empty()) {
    return error;
  }
  return CheckXex(data);
}

bool HasGameData(const std::filesystem::path& dir) {
  std::error_code ec;
  return !dir.empty() && std::filesystem::is_regular_file(dir / "default.xex", ec);
}

bool IsUnfinishedInstall(const std::filesystem::path& dir) {
  std::error_code ec;
  return !dir.empty() && std::filesystem::is_regular_file(dir / kMarker, ec);
}

std::filesystem::path UnfinishedDisc(const std::filesystem::path& dir) {
  if (!IsUnfinishedInstall(dir)) {
    return {};
  }
  std::ifstream marker(dir / kMarker);
  std::string line;
  while (std::getline(marker, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.starts_with("disc=")) {
      std::error_code ec;
      std::filesystem::path disc = PathFromText(std::string_view(line).substr(5));
      return std::filesystem::is_regular_file(disc, ec) ? disc : std::filesystem::path();
    }
  }
  return {};
}

std::filesystem::path InstallTarget(const std::filesystem::path& folder) {
  std::error_code ec;
  auto ours = [&](const std::filesystem::path& dir) {
    return !std::filesystem::exists(dir, ec) ||
           (std::filesystem::is_directory(dir, ec) &&
            (std::filesystem::is_empty(dir, ec) || HasGameData(dir) ||
             std::filesystem::exists(dir / kMarker, ec)));
  };
  return ours(folder) ? folder : folder / "Fracture";
}

std::optional<uint64_t> FreeSpace(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::path dir = path;
  while (!dir.empty() && !std::filesystem::exists(dir, ec)) {
    if (dir == dir.parent_path()) {
      return std::nullopt;
    }
    dir = dir.parent_path();
  }
  const auto info = std::filesystem::space(dir, ec);
  if (ec) {
    return std::nullopt;
  }
  return info.available;
}

Result Extract(const Disc& disc, const std::filesystem::path& target, Progress& progress,
               std::string& error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<fs::path> written;  // files this run created
  std::vector<fs::path> created;  // folders this run created, outermost first

  auto make_dirs = [&](const fs::path& dir) -> bool {
    std::vector<fs::path> missing;
    for (fs::path p = dir; !p.empty() && !fs::exists(p, ec); p = p.parent_path()) {
      missing.push_back(p);
      if (p == p.parent_path()) {
        break;
      }
    }
    if (!fs::create_directories(dir, ec) && ec) {
      error = fmt::format("Could not create the folder {} ({}).", PathText(dir), ec.message());
      return false;
    }
    created.insert(created.end(), missing.rbegin(), missing.rend());
    return true;
  };
  auto remove_created_dirs = [&]() {
    for (auto it = created.rbegin(); it != created.rend(); ++it) {
      fs::remove(*it, ec);  // only empty folders go
    }
  };
  auto clean_up = [&]() {
    for (auto it = written.rbegin(); it != written.rend(); ++it) {
      fs::remove(*it, ec);
    }
    remove_created_dirs();
    // The marker stays while files of an earlier run are left.
    bool only_marker = true;
    for (const auto& entry : fs::directory_iterator(target, ec)) {
      if (entry.path().filename() != kMarker) {
        only_marker = false;
        break;
      }
    }
    if (only_marker) {
      fs::remove(target / kMarker, ec);
      remove_created_dirs();
    }
  };
  auto fail = [&](std::string message) {
    error = std::move(message);
    REXLOG_ERROR("Installer: {}", error);
    clean_up();
    return Result::kFailed;
  };

  if (!make_dirs(target)) {
    REXLOG_ERROR("Installer: {}", error);
    return Result::kFailed;
  }
  {
    std::ofstream marker(target / kMarker, std::ios::binary | std::ios::trunc);
    if (!marker) {
      return fail(fmt::format("Could not write to {}. Choose another folder.", PathText(target)));
    }
    marker << "Fracture install in progress. Start fracture.exe to finish it.\n"
           << "disc=" << PathText(disc.image) << "\n";
  }
  std::ifstream in(disc.image, std::ios::binary);
  if (!in) {
    return fail("The disc image could not be opened.");
  }
  REXLOG_INFO("Installer: extracting {} files into {}", disc.files.size(), PathText(target));

  std::vector<char> buffer(kCopyChunk);
  for (const DiscFile& file : disc.files) {
    if (progress.cancel.load()) {
      clean_up();
      REXLOG_INFO("Installer: cancelled");
      return Result::kCancelled;
    }
    const fs::path destination = target / PathFromText(file.path);
    if (fs::is_regular_file(destination, ec) && fs::file_size(destination, ec) == file.size) {
      progress.done_bytes += file.size;
      ++progress.done_files;
      continue;
    }
    if (!make_dirs(destination.parent_path())) {
      return fail(error);
    }
    fs::path part = destination;
    part += ".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) {
      return fail(fmt::format("Could not create {}.", PathText(part)));
    }
    written.push_back(part);
    in.clear();
    in.seekg(std::streamoff(file.offset));
    uint64_t remaining = file.size;
    while (remaining) {
      if (progress.cancel.load()) {
        out.close();
        clean_up();
        REXLOG_INFO("Installer: cancelled");
        return Result::kCancelled;
      }
      const size_t chunk = size_t(std::min<uint64_t>(remaining, buffer.size()));
      in.read(buffer.data(), std::streamsize(chunk));
      if (in.gcount() != std::streamsize(chunk)) {
        out.close();
        return fail(fmt::format("The disc image could not be read ({}).", file.path));
      }
      out.write(buffer.data(), std::streamsize(chunk));
      if (!out) {
        out.close();
        return fail(fmt::format("Could not write {}. Is the drive full?", PathText(destination)));
      }
      remaining -= chunk;
      progress.done_bytes += chunk;
    }
    out.close();
    if (!out) {
      return fail(fmt::format("Could not write {}. Is the drive full?", PathText(destination)));
    }
    fs::rename(part, destination, ec);
    if (ec) {
      return fail(fmt::format("Could not create {} ({}).", PathText(destination), ec.message()));
    }
    written.back() = destination;
    ++progress.done_files;
  }
  fs::remove(target / kMarker, ec);
  REXLOG_INFO("Installer: done");
  return Result::kDone;
}

}  // namespace fracture::installer
