#include "response_pipeline.h"
#include "neural_components.h"
#include <algorithm>
#include <iostream>

ResponsePipeline::ResponsePipeline(OpticTrigeminal* graph, OpticEmbedder* emb)
    : knowledge_graph(graph), embedder(emb), decoder(nullptr) {
}

void ResponsePipeline::set_decoder(std::unique_ptr<DecoderContract> dec) {
    decoder = std::move(dec);
}

std::string ResponsePipeline::detect_intent(const std::string& prompt) const {
    if (prompt.find("what") != std::string::npos && 
        prompt.find("name") != std::string::npos) {
        return "identity_query";
    }
    if ((prompt.find("my name") != std::string::npos || 
         prompt.find("i am") != std::string::npos ||
         prompt.find("call me") != std::string::npos) &&
        (prompt.find("is") != std::string::npos || 
         prompt.find("am") != std::string::npos)) {
        return "identity_statement";
    }
    if (prompt.find("what") != std::string::npos ||
        prompt.find("who") != std::string::npos ||
        prompt.find("where") != std::string::npos ||
        prompt.find("when") != std::string::npos ||
        prompt.find("why") != std::string::npos ||
        prompt.find("how") != std::string::npos) {
        return "question";
    }
    return "statement";
}

std::vector<std::pair<std::string, float>> ResponsePipeline::retrieve_relevant_facts(
    const std::string& prompt,
    const Embedding& prompt_embedding,
    int top_k) const {
    
    std::vector<std::pair<std::string, float>> results;
    
    if (!knowledge_graph) {
        return results;
    }
    
    auto related = knowledge_graph->find_related_concepts(prompt_embedding, top_k);
    for (const auto& [concept, relevance] : related) {
        results.push_back({concept, relevance});
    }
    
    return results;
}

std::vector<std::pair<std::string, float>> ResponsePipeline::retrieve_active_concepts(
    const Embedding& embedding,
    int top_k) const {
    
    if (!knowledge_graph) {
        return {};
    }
    
    return knowledge_graph->find_related_concepts(embedding, top_k);
}

DecoderOutput ResponsePipeline::process(const std::string& session_id,
                                        const std::string& prompt,
                                        const std::vector<std::pair<std::string, float>>& episodic_facts) {
    
    if (!decoder) {
        DecoderOutput fallback;
        fallback.response_text = "Decoder not initialized.";
        fallback.confidence = 0.0f;
        fallback.generation_method = "error";
        return fallback;
    }
    
    std::string intent = detect_intent(prompt);
    
    Embedding prompt_emb = embedder ? embedder->embed(prompt) : Embedding(EMBEDDING_DIM);
    
    std::vector<std::pair<std::string, float>> retrieved_facts = 
        retrieve_relevant_facts(prompt, prompt_emb, 3);
    
    if (episodic_facts.size() > 0) {
        for (size_t i = 0; i < episodic_facts.size(); i++) {
            retrieved_facts.push_back(episodic_facts[i]);
        }
    }
    
    std::sort(retrieved_facts.rbegin(), retrieved_facts.rend(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (retrieved_facts.size() > 5) {
        retrieved_facts.resize(5);
    }
    
    std::vector<std::pair<std::string, float>> active_concepts = 
        retrieve_active_concepts(prompt_emb, 5);
    
    DecoderInput decoder_input;
    decoder_input.session_id = session_id;
    decoder_input.intent_type = intent;
    decoder_input.prompt = prompt;
    decoder_input.retrieved_facts = retrieved_facts;
    decoder_input.active_concepts = active_concepts;
    decoder_input.slot_context = "";
    
    return decoder->decode(decoder_input);
}
