#pragma once
#include <string>
#include <vector>
#include "evidence.h" // Include Evidence struct

class OpticTrigeminal; // Forward declaration

class ProtoVoiceDecoder {
public:
    ProtoVoiceDecoder() = default;
    ~ProtoVoiceDecoder() = default;
    
    std::string emit(const Evidence& e) const {
        if (e.slot == "name") {
            return "Your name is " + e.value + ".";
        }
        return "I do not have enough information.";
    }

    void mute() {
    }
    
    void set_graph(OpticTrigeminal* graph) {
    }
    
    std::string generate_from_knowledge(const std::string& prompt, int max_tokens, float temperature) {
        return prompt + " [generated]";
    }
    
    std::string generate(const std::string& prompt, int max_tokens, float temperature, int top_k) {
        return prompt + " [generated]";
    }
    
    void set_vocabulary(const std::vector<std::string>& vocab) {
    }
    
    void update_on_feedback(const std::string& prompt, const std::string& response, float learning_rate) {
    }
    
    void learn_sequence(const std::string& input, const std::string& output) {
    }
    
    std::string generate_from_embeddings(const std::vector<float>& embeddings, int max_tokens, float temperature) {
        return "[generated from embeddings]";
    }
};