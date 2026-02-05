/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#include "zasr-config.h"
#include "yaml-config.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

namespace zasr {

const char* ZAsrConfig::FindArg(const std::vector<const char*>& args, const char* name) const {
  for (size_t i = 0; i < args.size(); ++i) {
    if (strcmp(args[i], name) == 0) {
      return args[i];
    }
  }
  return nullptr;
}

const char* ZAsrConfig::FindArgValue(const std::vector<const char*>& args, const char* name) const {
  for (size_t i = 0; i < args.size(); ++i) {
    if (strcmp(args[i], name) == 0) {
      if (i + 1 < args.size()) {
        return args[i + 1];
      }
      return nullptr;
    }
  }
  return nullptr;
}

bool ZAsrConfig::FromCommandLine(int argc, char* argv[]) {
  // Build argument vector (skip program name)
  std::vector<const char*> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  // Check for help
  if (FindArg(args, "--help") || FindArg(args, "-h")) {
    return false;
  }

  // Helper macro to parse string arguments
  auto parseString = [&](const char* argName, std::string& output) {
    const char* value = FindArgValue(args, argName);
    if (value) output = value;
  };

  // Helper macro to parse int arguments
  auto parseInt = [&](const char* argName, int& output) {
    const char* value = FindArgValue(args, argName);
    if (value) output = std::atoi(value);
  };

  // Helper macro to parse float arguments
  auto parseFloat = [&](const char* argName, float& output) {
    const char* value = FindArgValue(args, argName);
    if (value) output = std::atof(value);
  };

  // Helper macro to parse bool arguments
  auto parseBool = [&](const char* argName, bool& output) {
    const char* value = FindArgValue(args, argName);
    if (value) {
      output = (strcmp(value, "1") == 0 ||
               strcmp(value, "true") == 0 ||
               strcmp(value, "True") == 0);
    }
  };

  // Server configuration
  parseString("--host", host);
  parseInt("--port", port);
  parseInt("--max-connections", max_connections);
  parseInt("--worker-threads", worker_threads);

  // Audio configuration
  parseInt("--sample-rate", sample_rate);
  parseInt("--sample-width", sample_width);

  // VAD configuration
  parseString("--silero-vad-model", silero_vad_model);
  parseFloat("--vad-threshold", vad_threshold);
  parseFloat("--min-silence-duration", min_silence_duration);
  parseFloat("--min-speech-duration", min_speech_duration);
  parseFloat("--max-speech-duration", max_speech_duration);

  // Punctuation configuration
  parseBool("--enable-punctuation", enable_punctuation);
  parseString("--punctuation-model", punctuation_model);

  // ASR configuration
  // Parse recognizer type first
  {
    const char* value = FindArgValue(args, "--recognizer-type");
    if (value) {
      if (strcmp(value, "sense-voice") == 0) {
        recognizer_type = RecognizerType::kSenseVoice;
      } else if (strcmp(value, "streaming-zipformer") == 0) {
        recognizer_type = RecognizerType::kStreamingZipformer;
      } else if (strcmp(value, "streaming-paraformer") == 0) {
        recognizer_type = RecognizerType::kStreamingParaformer;
      } else {
        std::cerr << "Error: Invalid --recognizer-type '" << value
                  << "'. Must be 'sense-voice', 'streaming-zipformer', or 'streaming-paraformer'\n";
        return false;
      }
    }
  }

  parseString("--sense-voice-model", sense_voice_model);
  parseString("--tokens", tokens_path);
  parseBool("--use-itn", use_itn);
  parseInt("--num-threads", num_threads);
  parseString("--zipformer-encoder", zipformer_encoder);
  parseString("--zipformer-decoder", zipformer_decoder);
  parseString("--zipformer-joiner", zipformer_joiner);
  parseString("--paraformer-encoder", paraformer_encoder);
  parseString("--paraformer-decoder", paraformer_decoder);

  // Processing configuration
  parseFloat("--vad-window-size-ms", vad_window_size_ms);
  parseFloat("--update-interval-ms", update_interval_ms);
  parseInt("--max-batch-size", max_batch_size);

  // Logging and storage
  parseString("--log-file", log_file);
  parseString("--data-dir", data_dir);

  // Timeouts
  parseInt("--connection-timeout", connection_timeout_seconds);
  parseInt("--recognition-timeout", recognition_timeout_seconds);

  // Set default paths if not specified
  if (silero_vad_model.empty()) {
    silero_vad_model = GetDefaultModelPath("silero_vad.int8.onnx");
  }
  if (enable_punctuation && punctuation_model.empty()) {
    punctuation_model = GetDefaultModelPath("sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/model.onnx");
  }

  return true;
}

bool ZAsrConfig::FromYamlFile(const std::string& filepath) {
  YamlConfig config;
  if (!config.LoadFromFile(filepath)) {
    std::cerr << "Error loading YAML config: " << config.GetError() << "\n";
    return false;
  }

  // Server configuration
  host = config.GetString("server.host", host);
  port = config.GetInt("server.port", port);
  max_connections = config.GetInt("server.max_connections", max_connections);
  worker_threads = config.GetInt("server.worker_threads", worker_threads);

  // Audio configuration
  sample_rate = config.GetInt("audio.sample_rate", sample_rate);
  sample_width = config.GetInt("audio.sample_width", sample_width);

  // VAD configuration
  if (config.HasKey("vad.enabled")) {
    bool vad_enabled = config.GetBool("vad.enabled", true);
    if (!vad_enabled) {
      // VAD is optional for streaming-zipformer
      silero_vad_model.clear();
    }
  }
  silero_vad_model = config.GetString("vad.model", silero_vad_model);
  vad_threshold = config.GetFloat("vad.threshold", vad_threshold);
  min_silence_duration = config.GetFloat("vad.min_silence_duration", min_silence_duration);
  min_speech_duration = config.GetFloat("vad.min_speech_duration", min_speech_duration);
  max_speech_duration = config.GetFloat("vad.max_speech_duration", max_speech_duration);

  // ASR configuration
  std::string asr_type = config.GetString("asr.type", "sense-voice");
  if (asr_type == "streaming-zipformer") {
    recognizer_type = RecognizerType::kStreamingZipformer;
  } else if (asr_type == "streaming-paraformer") {
    recognizer_type = RecognizerType::kStreamingParaformer;
  } else {
    recognizer_type = RecognizerType::kSenseVoice;
  }

  num_threads = config.GetInt("asr.num_threads", num_threads);
  use_itn = config.GetBool("asr.use_itn", use_itn);

  // ASR configuration based on recognizer type
  if (recognizer_type == RecognizerType::kSenseVoice) {
    // SenseVoice models
    sense_voice_model = config.GetString("asr.sense_voice.model", sense_voice_model);
    tokens_path = config.GetString("asr.sense_voice.tokens", tokens_path);
  } else if (recognizer_type == RecognizerType::kStreamingParaformer) {
    // Streaming Paraformer models
    paraformer_encoder = config.GetString("asr.streaming_paraformer.encoder", paraformer_encoder);
    paraformer_decoder = config.GetString("asr.streaming_paraformer.decoder", paraformer_decoder);
    tokens_path = config.GetString("asr.streaming_paraformer.tokens", tokens_path);
  } else {
    // Streaming Zipformer models
    zipformer_encoder = config.GetString("asr.streaming_zipformer.encoder", zipformer_encoder);
    zipformer_decoder = config.GetString("asr.streaming_zipformer.decoder", zipformer_decoder);
    zipformer_joiner = config.GetString("asr.streaming_zipformer.joiner", zipformer_joiner);
    tokens_path = config.GetString("asr.streaming_zipformer.tokens", tokens_path);
  }

  // Punctuation
  enable_punctuation = config.GetBool("punctuation.enabled", enable_punctuation);
  punctuation_model = config.GetString("punctuation.model", punctuation_model);

  // Processing
  vad_window_size_ms = config.GetFloat("processing.vad_window_size_ms", vad_window_size_ms);
  update_interval_ms = config.GetFloat("processing.update_interval_ms", update_interval_ms);
  max_batch_size = config.GetInt("processing.max_batch_size", max_batch_size);

  // Timeouts
  connection_timeout_seconds = config.GetInt("timeouts.connection", connection_timeout_seconds);
  recognition_timeout_seconds = config.GetInt("timeouts.recognition", recognition_timeout_seconds);

  // Logging
  log_file = config.GetString("logging.file", log_file);
  data_dir = config.GetString("logging.data_dir", data_dir);

  // Resolve model paths - check if file exists first
  if (!silero_vad_model.empty()) {
    // If absolute path, check if file exists
    if (silero_vad_model[0] == '/') {
      struct stat buffer;
      if (stat(silero_vad_model.c_str(), &buffer) != 0) {
        std::cerr << "VAD model not found at: " << silero_vad_model << ", searching..." << std::endl;
        // File doesn't exist, try to find it in search paths using just filename
        size_t pos = silero_vad_model.find_last_of('/');
        if (pos != std::string::npos) {
          std::string filename = silero_vad_model.substr(pos + 1);
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          std::string resolved = YamlConfig::FindFileInPaths(filename, model_paths);
          if (!resolved.empty()) {
            silero_vad_model = resolved;
            std::cerr << "VAD model found at: " << silero_vad_model << std::endl;
          } else {
            std::cerr << "VAD model NOT found!" << std::endl;
          }
        }
      }
    } else {
      // Relative path, search in model paths
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(silero_vad_model, model_paths);
      if (!resolved.empty()) {
        silero_vad_model = resolved;
      }
    }
  }

  // SenseVoice model path
  if (!sense_voice_model.empty()) {
    if (sense_voice_model[0] == '/') {
      struct stat buffer;
      if (stat(sense_voice_model.c_str(), &buffer) != 0) {
        std::cerr << "SenseVoice model not found at: " << sense_voice_model << ", searching..." << std::endl;
        // Extract the model directory name (e.g., "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17")
        std::string resolved;
        size_t last_slash = sense_voice_model.find_last_of('/');
        size_t second_last = sense_voice_model.find_last_of('/', last_slash - 1);

        if (second_last != std::string::npos && last_slash != std::string::npos) {
          // Extract model directory name
          std::string model_dir = sense_voice_model.substr(second_last + 1, last_slash - second_last - 1);
          std::string filename = sense_voice_model.substr(last_slash + 1);

          // Try to find this specific model directory
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          for (const auto& base_path : model_paths) {
            std::string model_path = base_path + "/" + model_dir + "/" + filename;
            if (stat(model_path.c_str(), &buffer) == 0) {
              resolved = model_path;
              break;
            }
          }

          // If not found, try recursive search with model_dir hint
          if (resolved.empty()) {
            resolved = YamlConfig::FindFileInModelDir(model_paths, model_dir, filename);
          }
        }

        if (!resolved.empty()) {
          sense_voice_model = resolved;
          std::cerr << "SenseVoice model found at: " << sense_voice_model << std::endl;
        } else {
          std::cerr << "SenseVoice model NOT found!" << std::endl;
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(sense_voice_model, model_paths);
      if (!resolved.empty()) {
        sense_voice_model = resolved;
      }
    }
  }

  // Tokens path
  if (!tokens_path.empty()) {
    if (tokens_path[0] == '/') {
      struct stat buffer;
      if (stat(tokens_path.c_str(), &buffer) != 0) {
        std::cerr << "Tokens file not found at: " << tokens_path << ", searching..." << std::endl;
        // Extract the model directory name
        std::string resolved;
        size_t last_slash = tokens_path.find_last_of('/');
        size_t second_last = tokens_path.find_last_of('/', last_slash - 1);

        if (second_last != std::string::npos && last_slash != std::string::npos) {
          // Extract model directory name
          std::string model_dir = tokens_path.substr(second_last + 1, last_slash - second_last - 1);
          std::string filename = tokens_path.substr(last_slash + 1);

          // Try to find this specific model directory
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          for (const auto& base_path : model_paths) {
            std::string model_path = base_path + "/" + model_dir + "/" + filename;
            if (stat(model_path.c_str(), &buffer) == 0) {
              resolved = model_path;
              break;
            }
          }

          // If not found, try recursive search with model_dir hint
          if (resolved.empty()) {
            resolved = YamlConfig::FindFileInModelDir(model_paths, model_dir, filename);
          }
        }

        if (!resolved.empty()) {
          tokens_path = resolved;
          std::cerr << "Tokens file found at: " << tokens_path << std::endl;
        } else {
          std::cerr << "Tokens file NOT found!" << std::endl;
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(tokens_path, model_paths);
      if (!resolved.empty()) {
        tokens_path = resolved;
      }
    }
  }

  // Zipformer encoder path
  if (!zipformer_encoder.empty()) {
    if (zipformer_encoder[0] == '/') {
      struct stat buffer;
      if (stat(zipformer_encoder.c_str(), &buffer) != 0) {
        size_t pos = zipformer_encoder.find_last_of('/');
        if (pos != std::string::npos) {
          std::string filename = zipformer_encoder.substr(pos + 1);
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          std::string resolved = YamlConfig::FindFileInPaths(filename, model_paths);
          if (!resolved.empty()) {
            zipformer_encoder = resolved;
          }
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(zipformer_encoder, model_paths);
      if (!resolved.empty()) {
        zipformer_encoder = resolved;
      }
    }
  }

  // Zipformer decoder path
  if (!zipformer_decoder.empty()) {
    if (zipformer_decoder[0] == '/') {
      struct stat buffer;
      if (stat(zipformer_decoder.c_str(), &buffer) != 0) {
        size_t pos = zipformer_decoder.find_last_of('/');
        if (pos != std::string::npos) {
          std::string filename = zipformer_decoder.substr(pos + 1);
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          std::string resolved = YamlConfig::FindFileInPaths(filename, model_paths);
          if (!resolved.empty()) {
            zipformer_decoder = resolved;
          }
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(zipformer_decoder, model_paths);
      if (!resolved.empty()) {
        zipformer_decoder = resolved;
      }
    }
  }

  // Zipformer joiner path
  if (!zipformer_joiner.empty()) {
    if (zipformer_joiner[0] == '/') {
      struct stat buffer;
      if (stat(zipformer_joiner.c_str(), &buffer) != 0) {
        size_t pos = zipformer_joiner.find_last_of('/');
        if (pos != std::string::npos) {
          std::string filename = zipformer_joiner.substr(pos + 1);
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          std::string resolved = YamlConfig::FindFileInPaths(filename, model_paths);
          if (!resolved.empty()) {
            zipformer_joiner = resolved;
          }
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(zipformer_joiner, model_paths);
      if (!resolved.empty()) {
        zipformer_joiner = resolved;
      }
    }
  }

  // Punctuation model path
  if (!punctuation_model.empty()) {
    if (punctuation_model[0] == '/') {
      struct stat buffer;
      if (stat(punctuation_model.c_str(), &buffer) != 0) {
        size_t pos = punctuation_model.find_last_of('/');
        if (pos != std::string::npos) {
          std::string filename = punctuation_model.substr(pos + 1);
          auto model_paths = YamlConfig::GetDefaultModelPaths();
          std::string resolved = YamlConfig::FindFileInPaths(filename, model_paths);
          if (!resolved.empty()) {
            punctuation_model = resolved;
          }
        }
      }
    } else {
      auto model_paths = YamlConfig::GetDefaultModelPaths();
      std::string resolved = YamlConfig::FindFileInPaths(punctuation_model, model_paths);
      if (!resolved.empty()) {
        punctuation_model = resolved;
      }
    }
  }

  // Set default paths if not specified
  if (silero_vad_model.empty() && recognizer_type == RecognizerType::kSenseVoice) {
    silero_vad_model = GetDefaultModelPath("silero_vad.int8.onnx");
  }
  if (enable_punctuation && punctuation_model.empty()) {
    punctuation_model = GetDefaultModelPath("sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/model.onnx");
  }

  return true;
}

bool ZAsrConfig::Validate() const {
  // Validate based on recognizer type
  if (recognizer_type == RecognizerType::kSenseVoice) {
    // SenseVoice 需要 VAD 模型和 SenseVoice 模型
    if (silero_vad_model.empty()) {
      std::cerr << "Error: --silero-vad-model is required for recognizer-type 'sense-voice'\n";
      return false;
    }
    if (sense_voice_model.empty()) {
      std::cerr << "Error: --sense-voice-model is required for recognizer-type 'sense-voice'\n";
      return false;
    }
  } else if (recognizer_type == RecognizerType::kStreamingParaformer) {
    // Streaming Paraformer 不需要 VAD，只需要两个模型文件
    if (paraformer_encoder.empty()) {
      std::cerr << "Error: --paraformer-encoder is required for recognizer-type 'streaming-paraformer'\n";
      return false;
    }
    if (paraformer_decoder.empty()) {
      std::cerr << "Error: --paraformer-decoder is required for recognizer-type 'streaming-paraformer'\n";
      return false;
    }
  } else if (recognizer_type == RecognizerType::kStreamingZipformer) {
    // Streaming Zipformer 不需要 VAD，只需要三个模型文件
    if (zipformer_encoder.empty()) {
      std::cerr << "Error: --zipformer-encoder is required for recognizer-type 'streaming-zipformer'\n";
      return false;
    }
    if (zipformer_decoder.empty()) {
      std::cerr << "Error: --zipformer-decoder is required for recognizer-type 'streaming-zipformer'\n";
      return false;
    }
    if (zipformer_joiner.empty()) {
      std::cerr << "Error: --zipformer-joiner is required for recognizer-type 'streaming-zipformer'\n";
      return false;
    }
  }

  if (tokens_path.empty()) {
    std::cerr << "Error: --tokens is required\n";
    return false;
  }

  if (sample_rate != 16000) {
    std::cerr << "Error: sample rate must be 16000\n";
    return false;
  }

  if (sample_width != 2) {
    std::cerr << "Error: sample width must be 2 (s16le)\n";
    return false;
  }

  if (max_connections <= 0) {
    std::cerr << "Error: max-connections must be > 0\n";
    return false;
  }

  if (worker_threads <= 0) {
    std::cerr << "Error: worker-threads must be > 0\n";
    return false;
  }

  if (num_threads <= 0) {
    std::cerr << "Error: num-threads must be > 0\n";
    return false;
  }

  if (vad_threshold <= 0 || vad_threshold > 1) {
    std::cerr << "Error: vad-threshold must be in range (0, 1]\n";
    return false;
  }

  if (min_silence_duration < 0) {
    std::cerr << "Error: min-silence-duration must be >= 0\n";
    return false;
  }

  if (min_speech_duration <= 0) {
    std::cerr << "Error: min-speech-duration must be > 0\n";
    return false;
  }

  if (max_speech_duration <= 0) {
    std::cerr << "Error: max-speech-duration must be > 0\n";
    return false;
  }

  if (vad_window_size_ms <= 0) {
    std::cerr << "Error: vad-window-size-ms must be > 0\n";
    return false;
  }

  if (update_interval_ms <= 0) {
    std::cerr << "Error: update-interval-ms must be > 0\n";
    return false;
  }

  if (max_batch_size <= 0) {
    std::cerr << "Error: max-batch-size must be > 0\n";
    return false;
  }

  if (connection_timeout_seconds <= 0) {
    std::cerr << "Error: connection-timeout must be > 0\n";
    return false;
  }

  if (recognition_timeout_seconds <= 0) {
    std::cerr << "Error: recognition-timeout must be > 0\n";
    return false;
  }

  return true;
}

std::string ZAsrConfig::ToString() const {
  std::ostringstream os;

  os << "ZASR Server Configuration:\n";
  os << "  Server: " << host << ":" << port << "\n";
  os << "  Max connections: " << max_connections << "\n";
  os << "  Worker threads: " << worker_threads << "\n";
  os << "  Audio: " << sample_rate << "Hz, " << sample_width << " bytes/sample\n";

  os << "  VAD:\n";
  os << "    Model: " << silero_vad_model << "\n";
  os << "    Threshold: " << vad_threshold << "\n";
  os << "    Min silence: " << min_silence_duration << "s\n";
  os << "    Min speech: " << min_speech_duration << "s\n";
  os << "    Max speech: " << max_speech_duration << "s\n";
  os << "    Window size: " << vad_window_size_ms << "ms\n";

  os << "  ASR:\n";
  if (recognizer_type == RecognizerType::kSenseVoice) {
    os << "    Type: sense-voice (simulated streaming)\n";
    os << "    Model: " << sense_voice_model << "\n";
  } else if (recognizer_type == RecognizerType::kStreamingParaformer) {
    os << "    Type: streaming-paraformer (true streaming)\n";
    os << "    Encoder: " << paraformer_encoder << "\n";
    os << "    Decoder: " << paraformer_decoder << "\n";
  } else {
    os << "    Type: streaming-zipformer (true streaming)\n";
    os << "    Encoder: " << zipformer_encoder << "\n";
    os << "    Decoder: " << zipformer_decoder << "\n";
    os << "    Joiner: " << zipformer_joiner << "\n";
  }
  os << "    Tokens: " << tokens_path << "\n";
  os << "    Use ITN: " << (use_itn ? "true" : "false") << "\n";
  os << "    Threads: " << num_threads << "\n";
  os << "    Max batch size: " << max_batch_size << "\n";
  os << "    Update interval: " << update_interval_ms << "ms\n";

  os << "  Punctuation:\n";
  os << "    Enabled: " << (enable_punctuation ? "true" : "false") << "\n";
  if (enable_punctuation) {
    os << "    Model: " << punctuation_model << "\n";
  }

  os << "  Timeouts:\n";
  os << "    Connection: " << connection_timeout_seconds << "s\n";
  os << "    Recognition: " << recognition_timeout_seconds << "s\n";

  if (!log_file.empty()) {
    os << "  Log file: " << log_file << "\n";
  }

  if (!data_dir.empty()) {
    os << "  Data directory: " << data_dir << "\n";
  }

  return os.str();
}

}  // namespace zasr
