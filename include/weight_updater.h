#pragma once

#include "neural_components.h"
#include "training_stages.h"
#include <vector>

class NeuralWeightUpdater {
public:
    NeuralWeightUpdater();
    
    void update_stem_classifier_weights(StemClassifier& classifier, 
                                       const std::vector<float>& weights,
                                       const std::vector<float>& biases);
    
    void update_optic_embedder_weights(OpticEmbedder& embedder,
                                      const std::vector<float>& weights,
                                      const std::vector<float>& biases);
    
    void update_vta_predictor_weights(VTAPredictor& predictor,
                                     const std::vector<float>& weights,
                                     const std::vector<float>& biases);
    
    void update_sequence_decoder_weights(SequenceDecoder& decoder,
                                        const std::vector<float>& weights,
                                        const std::vector<float>& biases);
    
    std::vector<float> extract_weights_from_checkpoint(const CheckpointData& checkpoint);
    
    std::vector<float> extract_biases_from_checkpoint(const CheckpointData& checkpoint);
    
    void apply_checkpoint_to_modules(const CheckpointData& checkpoint,
                                     StemClassifier* stem_clf = nullptr,
                                     OpticEmbedder* embedder = nullptr,
                                     VTAPredictor* vta = nullptr,
                                     SequenceDecoder* decoder = nullptr);
    
    void blend_weights(const std::vector<float>& old_weights,
                      const std::vector<float>& new_weights,
                      std::vector<float>& blended_weights,
                      float blend_factor = 0.1f);
    
    bool validate_weight_dimensions(const std::vector<float>& weights, int expected_size);
    
private:
    float learning_rate;
    int update_count;
    
    std::vector<float> construct_combined_weights(const std::vector<float>& w1, 
                                                  const std::vector<float>& w2);
};
