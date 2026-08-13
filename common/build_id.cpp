#include "common/build_id.hpp"

#include <windows.h>

#include <fstream>
#include <vector>

namespace live_hud {

std::optional<PeFingerprint> read_pe_fingerprint(const std::filesystem::path& pe) {
  std::ifstream in(pe, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  IMAGE_DOS_HEADER dos{};
  in.read(reinterpret_cast<char*>(&dos), sizeof(dos));
  if (!in || dos.e_magic != IMAGE_DOS_SIGNATURE) {
    return std::nullopt;
  }

  in.seekg(dos.e_lfanew, std::ios::beg);
  DWORD sig = 0;
  in.read(reinterpret_cast<char*>(&sig), sizeof(sig));
  if (!in || sig != IMAGE_NT_SIGNATURE) {
    return std::nullopt;
  }

  IMAGE_FILE_HEADER file{};
  in.read(reinterpret_cast<char*>(&file), sizeof(file));
  if (!in) {
    return std::nullopt;
  }

  // Read SizeOfImage from optional header (PE32 or PE32+).
  WORD magic = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (!in) {
    return std::nullopt;
  }

  std::uint32_t size_of_image = 0;
  if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    // Already consumed Magic (2 bytes). Remaining optional header is
    // sizeof(IMAGE_OPTIONAL_HEADER64) - sizeof(WORD).
    IMAGE_OPTIONAL_HEADER64 opt{};
    opt.Magic = magic;
    in.read(reinterpret_cast<char*>(&opt) + sizeof(WORD),
            sizeof(opt) - sizeof(WORD));
    if (!in) {
      return std::nullopt;
    }
    size_of_image = opt.SizeOfImage;
  } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    IMAGE_OPTIONAL_HEADER32 opt{};
    opt.Magic = magic;
    in.read(reinterpret_cast<char*>(&opt) + sizeof(WORD),
            sizeof(opt) - sizeof(WORD));
    if (!in) {
      return std::nullopt;
    }
    size_of_image = opt.SizeOfImage;
  } else {
    return std::nullopt;
  }

  PeFingerprint fp;
  fp.size_of_image = size_of_image;
  fp.time_date_stamp = file.TimeDateStamp;
  return fp;
}

bool fingerprint_matches(const PeFingerprint& actual,
                         std::uint32_t expected_size,
                         std::uint32_t expected_ts) {
  if (expected_size == 0 || expected_ts == 0) {
    return false;
  }
  return actual.size_of_image == expected_size &&
         actual.time_date_stamp == expected_ts;
}

}  // namespace live_hud
