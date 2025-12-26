#include "weight_updater.h"
#include <algorithm>
#include <cmath>

NeuralWeightUpdater::NeuralWeightUpdater() 
    : learning_rate(0.0001f), update_count(0) {
}

void NeuralWeightUpdater::update_stem_classifier_weights(StemClassifier& classifier,
                                                        const std::vector<float>& weights,
                                                        const std::vector<float>& biases) {
    if (weights.size() < 256 || biases.size() < 64) {
        return;
    }
    
    MatrixF w1(256, VectorF(1));
    for (size_t i = 0; i < 256 && i < weights.size(); ++i) {
        w1[i][0] = weights[i];
    }
    
    MatrixF w2(64, VectorF(1));
    for (size_t i = 0; i < 64 && i < weights.size() - 256; ++i) {
        w2[i][0] = weights[256 + i];
    }
    
    VectorF b1(64);
    for (size_t i = 0; i < 64 && i < biases.size(); ++i) {
        b1[i] = biases[i];
    }
    
    VectorF b2(16);
    for (size_t i = 0; i < 16 && i + 64 < biases.size(); ++i) {
        b2[i] = biases[64 + i];
    }
    
    classifier.set_weights(w1, w2, b1, b2);
}

void NeuralWeightUpdater::update_optic_embedder_weights(OpticEmbedder& embedder,
                                                       const std::vector<float>& weights,
                                                       const std::vector<float>& biases) {
    if (weights.size() < 256 || biases.size() < 64) {
        return;
    }
    
    MatrixF w1(256, VectorF(1));
    for (size_t i = 0; i < 256 && i < weights.size(); ++i) {
        w1[i][0] = weights[i];
    }
    
    MatrixF w2(256, VectorF(1));
    for (size_t i = 0; i < 256 && i + 256 < weights.size(); ++i) {
        w2[i][0] = weights[256 + i];
    }
    
    VectorF b1(64);
    for (size_t i = 0; i < 64 && i < biases.size(); ++i) {
        b1[i] = biases[i];
    }
    
    VectorF b2(64);
    for (size_t i = 0; i < 64 && i + 64 < biases.size(); ++i) {
        b2[i] = biases[64 + i];
    }
    
    VectorF ln_scale(64, 1.0f);
    VectorF ln_bias(64, 0.0f);
    for (size_t i = 0; i < 64 && i < biases.size(); ++i) {
        ln_scale[i] = 1.0f + (biases[i] * 0.01f);
    }
    
    embedder.set_weights(w1, w2, b1, b2, ln_scale, ln_bias);
}

void NeuralWeightUpdater::update_vta_predictor_weights(VTAPredictor& predictor,
                                                      const std::vector<float>& weights,
                                                      const std::vector<float>& biases) {
    if (weights.size() < 256) {
        return;
    }
    
    int hidden_size = 128;
    MatrixF wxh(hidden_size, VectorF(256));
    MatrixF whh(hidden_size, VectorF(hidden_size));
    MatrixF why(10000, VectorF(hidden_size));
    
    size_t idx = 0;
    for (int i = 0; i < hidden_size && idx < weights.size(); ++i) {
        for (int j = 0; j < 256 && idx < weights.size(); ++j) {
            wxh[i][j] = weights[idx++];
        }
    }
    
    for (int i = 0; i < hidden_size && idx < weights.size(); ++i) {
        for (int j = 0; j < hidden_size && idx < weights.size(); ++j) {
            whh[i][j] = weights[idx++];
        }
    }
    
    VectorF bh(hidden_size, 0.0f);
    VectorF by(10000, 0.0f);
    for (size_t i = 0; i < bh.size() && i < biases.size(); ++i) {
        bh[i] = biases[i];
    }
    
    predictor.set_weights(wxh, whh, why, bh, by);
}

void NeuralWeightUpdater::update_sequence_decoder_weights(SequenceDecoder& decoder,
                                                         const std::vector<float>& weights,
                                                         const std::vector<float>& biases) {
    if (weights.size() < 256) {
        return;
    }
    
    int hidden_size = 128;
    MatrixF wxh(hidden_size, VectorF(256));
    MatrixF whh(hidden_size, VectorF(hidden_size));
    MatrixF why(10000, VectorF(hidden_size));
    
    size_t idx = 0;
    for (int i = 0; i < hidden_size && idx < weights.size(); ++i) {
        for (int j = 0; j < 256 && idx < weights.size(); ++j) {
            wxh[i][j] = weights[idx++];
        }
    }
    
    for (int i = 0; i < hidden_size && idx < weights.size(); ++i) {
        for (int j = 0; j < hidden_size && idx < weights.size(); ++j) {
            whh[i][j] = weights[idx++];
        }
    }
    
    VectorF bh(hidden_size, 0.0f);
    VectorF by(10000, 0.0f);
    for (size_t i = 0; i < bh.size() && i < biases.size(); ++i) {
        bh[i] = biases[i];
    }
    
    decoder.set_weights(wxh, whh, why, bh, by);
}

std::vector<float> NeuralWeightUpdater::extract_weights_from_checkpoint(const CheckpointData& checkpoint) {
    return checkpoint.model_weights;
}

std::vector<float> NeuralWeightUpdater::extract_biases_from_checkpoint(const CheckpointData& checkpoint) {
    return checkpoint.model_biases;
}

void NeuralWeightUpdater::apply_checkpoint_to_modules(const CheckpointData& checkpoint,
                                                     StemClassifier* stem_clf,
                                                     OpticEmbedder* embedder,
                                                     VTAPredictor* vta,
                                                     SequenceDecoder* decoder) {
    const auto& weights = checkpoint.model_weights;
    const auto& biases = checkpoint.model_biases;
    
    if (stem_clf) {
        update_stem_classifier_weights(*stem_clf, weights, biases);
    }
    
    if (embedder) {
        update_optic_embedder_weights(*embedder, weights, biases);
    }
    
    if (vta) {
        update_vta_predictor_weights(*vta, weights, biases);
    }
    
    if (decoder) {
        update_sequence_decoder_weights(*decoder, weights, biases);
    }
    
    update_count++;
}

void NeuralWeightUpdater::blend_weights(const std::vector<float>& old_weights,
                                       const std::vector<float>& new_weights,
                                       std::vector<float>& blended_weights,
                                       float blend_factor) {
    blended_weights.clear();
    size_t size = std::min(old_weights.size(), new_weights.size());
    
    for (size_t i = 0; i < size; ++i) {
        float blended = (1.0f - blend_factor) * old_weights[i] + blend_factor * new_weights[i];
        blended_weights.push_back(blended);
    }
    
    if (new_weights.size() > old_weights.size()) {
        for (size_t i = size; i < new_weights.size(); ++i) {
            blended_weights.push_back(new_weights[i]);
        }
    }
}

bool NeuralWeightUpdater::validate_weight_dimensions(const std::vector<float>& weights, int expected_size) {
    return weights.size() >= expected_size;
}

std::vector<float> NeuralWeightUpdater::construct_combined_weights(const std::vector<float>& w1,
                                                                  const std::vector<float>& w2) {
    std::vector<float> combined = w1;
    combined.insert(combined.end(), w2.begin(), w2.end());
    return combined;
}
