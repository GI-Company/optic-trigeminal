#pragma once
#include <string>
#include <vector>
#include <memory>
#include "decoder_contract.h"
#include "types.h"

class OpticTrigeminal;
class OpticEmbedder;

class ResponsePipeline {
private:
    std::unique_ptr<DecoderContract> decoder;
    OpticTrigeminal* knowledge_graph;
    OpticEmbedder* embedder;
    
    std::string detect_intent(const std::string& prompt) const;
    
    std::vector<std::pair<std::string, float>> retrieve_relevant_facts(
        const std::string& prompt,
        const Embedding& prompt_embedding,
        int top_k = 3) const;
    
    std::vector<std::pair<std::string, float>> retrieve_active_concepts(
        const Embedding& embedding,
        int top_k = 5) const;

public:
    ResponsePipeline(OpticTrigeminal* graph, OpticEmbedder* emb);
    
    void set_decoder(std::unique_ptr<DecoderContract> dec);
    
    DecoderOutput process(const std::string& session_id,
                         const std::string& prompt,
                         const std::vector<std::pair<std::string, float>>& episodic_facts);
};
