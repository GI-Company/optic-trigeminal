#pragma once
#include <string>
#include <vector>
#include <optional>
#include "types.h"

struct DecoderInput {
    std::string session_id;
    std::string intent_type;
    std::string prompt;
    std::vector<std::pair<std::string, float>> retrieved_facts;
    std::vector<std::pair<std::string, float>> active_concepts;
    std::string slot_context;
};

struct DecoderOutput {
    std::string response_text;
    float confidence;
    std::vector<std::string> provenance;
    std::string generation_method;
};

class DecoderContract {
public:
    virtual ~DecoderContract() = default;
    
    virtual DecoderOutput decode(const DecoderInput& input) = 0;
    
    virtual void register_slot_template(const std::string& slot_name, 
                                       const std::string& template_text) = 0;
    
    virtual void set_session_context(const std::string& session_id, 
                                     const std::string& key, 
                                     const std::string& value) = 0;
};
