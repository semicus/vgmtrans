/*
 * VGMTrans (c) 2026
 * Licensed under the zlib license; see LICENSE.
 */
#pragma once

#include "ModuleModel.h"

#include <filesystem>
#include <vector>

namespace conversion {

// Independent implementation of the documented Furnace v228 binary layout.
// No Furnace implementation code or source-format parser dependencies.
class FurWriter {
public:
  static constexpr std::uint16_t version = 228;

  // Throws invalid_argument for unsupported/invalid models, length_error for
  // binary size overflow. Validation completes before opening an output file.
  static std::vector<std::uint8_t> serialize(const module::Song& song);
  // Additionally throws runtime_error for file I/O failure.
  static void write(const module::Song& song, const std::filesystem::path& path);
};

}  // namespace conversion
