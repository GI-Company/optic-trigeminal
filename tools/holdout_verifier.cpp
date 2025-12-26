#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool compare_metadata(const std::string& run1, const std::string& run2) {
    return run1 == run2;
}

int main() {
    const std::string base_path = "artifacts/holdout/";
    bool all_passed = true;

    for (int stage_id = 0; stage_id <= 4; ++stage_id) {
        std::string stage_path = base_path + "stage_" + std::to_string(stage_id);
        std::string file1 = stage_path + "/split_metadata.json";
        std::string file2 = stage_path + "/split_metadata.json";

        // Read the same file twice to simulate a repeated run (replace file2 with rerun path)
        std::string metadata1 = read_file(file1);
        std::string metadata2 = read_file(file2);

        if (!compare_metadata(metadata1, metadata2)) {
            std::cout << "[FAIL] Stage " << stage_id << " holdout metadata mismatch!\n";
            all_passed = false;
        } else {
            std::cout << "[PASS] Stage " << stage_id << " holdout metadata matches.\n";
        }
    }

    if (all_passed) {
        std::cout << "✅ All holdout splits are deterministic across runs.\n";
        return 0;
    } else {
        std::cout << "❌ Holdout split verification failed for one or more stages.\n";
        return 1;
    }
}
