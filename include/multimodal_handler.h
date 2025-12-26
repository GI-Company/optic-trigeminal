#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <map>

enum class ModalityType {
    TEXT = 0,
    AUDIO = 1,
    IMAGE = 2,
    VIDEO = 3,
    MIXED = 4
};

struct AudioFrame {
    std::vector<float> samples;
    int sample_rate;
    int channels;
    float duration_ms;
    
    AudioFrame() : sample_rate(16000), channels(1), duration_ms(0.0f) {}
};

struct ImageFrame {
    std::vector<uint8_t> pixel_data;
    int width;
    int height;
    int channels;
    std::string format;
    
    ImageFrame() : width(0), height(0), channels(3), format("RGB") {}
};

struct VideoFrame {
    std::vector<ImageFrame> frames;
    float fps;
    float duration_ms;
    int frame_count;
    
    VideoFrame() : fps(30.0f), duration_ms(0.0f), frame_count(0) {}
};

struct MultimodalInput {
    std::string input_id;
    ModalityType modality;
    std::string text_content;
    std::shared_ptr<AudioFrame> audio;
    std::shared_ptr<ImageFrame> image;
    std::shared_ptr<VideoFrame> video;
    Embedding combined_embedding;
    std::map<std::string, float> feature_scores;
    int64_t timestamp;
    
    MultimodalInput() : modality(ModalityType::TEXT), timestamp(0), 
                        combined_embedding(EMBEDDING_DIM),
                        audio(nullptr), image(nullptr), video(nullptr) {}
};

struct InstructionParsed {
    std::string instruction_id;
    std::string primary_command;
    std::vector<std::string> parameters;
    std::vector<std::string> context_clues;
    float confidence;
    std::string intent_type;
    
    InstructionParsed() : confidence(0.0f) {}
};

class AudioProcessor {
public:
    AudioProcessor();
    
    Embedding extract_spectrogram_features(const AudioFrame& audio);
    
    std::vector<float> apply_mfcc(const std::vector<float>& samples, int n_mfcc = 13);
    
    std::vector<float> compute_zero_crossing_rate(const AudioFrame& audio);
    
    std::vector<float> compute_energy_envelope(const AudioFrame& audio);
    
    std::string recognize_intent_from_audio(const AudioFrame& audio);
    
    bool detect_silence(const AudioFrame& audio, float threshold = 0.01f);
    
private:
    std::vector<float> apply_fft(const std::vector<float>& samples);
    std::vector<float> create_mel_filterbank(int n_mels, int n_fft, int sample_rate);
};

class ImageProcessor {
public:
    ImageProcessor();
    
    Embedding extract_image_features(const ImageFrame& image);
    
    std::vector<std::vector<float>> compute_histogram(const ImageFrame& image, int bins = 256);
    
    std::vector<float> extract_edges(const ImageFrame& image);
    
    std::vector<float> extract_color_moments(const ImageFrame& image);
    
    std::string detect_objects_in_image(const ImageFrame& image);
    
    std::vector<std::string> extract_text_from_image(const ImageFrame& image);
    
private:
    std::vector<float> apply_gaussian_blur(const ImageFrame& image, float sigma = 1.0f);
    std::vector<float> compute_gradients(const ImageFrame& image);
};

class VideoProcessor {
public:
    VideoProcessor();
    
    Embedding extract_video_features(const VideoFrame& video);
    
    std::vector<Embedding> extract_frame_embeddings(const VideoFrame& video);
    
    std::vector<float> compute_optical_flow(const ImageFrame& frame1, const ImageFrame& frame2);
    
    std::string detect_action_in_video(const VideoFrame& video);
    
    std::vector<float> extract_temporal_features(const VideoFrame& video);
    
private:
    std::vector<float> compute_motion_vectors(const ImageFrame& f1, const ImageFrame& f2);
};

class TextProcessor {
public:
    TextProcessor();
    
    InstructionParsed parse_instruction(const std::string& text);
    
    std::vector<std::string> tokenize(const std::string& text);
    
    std::string detect_intent(const std::string& text);
    
    std::vector<std::string> extract_entities(const std::string& text);
    
    std::string normalize_text(const std::string& text);
    
    bool is_valid_instruction(const std::string& text);
    
private:
    std::vector<std::string> intent_keywords;
};

class MultimodalInstructionHandler {
public:
    MultimodalInstructionHandler();
    
    MultimodalInput process_multimodal_input(const std::string& text,
                                            const std::shared_ptr<AudioFrame>& audio = nullptr,
                                            const std::shared_ptr<ImageFrame>& image = nullptr,
                                            const std::shared_ptr<VideoFrame>& video = nullptr);
    
    InstructionParsed extract_instruction(const MultimodalInput& input);
    
    Embedding combine_modalities(const std::vector<Embedding>& embeddings);
    
    std::string fuse_modality_decisions(const std::map<ModalityType, std::string>& decisions);
    
    std::map<std::string, float> compute_modality_confidence(const MultimodalInput& input);
    
    std::string get_dominant_modality(const MultimodalInput& input) const;
    
    std::vector<MultimodalInput> batch_process_inputs(const std::vector<std::string>& texts);
    
    std::string to_json(const MultimodalInput& input) const;

private:
    std::unique_ptr<AudioProcessor> audio_processor;
    std::unique_ptr<ImageProcessor> image_processor;
    std::unique_ptr<VideoProcessor> video_processor;
    std::unique_ptr<TextProcessor> text_processor;
    
    std::string generate_input_id();
    Embedding extract_modality_embedding(const MultimodalInput& input);
};
