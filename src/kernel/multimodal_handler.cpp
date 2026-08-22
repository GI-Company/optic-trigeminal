#include "multimodal_handler.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <chrono>

AudioProcessor::AudioProcessor() {}

Embedding AudioProcessor::extract_spectrogram_features(const AudioFrame& audio) {
    Embedding embedding(EMBEDDING_DIM);
    
    if (audio.samples.empty()) {
        return embedding;
    }
    
    std::vector<float> mfcc = apply_mfcc(audio.samples, 13);
    std::vector<float> zcr = compute_zero_crossing_rate(audio);
    std::vector<float> energy = compute_energy_envelope(audio);
    
    size_t idx = 0;
    for (size_t i = 0; i < mfcc.size() && idx < EMBEDDING_DIM; ++i) {
        embedding.values[idx++] = mfcc[i];
    }
    for (size_t i = 0; i < zcr.size() && idx < EMBEDDING_DIM; ++i) {
        embedding.values[idx++] = zcr[i];
    }
    for (size_t i = 0; i < energy.size() && idx < EMBEDDING_DIM; ++i) {
        embedding.values[idx++] = energy[i];
    }
    
    while (idx < EMBEDDING_DIM) {
        embedding.values[idx++] = 0.0f;
    }
    
    return embedding;
}

std::vector<float> AudioProcessor::apply_mfcc(const std::vector<float>& samples, int n_mfcc) {
    std::vector<float> mfcc_features(n_mfcc, 0.0f);
    
    if (samples.empty()) return mfcc_features;
    
    int frame_size = 512;
    int n_frames = std::max(1, static_cast<int>(samples.size()) / frame_size);
    
    for (int i = 0; i < n_mfcc; ++i) {
        float energy = 0.0f;
        for (int j = i * frame_size; j < (i + 1) * frame_size && j < samples.size(); ++j) {
            energy += samples[j] * samples[j];
        }
        mfcc_features[i] = std::log(energy + 1e-6f) / n_frames;
    }
    
    return mfcc_features;
}

std::vector<float> AudioProcessor::compute_zero_crossing_rate(const AudioFrame& audio) {
    std::vector<float> zcr;
    
    if (audio.samples.size() < 2) return zcr;
    
    int frame_size = 256;
    int n_frames = audio.samples.size() / frame_size;
    
    for (int i = 0; i < n_frames; ++i) {
        int zero_crossings = 0;
        for (int j = i * frame_size; j < (i + 1) * frame_size - 1; ++j) {
            if ((audio.samples[j] > 0 && audio.samples[j + 1] < 0) ||
                (audio.samples[j] < 0 && audio.samples[j + 1] > 0)) {
                zero_crossings++;
            }
        }
        zcr.push_back(static_cast<float>(zero_crossings) / frame_size);
    }
    
    return zcr;
}

std::vector<float> AudioProcessor::compute_energy_envelope(const AudioFrame& audio) {
    std::vector<float> envelope;
    
    if (audio.samples.empty()) return envelope;
    
    int frame_size = 256;
    int n_frames = audio.samples.size() / frame_size;
    
    for (int i = 0; i < n_frames; ++i) {
        float energy = 0.0f;
        for (int j = i * frame_size; j < (i + 1) * frame_size && j < audio.samples.size(); ++j) {
            energy += audio.samples[j] * audio.samples[j];
        }
        envelope.push_back(std::sqrt(energy / frame_size));
    }
    
    return envelope;
}

std::string AudioProcessor::recognize_intent_from_audio(const AudioFrame& audio) {
    if (audio.samples.empty()) {
        return "unknown_intent";
    }
    
    float energy = 0.0f;
    for (float sample : audio.samples) {
        energy += sample * sample;
    }
    energy = std::sqrt(energy / audio.samples.size());
    
    if (energy > 0.5f) {
        return "high_energy_speech";
    } else if (energy > 0.1f) {
        return "normal_speech";
    } else {
        return "low_energy_speech";
    }
}

