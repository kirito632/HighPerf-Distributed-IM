#pragma once

#include <string>

namespace im::auth {

// Builds a JWKS document containing one RSA signing key.
// Throws std::runtime_error if the PEM cannot be parsed or is not RSA.
std::string BuildJwksJson(const std::string& public_key_pem, const std::string& key_id);

}  // namespace im::auth
