#pragma once
#include <string>
#include <map>
#include <vector>
#include <regex>

class EntityExtractor {
public:
    static std::map<std::string, std::string> extract_from_identity_statement(const std::string& prompt) {
        std::map<std::string, std::string> entities;
        
        std::string lower = prompt;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        std::regex name_pattern(R"((my\s+name\s+is|i\s+am|call\s+me|i'm)\s+([a-zA-Z]+))");
        std::smatch match;
        if (std::regex_search(prompt, match, name_pattern)) {
            if (match.size() > 2) {
                entities["name"] = match[2].str();
            }
        }
        
        std::regex age_pattern(R"((i\s+am|i'm)\s+(\d+)\s+years?\s+old)");
        if (std::regex_search(prompt, match, age_pattern)) {
            if (match.size() > 2) {
                entities["age"] = match[2].str();
            }
        }
        
        return entities;
    }
    
    static std::string extract_query_slot(const std::string& prompt) {
        std::string lower = prompt;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        if (lower.find("name") != std::string::npos) {
            return "name";
        }
        if (lower.find("age") != std::string::npos) {
            return "age";
        }
        if (lower.find("who") != std::string::npos) {
            return "identity";
        }
        
        return "";
    }
};
