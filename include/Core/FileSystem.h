#pragma once

#include <filesystem>
#include <vector>

namespace plaster {

// Returns the absolute path to the directory containing the running
// executable. Used as the anchor for runtime asset lookups (shaders,
// textures, etc.) so the engine works regardless of the caller's
// current working directory.
std::filesystem::path GetExecutableDirectory();

// Reads an entire binary file into a byte buffer. Throws std::runtime_error
// if the file cannot be opened.
std::vector<char> ReadBinaryFile(const std::filesystem::path& path);

} // namespace plaster
