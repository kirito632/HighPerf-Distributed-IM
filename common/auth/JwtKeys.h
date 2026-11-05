#pragma once

#include <string>

namespace im::auth {

// Reads a PEM file. Throws std::runtime_error with the path on failure.
std::string ReadPemFile(const std::string& path);

}  // namespace im::auth
