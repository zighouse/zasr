#include "zasr-config.h"
#include <iostream>
#include <sstream>
#include <cstdlib>

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
      } else {
        std::cerr << "Error: Invalid --recognizer-type '" << value
                  << "'. Must be 'sense-voice' or 'streaming-zipformer'\n";
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
