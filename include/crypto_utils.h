#pragma once

#include <string>
#include <cstdint>

// Self-contained crypto helpers (SHA-256, salted password hashing, secure
// random bytes) so auth doesn't rely on plaintext comparison or predictable
// counter-based tokens. No external dependency: SHA-256 is implemented
// in-house and randomness comes from the OS CSPRNG (/dev/urandom on POSIX).

namespace Crypto {

std::string sha256_hex(const std::string& input);

// Cryptographically-random bytes, hex-encoded. Reads from /dev/urandom.
std::string random_hex(size_t num_bytes);

// Salted, iterated SHA-256 password hash, stored as "salt_hex$hash_hex".
std::string hash_password(const std::string& password);

// Constant-time-ish verification against a "salt_hex$hash_hex" stored hash.
bool verify_password(const std::string& password, const std::string& stored);

} // namespace Crypto
