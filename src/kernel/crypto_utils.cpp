#include "crypto_utils.h"

#include <array>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace Crypto {
namespace {

// SHA-256 implementation per FIPS 180-4.
class Sha256 {
public:
  Sha256() { reset(); }

  void reset() {
    h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    buffer_.clear();
    total_len_ = 0;
  }

  void update(const uint8_t* data, size_t len) {
    total_len_ += len;
    buffer_.insert(buffer_.end(), data, data + len);
    while (buffer_.size() >= 64) {
      process_block(buffer_.data());
      buffer_.erase(buffer_.begin(), buffer_.begin() + 64);
    }
  }

  std::array<uint8_t, 32> finalize() {
    uint64_t bit_len = total_len_ * 8;
    buffer_.push_back(0x80);
    while (buffer_.size() % 64 != 56) buffer_.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
      buffer_.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }
    while (buffer_.size() >= 64) {
      process_block(buffer_.data());
      buffer_.erase(buffer_.begin(), buffer_.begin() + 64);
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>((h_[i] >> 24) & 0xFF);
      out[i * 4 + 1] = static_cast<uint8_t>((h_[i] >> 16) & 0xFF);
      out[i * 4 + 2] = static_cast<uint8_t>((h_[i] >> 8) & 0xFF);
      out[i * 4 + 3] = static_cast<uint8_t>(h_[i] & 0xFF);
    }
    return out;
  }

private:
  static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

  void process_block(const uint8_t* block) {
    static const uint32_t k[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
             (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
             (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

    for (int i = 0; i < 64; ++i) {
      uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
      uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;

      hh = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }

    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  std::array<uint32_t, 8> h_;
  std::vector<uint8_t> buffer_;
  uint64_t total_len_ = 0;
};

std::string to_hex(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += digits[(data[i] >> 4) & 0xF];
    out += digits[data[i] & 0xF];
  }
  return out;
}

} // namespace

std::string sha256_hex(const std::string& input) {
  Sha256 ctx;
  ctx.update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  auto digest = ctx.finalize();
  return to_hex(digest.data(), digest.size());
}

std::string random_hex(size_t num_bytes) {
  std::vector<uint8_t> buf(num_bytes);
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  bool ok = false;
  if (urandom.is_open()) {
    urandom.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(num_bytes));
    ok = urandom.gcount() == static_cast<std::streamsize>(num_bytes);
  }
  if (!ok) {
    // Fallback (should not normally be hit on POSIX systems): seed a
    // non-deterministic engine as best-effort randomness.
    std::random_device rd;
    std::mt19937_64 gen(rd() ^ static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : buf) b = static_cast<uint8_t>(dist(gen));
  }
  return to_hex(buf.data(), buf.size());
}

std::string hash_password(const std::string& password) {
  std::string salt = random_hex(16);
  // Iterated hashing (poor-man's KDF) to slow down brute force; a real
  // deployment should swap this for bcrypt/argon2, but this project has a
  // hard "zero external dependencies" rule so we don't link one in.
  std::string h = salt + password;
  for (int i = 0; i < 100000; ++i) {
    h = sha256_hex(h + salt);
  }
  return salt + "$" + h;
}

bool verify_password(const std::string& password, const std::string& stored) {
  size_t sep = stored.find('$');
  if (sep == std::string::npos) return false;
  std::string salt = stored.substr(0, sep);
  std::string expected_hash = stored.substr(sep + 1);

  std::string h = salt + password;
  for (int i = 0; i < 100000; ++i) {
    h = sha256_hex(h + salt);
  }

  if (h.size() != expected_hash.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < h.size(); ++i) {
    diff |= static_cast<unsigned char>(h[i]) ^ static_cast<unsigned char>(expected_hash[i]);
  }
  return diff == 0;
}

} // namespace Crypto
