#pragma once

#include <string>
#include <cstdint>

// Self-contained crypto helpers (SHA-256, salted password hashing, secure
// random bytes) so auth doesn't rely on plaintext comparison or predictable
// counter-based tokens. SHA-256 is implemented in-house and randomness comes
// from the OS CSPRNG (/dev/urandom on POSIX).
//
// Password hashing is the one deliberate exception to this project's
// zero-external-dependency rule: it vendors the official PHC-winning Argon2
// reference implementation (third_party/argon2/, CC0/Apache-2.0) rather than
// hand-rolling a memory-hard KDF, which is exactly the kind of primitive
// that's dangerous to DIY. See hash_password()/verify_password() below.

namespace Crypto {

std::string sha256_hex(const std::string& input);

// Cryptographically-random bytes, hex-encoded. Reads from /dev/urandom.
std::string random_hex(size_t num_bytes);

// Argon2id password hash (OWASP-baseline params: m=19456 KiB, t=2, p=1),
// returned as Argon2's own self-describing encoded string
// ("$argon2id$v=19$m=...,t=...,p=...$<salt>$<hash>").
std::string hash_password(const std::string& password);

// Verifies against either format: a stored hash starting with '$' is the
// current Argon2id format (argon2id_verify); anything else is the legacy
// "salt_hex$hash_hex" 100k-round-SHA-256 format this project used before --
// kept working so already-persisted accounts don't need a forced reset.
bool verify_password(const std::string& password, const std::string& stored);

} // namespace Crypto