bool AudioProcessor::detect_silence(const AudioFrame& audio, float threshold) {
    if (audio.samples.empty()) return true;
    
    float energy = 0.0f;
    for (float sample : audio.samples) {
        energy += sample * sample;
    }
    energy = std::sqrt(energy / audio.samples.size());
    
    return energy < threshold;
}

std::vector<float> AudioProcessor::apply_fft(const std::vector<float>& samples) {
    std::vector<float> fft_result(samples.size(), 0.0f);
    for (size_t i = 0; i < samples.size(); ++i) {
        fft_result[i] = std::abs(samples[i]);
    }
    return fft_result;
}

std::vector<float> AudioProcessor::create_mel_filterbank(int n_mels, int n_fft, int sample_rate) {
    std::vector<float> filterbank(n_mels, 1.0f / n_mels);
    return filterbank;
}

ImageProcessor::ImageProcessor() {}

Embedding ImageProcessor::extract_image_features(const ImageFrame& image) {
    Embedding embedding(EMBEDDING_DIM);
    
    auto histogram = compute_histogram(image, 64);
    auto edges = extract_edges(image);
    auto moments = extract_color_moments(image);
    
    size_t idx = 0;
    for (const auto& bin_row : histogram) {
        for (float val : bin_row) {
            if (idx < EMBEDDING_DIM) {
                embedding.values[idx++] = val;
            }
        }
    }
    
    for (float val : edges) {
        if (idx < EMBEDDING_DIM) {
            embedding.values[idx++] = val;
        }
    }
    
    for (float val : moments) {
        if (idx < EMBEDDING_DIM) {
            embedding.values[idx++] = val;
        }
    }
    
    while (idx < EMBEDDING_DIM) {
        embedding.values[idx++] = 0.0f;
    }
    
    return embedding;
}

std::vector<std::vector<float>> ImageProcessor::compute_histogram(const ImageFrame& image, int bins) {
    std::vector<std::vector<float>> histogram(3, std::vector<float>(bins, 0.0f));
    
    if (image.pixel_data.empty()) return histogram;
    
    int pixels = image.width * image.height;
    for (int i = 0; i < pixels && i * image.channels + 2 < image.pixel_data.size(); ++i) {
        int r = image.pixel_data[i * image.channels] * bins / 256;
        int g = image.pixel_data[i * image.channels + 1] * bins / 256;
        int b = image.pixel_data[i * image.channels + 2] * bins / 256;
        
        if (r < bins) histogram[0][r]++;
        if (g < bins) histogram[1][g]++;
        if (b < bins) histogram[2][b]++;
    }
    
    for (int c = 0; c < 3; ++c) {
        for (int b = 0; b < bins; ++b) {
            histogram[c][b] /= (pixels + 1);
        }
    }
    
    return histogram;
}

std::vector<float> ImageProcessor::extract_edges(const ImageFrame& image) {
    std::vector<float> edges;
    
    if (image.pixel_data.size() < 9) return edges;
    
    std::vector<float> gradients = compute_gradients(image);
    for (size_t i = 0; i < gradients.size() && edges.size() < 32; ++i) {
        if (gradients[i] > 0.3f) {
            edges.push_back(gradients[i]);
        }
    }
    
    while (edges.size() < 32) {
        edges.push_back(0.0f);
    }
    
    return edges;
}

std::vector<float> ImageProcessor::extract_color_moments(const ImageFrame& image) {
    std::vector<float> moments(9, 0.0f);
    
    if (image.pixel_data.empty()) return moments;
    
    int pixels = image.width * image.height;
    std::vector<float> r_vals, g_vals, b_vals;
    
    for (int i = 0; i < pixels && i * image.channels + 2 < image.pixel_data.size(); ++i) {
        r_vals.push_back(image.pixel_data[i * image.channels] / 255.0f);
        g_vals.push_back(image.pixel_data[i * image.channels + 1] / 255.0f);
        b_vals.push_back(image.pixel_data[i * image.channels + 2] / 255.0f);
    }
    
    if (!r_vals.empty()) {
        float r_mean = 0, g_mean = 0, b_mean = 0;
        for (size_t i = 0; i < r_vals.size(); ++i) {
            r_mean += r_vals[i];
            g_mean += g_vals[i];
            b_mean += b_vals[i];
        }
        r_mean /= r_vals.size();
        g_mean /= g_vals.size();
        b_mean /= b_vals.size();
        
        moments[0] = r_mean;
        moments[1] = g_mean;
        moments[2] = b_mean;
        
        float r_var = 0, g_var = 0, b_var = 0;
        for (size_t i = 0; i < r_vals.size(); ++i) {
            r_var += (r_vals[i] - r_mean) * (r_vals[i] - r_mean);
            g_var += (g_vals[i] - g_mean) * (g_vals[i] - g_mean);
            b_var += (b_vals[i] - b_mean) * (b_vals[i] - b_mean);
        }
        r_var /= r_vals.size();
        g_var /= g_vals.size();
        b_var /= b_vals.size();
        
        moments[3] = std::sqrt(r_var);
        moments[4] = std::sqrt(g_var);
        moments[5] = std::sqrt(b_var);
    }
    
    return moments;
}

