#include "JwtKeys.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace im::auth {

std::string ReadPemFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open PEM file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string contents = buffer.str();
    if (contents.empty()) {
        throw std::runtime_error("empty PEM file: " + path);
    }
    return contents;
}

}  // namespace im::auth
