#pragma once

#include <string>
#include <functional>

namespace HoldoutValidationUtils {

/**
 * @brief Computes a deterministic hash for a given string using FNV-1a algorithm.
 *
 * This function implements the FNV-1a hash algorithm to compute a hash value
 * for the input string. This ensures cross-platform and cross-compiler
 * determinism, which is crucial for reproducible holdout splits.
 *
 * @param id The string identifier to hash (e.g., source_record_id).
 * @return A size_t hash value.
 */
inline size_t deterministic_hash(const std::string& id) {
    size_t hash = 2166136261u; // FNV-1a 32-bit offset basis
    for (char c : id) {
        hash ^= static_cast<size_t>(c);
        hash *= 16777619u; // FNV-1a prime
    }
    return hash;
}

} // namespace HoldoutValidationUtils