std::string ImageProcessor::detect_objects_in_image(const ImageFrame& image) {
    if (image.pixel_data.empty()) return "no_objects";
    return "generic_objects_detected";
}

std::vector<std::string> ImageProcessor::extract_text_from_image(const ImageFrame& image) {
    std::vector<std::string> text;
    if (!image.pixel_data.empty()) {
        text.push_back("text_region_detected");
    }
    return text;
}

std::vector<float> ImageProcessor::apply_gaussian_blur(const ImageFrame& image, float sigma) {
    std::vector<float> blurred(image.pixel_data.size(), 0.0f);
    for (size_t i = 0; i < image.pixel_data.size(); ++i) {
        blurred[i] = image.pixel_data[i] / 255.0f;
    }
    return blurred;
}

std::vector<float> ImageProcessor::compute_gradients(const ImageFrame& image) {
    std::vector<float> gradients(std::min(256, static_cast<int>(image.pixel_data.size())), 0.0f);
    
    for (size_t i = 0; i < gradients.size(); ++i) {
        if (i > 0 && i < image.pixel_data.size() - 1) {
            float grad = std::abs(image.pixel_data[i] - image.pixel_data[i - 1]) +
                        std::abs(image.pixel_data[i + 1] - image.pixel_data[i]);
            gradients[i] = grad / 510.0f;
        }
    }
    
    return gradients;
}

VideoProcessor::VideoProcessor() {}

Embedding VideoProcessor::extract_video_features(const VideoFrame& video) {
    Embedding embedding(EMBEDDING_DIM);
    
    if (video.frames.empty()) {
        return embedding;
    }
    
    auto frame_embeddings = extract_frame_embeddings(video);
    auto temporal = extract_temporal_features(video);
    
    size_t idx = 0;
    for (const auto& frame_emb : frame_embeddings) {
        for (size_t i = 0; i < frame_emb.values.size() && idx < EMBEDDING_DIM; ++i) {
            embedding.values[idx++] += frame_emb.values[i];
        }
    }
    
    for (float val : temporal) {
        if (idx < EMBEDDING_DIM) {
            embedding.values[idx++] = val;
        }
    }
    
    while (idx < EMBEDDING_DIM) {
        embedding.values[idx++] = 0.0f;
    }
    
    return embedding;
}

std::vector<Embedding> VideoProcessor::extract_frame_embeddings(const VideoFrame& video) {
    std::vector<Embedding> embeddings;
    ImageProcessor img_processor;
    
    for (const auto& frame : video.frames) {
        embeddings.push_back(img_processor.extract_image_features(frame));
    }
    
    return embeddings;
}

std::vector<float> VideoProcessor::compute_optical_flow(const ImageFrame& frame1, 
                                                        const ImageFrame& frame2) {
    std::vector<float> flow = compute_motion_vectors(frame1, frame2);
    return flow;
}

std::string VideoProcessor::detect_action_in_video(const VideoFrame& video) {
    if (video.frames.empty()) return "no_action";
    return "generic_action_detected";
}

