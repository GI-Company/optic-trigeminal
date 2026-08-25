#include "proto_voice.h"
#include <sstream>
#include <algorithm>
#include <iostream>

ProtoVoice::ProtoVoice() {
    register_slot_template("identity_query_name", "Your name is {name}.");
    register_slot_template("identity_query_unknown", "I do not know your name yet.");
    register_slot_template("identity_statement", "I will remember that your name is {name}.");
    register_slot_template("general_query_unknown", "I do not have information about that.");
}

void ProtoVoice::register_slot_template(const std::string& slot_name, 
                                       const std::string& template_text) {
    slot_templates[slot_name] = template_text;
}

void ProtoVoice::set_session_context(const std::string& session_id, 
                                    const std::string& key, 
                                    const std::string& value) {
    session_context[session_id][key] = value;
}

std::string ProtoVoice::get_session_value(const std::string& session_id, 
                                         const std::string& key) const {
    auto it = session_context.find(session_id);
    if (it != session_context.end()) {
        auto kv = it->second.find(key);
        if (kv != it->second.end()) {
            return kv->second;
        }
    }
    return "";
}

std::string ProtoVoice::fill_template(const std::string& template_text, 
                                     const std::map<std::string, std::string>& slots) const {
    std::string result = template_text;
    for (const auto& [key, value] : slots) {
        std::string placeholder = "{" + key + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

std::string ProtoVoice::extract_slot_value(const std::string& fact_text, 
                                          const std::string& slot_name) const {
    if (slot_name == "name") {
        size_t name_pos = fact_text.find("name is ");
        if (name_pos != std::string::npos) {
            size_t start = name_pos + 8;
            size_t end = fact_text.find(".", start);
            if (end == std::string::npos) end = fact_text.length();
            return fact_text.substr(start, end - start);
        }
    }
    return "";
}

std::string ProtoVoice::detect_intent_slot(const std::string& intent_type, 
                                          const std::string& prompt) const {
    if (intent_type == "identity_query") {
        if (prompt.find("name") != std::string::npos) {
            return "name";
        }
    } else if (intent_type == "identity_statement") {
        if (prompt.find("name") != std::string::npos) {
            return "name";
        }
    }
    return "";
}

DecoderOutput ProtoVoice::decode(const DecoderInput& input) {
    DecoderOutput output;
    output.generation_method = "proto_voice_rule_based";
    output.provenance.clear();
    
    std::string slot_name = detect_intent_slot(input.intent_type, input.prompt);
    
    if (input.intent_type == "identity_query") {
        if (slot_name == "name") {
            std::string stored_name = get_session_value(input.session_id, "name");
            
            if (!stored_name.empty()) {
                std::map<std::string, std::string> slots = {{"name", stored_name}};
                output.response_text = fill_template(slot_templates["identity_query_name"], slots);
                output.confidence = 0.99f;
                output.provenance.push_back("session:" + input.session_id + ":name");
            } else if (!input.retrieved_facts.empty()) {
                std::string fact_value = extract_slot_value(input.retrieved_facts[0].first, "name");
                if (!fact_value.empty()) {
                    std::map<std::string, std::string> slots = {{"name", fact_value}};
                    output.response_text = fill_template(slot_templates["identity_query_name"], slots);
                    output.confidence = input.retrieved_facts[0].second;
                    output.provenance.push_back("memory:" + input.retrieved_facts[0].first);
                } else {
                    output.response_text = slot_templates["identity_query_unknown"];
                    output.confidence = 0.5f;
                }
            } else {
                output.response_text = slot_templates["identity_query_unknown"];
                output.confidence = 0.5f;
            }
        }
    } else if (input.intent_type == "identity_statement") {
        if (slot_name == "name") {
            std::string extracted_name;
            size_t name_pos = input.prompt.find("name");
            if (name_pos != std::string::npos) {
                size_t is_pos = input.prompt.find("is", name_pos);
                if (is_pos != std::string::npos) {
                    size_t start = is_pos + 2;
                    while (start < input.prompt.length() && input.prompt[start] == ' ') start++;
                    size_t end = input.prompt.find(".", start);
                    if (end == std::string::npos) end = input.prompt.length();
                    extracted_name = input.prompt.substr(start, end - start);
                }
            }
            
            if (!extracted_name.empty()) {
                set_session_context(input.session_id, "name", extracted_name);
                std::map<std::string, std::string> slots = {{"name", extracted_name}};
                output.response_text = fill_template(slot_templates["identity_statement"], slots);
                output.confidence = 0.95f;
                output.provenance.push_back("parsed:" + input.prompt);
            } else {
                output.response_text = "I did not understand your name. Please tell me again.";
                output.confidence = 0.6f;
            }
        }
    } else {
        if (!input.retrieved_facts.empty()) {
            // Present the retrieved answer directly rather than wrapped in
            // "I understand: {fact}." -- that template implies the system
            // parsed the user's own statement back to them, which is the
            // wrong framing for a Q&A response, and the retrieved text
            // already reads as a complete answer on its own (it comes from
            // real training-corpus content via episodic-memory retrieval,
            // not something generated). Below the retrieval threshold that
            // filters results at all (0.3, see
            // retrieve_episodic_memory_by_session), a match is genuinely
            // weak -- a soft "closest match" qualifier keeps that honest
            // instead of presenting a shaky match with full confidence.
            float conf = input.retrieved_facts[0].second;
            if (conf >= 0.5f) {
                output.response_text = input.retrieved_facts[0].first;
            } else {
                output.response_text = "Closest related information found: " + input.retrieved_facts[0].first;
            }
            output.confidence = conf;
            output.provenance.push_back("memory:" + input.retrieved_facts[0].first);
        } else {
            output.response_text = slot_templates["general_query_unknown"];
            output.confidence = 0.3f;
        }
    }
    
    return output;
}
