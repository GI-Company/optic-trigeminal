#pragma once
#include "decoder_contract.h"
#include <map>
#include <memory>

class ProtoVoice : public DecoderContract {
private:
    std::map<std::string, std::string> slot_templates;
    std::map<std::string, std::map<std::string, std::string>> session_context;
    
    std::string fill_template(const std::string& template_text, 
                             const std::map<std::string, std::string>& slots) const;
    
    std::string extract_slot_value(const std::string& fact_text, 
                                  const std::string& slot_name) const;
    
    std::string detect_intent_slot(const std::string& intent_type, 
                                   const std::string& prompt) const;

public:
    ProtoVoice();
    
    DecoderOutput decode(const DecoderInput& input) override;
    
    void register_slot_template(const std::string& slot_name, 
                               const std::string& template_text) override;
    
    void set_session_context(const std::string& session_id, 
                            const std::string& key, 
                            const std::string& value) override;
    
    std::string get_session_value(const std::string& session_id, 
                                 const std::string& key) const;
};