std::vector<float> VideoProcessor::extract_temporal_features(const VideoFrame& video) {
    std::vector<float> temporal(16, 0.0f);
    
    if (video.frames.size() > 1) {
        for (size_t i = 1; i < video.frames.size() && i < 16; ++i) {
            std::vector<float> flow = compute_optical_flow(video.frames[i-1], video.frames[i]);
            if (!flow.empty()) {
                temporal[i] = flow[0];
            }
        }
    }
    
    return temporal;
}

std::vector<float> VideoProcessor::compute_motion_vectors(const ImageFrame& f1, 
                                                          const ImageFrame& f2) {
    std::vector<float> motion(8, 0.0f);
    
    if (f1.pixel_data.size() == f2.pixel_data.size() && !f1.pixel_data.empty()) {
        float total_diff = 0.0f;
        for (size_t i = 0; i < f1.pixel_data.size(); ++i) {
            total_diff += std::abs(f1.pixel_data[i] - f2.pixel_data[i]);
        }
        motion[0] = total_diff / (f1.pixel_data.size() * 255.0f);
    }
    
    return motion;
}

TextProcessor::TextProcessor() {
    intent_keywords = {
        "query", "ask", "tell", "describe", "explain",
        "understand", "remember", "recall", "store", "learn",
        "verify", "check", "validate", "confirm"
    };
}

InstructionParsed TextProcessor::parse_instruction(const std::string& text) {
    InstructionParsed instruction;
    instruction.instruction_id = "instr_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    std::string normalized = normalize_text(text);
    std::vector<std::string> tokens = tokenize(normalized);
    
    if (!tokens.empty()) {
        instruction.primary_command = tokens[0];
        for (size_t i = 1; i < tokens.size(); ++i) {
            instruction.parameters.push_back(tokens[i]);
        }
    }
    
    instruction.intent_type = detect_intent(text);
    instruction.context_clues = extract_entities(text);
    instruction.confidence = 0.75f;
    
    return instruction;
}

std::vector<std::string> TextProcessor::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string word;
    
    while (iss >> word) {
        if (!word.empty()) {
            tokens.push_back(word);
        }
    }
    
    return tokens;
}

std::string TextProcessor::detect_intent(const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    for (const auto& keyword : intent_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return "intent_" + keyword;
        }
    }
    
    if (lower_text.find("who") != std::string::npos || 
        lower_text.find("what") != std::string::npos) {
        return "identity_query";
    }
    
    return "general_query";
}

std::vector<std::string> TextProcessor::extract_entities(const std::string& text) {
    std::vector<std::string> entities;
    
    if (text.find("name") != std::string::npos) {
        entities.push_back("entity_name");
    }
    if (text.find("age") != std::string::npos) {
        entities.push_back("entity_age");
    }
    if (text.find("location") != std::string::npos || text.find("place") != std::string::npos) {
        entities.push_back("entity_location");
    }
    
    return entities;
}

std::string TextProcessor::normalize_text(const std::string& text) {
    std::string normalized = text;
    
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    return normalized;
}

bool TextProcessor::is_valid_instruction(const std::string& text) {
    return !text.empty() && text.length() < 10000;
}

MultimodalInstructionHandler::MultimodalInstructionHandler()
    : audio_processor(std::make_unique<AudioProcessor>()),
      image_processor(std::make_unique<ImageProcessor>()),
      video_processor(std::make_unique<VideoProcessor>()),
      text_processor(std::make_unique<TextProcessor>()) {}

MultimodalInput MultimodalInstructionHandler::process_multimodal_input(
    const std::string& text,
    const std::shared_ptr<AudioFrame>& audio,
    const std::shared_ptr<ImageFrame>& image,
    const std::shared_ptr<VideoFrame>& video) {
    
    MultimodalInput input;
    input.input_id = generate_input_id();
    input.text_content = text;
    input.audio = audio;
    input.image = image;
    input.video = video;
    input.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    if (video) {
        input.modality = ModalityType::VIDEO;
    } else if (audio && image) {
        input.modality = ModalityType::MIXED;
    } else if (audio) {
        input.modality = ModalityType::AUDIO;
    } else if (image) {
        input.modality = ModalityType::IMAGE;
    } else {
        input.modality = ModalityType::TEXT;
    }
    
    input.combined_embedding = extract_modality_embedding(input);
    input.feature_scores = compute_modality_confidence(input);
    
    return input;
}

