#include "groq_client.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <fstream>

namespace Groq {

GroqClient::GroqClient() {
    api_url_ = "https://api.groq.com/openai/v1/chat/completions";
    
    const char* env_key = std::getenv("GROQ_API_KEY");
    if (env_key && std::string(env_key) != "") {
        api_key_ = std::string(env_key);
        return;
    }
    
    std::ifstream file(".groq_api_key");
    if (file.is_open()) {
        std::getline(file, api_key_);
        while (!api_key_.empty() && (api_key_.back() == '\n' || api_key_.back() == '\r' || api_key_.back() == ' ')) {
            api_key_.pop_back();
        }
    }
}

GroqClient::~GroqClient() {
}

std::string GroqClient::extract_json_string(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c;
        }
    }
    return escaped;
}

std::string GroqClient::send_curl_request(const std::string& json_payload) {
    if (api_key_.empty()) {
        return "Internal Error: GROQ_API_KEY environment variable not set.";
    }

    // Write payload to a temporary file to avoid command line length limits and quoting issues
    std::string tmp_file = "/tmp/groq_payload_XXXXXX";
    int fd = mkstemp(&tmp_file[0]);
    if (fd == -1) {
        return "Internal Error: Failed to create temp file for curl payload.";
    }
    
    FILE* fp = fdopen(fd, "w");
    if (!fp) {
        return "Internal Error: Failed to open temp file.";
    }
    fwrite(json_payload.c_str(), 1, json_payload.length(), fp);
    fclose(fp);

    std::string cmd = "curl -s -X POST " + api_url_ + 
                      " -H \"Authorization: Bearer " + api_key_ + "\"" +
                      " -H \"Content-Type: application/json\"" +
                      " -d @" + tmp_file;

    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        remove(tmp_file.c_str());
        return "Internal Error: Failed to run curl command.";
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        result += buffer;
    }
    
    remove(tmp_file.c_str());
    return result;
}

std::string GroqClient::chat_completion(const std::vector<ChatMessage>& messages, 
                                        const std::string& model, 
                                        float temperature, 
                                        int max_tokens) {
    std::ostringstream json_payload;
    json_payload << "{";
    json_payload << "\"model\": \"" << model << "\", ";
    json_payload << "\"temperature\": " << temperature << ", ";
    if (max_tokens > 0) {
        json_payload << "\"max_tokens\": " << max_tokens << ", ";
    }
    
    json_payload << "\"messages\": [";
    for (size_t i = 0; i < messages.size(); ++i) {
        json_payload << "{\"role\": \"" << messages[i].role << "\", \"content\": \"" 
                     << extract_json_string(messages[i].content) << "\"}";
        if (i < messages.size() - 1) {
            json_payload << ", ";
        }
    }
    json_payload << "]}";

    std::string response = send_curl_request(json_payload.str());
    if (response.empty() || response.find("Internal Error") != std::string::npos) {
        return response; // Return the error directly
    }

    return extract_chat_content(response);
}

std::string GroqClient::extract_chat_content(const std::string& json_response) {
    std::string search_key = "\"content\":\"";
    size_t content_pos = json_response.find(search_key);
    
    if (content_pos == std::string::npos) {
        search_key = "\"content\": \"";
        content_pos = json_response.find(search_key);
    }
    
    if (content_pos == std::string::npos) {
        search_key = "\"content\":\n        \"";
        content_pos = json_response.find(search_key);
    }

    if (content_pos != std::string::npos) {
        size_t start = content_pos + search_key.length();
        std::string content;
        for (size_t i = start; i < json_response.length(); ++i) {
            if (json_response[i] == '\\' && i + 1 < json_response.length()) {
                char next = json_response[i + 1];
                if (next == 'n') content += '\n';
                else if (next == 'r') content += '\r';
                else if (next == 't') content += '\t';
                else if (next == '"') content += '"';
                else if (next == '\\') content += '\\';
                i++;
            } else if (json_response[i] == '"') {
                break;
            } else {
                content += json_response[i];
            }
        }
        return content;
    }
    
    return "Error parsing Groq response: " + json_response;
}

} // namespace Groq
