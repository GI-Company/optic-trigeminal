#pragma once

#include <string>
#include <vector>

namespace Groq {

struct ChatMessage {
    std::string role;
    std::string content;
};

class GroqClient {
public:
    GroqClient();
    ~GroqClient();

    // Sends a chat completion request to Groq API. Groq deprecates models
    // routinely -- verify against GET https://api.groq.com/openai/v1/models
    // before trusting any hardcoded id here is still live (llama3-8b-8192,
    // the previous default, was decommissioned).
    std::string chat_completion(const std::vector<ChatMessage>& messages,
                                const std::string& model = "openai/gpt-oss-120b",
                                float temperature = 0.7f,
                                int max_tokens = 1024);

private:
    std::string api_key_;
    std::string api_url_;

    // Helper to send request via system curl
    std::string send_curl_request(const std::string& json_payload);
    
    // Naive JSON extraction
    std::string extract_json_string(const std::string& str);
    std::string extract_chat_content(const std::string& json_response);
};

} // namespace Groq