InstructionParsed MultimodalInstructionHandler::extract_instruction(const MultimodalInput& input) {
    InstructionParsed instruction = text_processor->parse_instruction(input.text_content);
    
    if (input.audio) {
        std::string audio_intent = audio_processor->recognize_intent_from_audio(*input.audio);
        instruction.context_clues.push_back(audio_intent);
    }
    
    if (input.image) {
        std::string obj_detection = image_processor->detect_objects_in_image(*input.image);
        instruction.context_clues.push_back(obj_detection);
    }
    
    if (input.video) {
        std::string action = video_processor->detect_action_in_video(*input.video);
        instruction.context_clues.push_back(action);
    }
    
    return instruction;
}

Embedding MultimodalInstructionHandler::combine_modalities(const std::vector<Embedding>& embeddings) {
    Embedding combined(EMBEDDING_DIM);
    
    if (embeddings.empty()) {
        return combined;
    }
    
    for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
        float sum = 0.0f;
        for (const auto& emb : embeddings) {
            sum += emb.values[i];
        }
        combined.values[i] = sum / embeddings.size();
    }
    
    return combined;
}

std::string MultimodalInstructionHandler::fuse_modality_decisions(
    const std::map<ModalityType, std::string>& decisions) {
    
    if (decisions.empty()) return "unknown";
    
    std::string fused_result = "fused_decision";
    for (const auto& [modality, decision] : decisions) {
        fused_result += "|" + decision;
    }
    
    return fused_result;
}

std::map<std::string, float> MultimodalInstructionHandler::compute_modality_confidence(
    const MultimodalInput& input) {
    
    std::map<std::string, float> confidence;
    
    confidence["text"] = 0.8f;
    if (input.audio) confidence["audio"] = 0.7f;
    if (input.image) confidence["image"] = 0.75f;
    if (input.video) confidence["video"] = 0.8f;
    
    return confidence;
}

std::string MultimodalInstructionHandler::get_dominant_modality(const MultimodalInput& input) const {
    switch (input.modality) {
        case ModalityType::TEXT: return "text";
        case ModalityType::AUDIO: return "audio";
        case ModalityType::IMAGE: return "image";
        case ModalityType::VIDEO: return "video";
        case ModalityType::MIXED: return "mixed";
        default: return "unknown";
    }
}

std::vector<MultimodalInput> MultimodalInstructionHandler::batch_process_inputs(
    const std::vector<std::string>& texts) {
    
    std::vector<MultimodalInput> batch;
    for (const auto& text : texts) {
        batch.push_back(process_multimodal_input(text));
    }
    
    return batch;
}

std::string MultimodalInstructionHandler::to_json(const MultimodalInput& input) const {
    std::string json = "{";
    json += "\"input_id\": \"" + input.input_id + "\", ";
    json += "\"modality\": \"" + get_dominant_modality(input) + "\", ";
    json += "\"text_content\": \"" + input.text_content + "\", ";
    json += "\"timestamp\": " + std::to_string(input.timestamp);
    json += "}";
    return json;
}

std::string MultimodalInstructionHandler::generate_input_id() {
    static int counter = 0;
    return "input_" + std::to_string(counter++) + "_" + 
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
}

Embedding MultimodalInstructionHandler::extract_modality_embedding(const MultimodalInput& input) {
    std::vector<Embedding> embeddings;
    
    Embedding text_emb(EMBEDDING_DIM);
    if (!input.text_content.empty()) {
        for (size_t i = 0; i < EMBEDDING_DIM && i < input.text_content.length(); ++i) {
            text_emb.values[i] = input.text_content[i] / 255.0f;
        }
    }
    embeddings.push_back(text_emb);
    
    if (input.audio) {
        embeddings.push_back(audio_processor->extract_spectrogram_features(*input.audio));
    }
    
    if (input.image) {
        embeddings.push_back(image_processor->extract_image_features(*input.image));
    }
    
    if (input.video) {
        embeddings.push_back(video_processor->extract_video_features(*input.video));
    }
    
    return combine_modalities(embeddings);
}
