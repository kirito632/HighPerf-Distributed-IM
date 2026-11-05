#include "Jwks.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace im::auth {
namespace {

struct BioDeleter {
    void operator()(BIO* bio) const { BIO_free(bio); }
};
struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};
struct BnDeleter {
    void operator()(BIGNUM* bn) const { BN_free(bn); }
};

std::string Base64UrlEncode(const unsigned char* data, size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string out;
    out.reserve((size * 4 + 2) / 3);
    size_t i = 0;
    while (i + 3 <= size) {
        const unsigned int value = (static_cast<unsigned int>(data[i]) << 16) |
                                   (static_cast<unsigned int>(data[i + 1]) << 8) |
                                   static_cast<unsigned int>(data[i + 2]);
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        out.push_back(kAlphabet[value & 0x3F]);
        i += 3;
    }

    if (i < size) {
        unsigned int value = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < size) {
            value |= static_cast<unsigned int>(data[i + 1]) << 8;
        }
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        if (i + 1 < size) {
            out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        }
    }
    return out;
}

std::string BnToBase64Url(const BIGNUM* bn) {
    if (BN_is_zero(bn)) {
        return "AA";
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(BN_num_bytes(bn)));
    BN_bn2bin(bn, bytes.data());
    return Base64UrlEncode(bytes.data(), bytes.size());
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '"':
            case '\\':
                out.push_back('\\');
                out.push_back(c);
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

}  // namespace

std::string BuildJwksJson(const std::string& public_key_pem, const std::string& key_id) {
    if (public_key_pem.empty()) {
        throw std::runtime_error("JWKS public key must not be empty");
    }
    if (key_id.empty()) {
        throw std::runtime_error("JWKS key id must not be empty");
    }

    std::unique_ptr<BIO, BioDeleter> bio(
        BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size())));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed");
    }

    std::unique_ptr<EVP_PKEY, PkeyDeleter> key(
        PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
    if (!key) {
        throw std::runtime_error("failed to parse RSA public key PEM");
    }
    if (!EVP_PKEY_is_a(key.get(), "RSA")) {
        throw std::runtime_error("JWKS key is not an RSA key");
    }

    BIGNUM* modulus_raw = nullptr;
    BIGNUM* exponent_raw = nullptr;
    if (EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_RSA_N, &modulus_raw) != 1 ||
        EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_RSA_E, &exponent_raw) != 1) {
        BN_free(modulus_raw);
        BN_free(exponent_raw);
        throw std::runtime_error("failed to extract RSA parameters");
    }
    std::unique_ptr<BIGNUM, BnDeleter> modulus(modulus_raw);
    std::unique_ptr<BIGNUM, BnDeleter> exponent(exponent_raw);

    std::ostringstream json;
    json << R"({"keys":[{"kty":"RSA","use":"sig","alg":"RS256","kid":")"
         << JsonEscape(key_id) << R"(","n":")" << BnToBase64Url(modulus.get())
         << R"(","e":")" << BnToBase64Url(exponent.get()) << R"("}]})";
    return json.str();
}

}  // namespace im::auth
