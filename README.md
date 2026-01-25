# zasr - WebSocket Streaming ASR Server

A high-performance streaming Automatic Speech Recognition (ASR) server built with C++17, WebSocket++, and sherpa-onnx.

## Features

- **Dual Recognizer Support**
  - **SenseVoice**: Simulated streaming using VAD-triggered offline recognition (multi-lingual: Chinese, English, Japanese, Korean, Cantonese)
  - **Streaming Zipformer**: True streaming recognition with ultra-low latency (Chinese-English bilingual)

- **Thread-Safe Architecture**: Dual io_context design for efficient thread separation
- **Punctuation Support**: Automatic punctuation restoration (optional)
- **Static Linking**: Self-contained ~35MB binary with no external runtime dependencies
- **WebSocket Protocol**: JSON-based protocol compatible with various clients
- **VAD Integration**: Silero VAD for accurate voice activity detection

## Project Structure

```
zasr/
├── src/                    # Main source code
│   ├── zasr-server.h/cc   # WebSocket server
│   ├── zasr-connection.h/cc  # Per-client connection
│   └── zasr-config.h/cc   # Configuration
├── third_party/            # Dependencies
│   ├── sherpa-onnx/       # Symbolic link to sherpa-onnx
│   ├── asio/              # Asio networking library
│   ├── websocketpp/       # WebSocket library
│   ├── json.hpp           # nlohmann/json
│   └── download_deps.sh   # Dependency download script
├── build/                  # CMake build output
├── start-server.sh         # Server startup script
├── test_simple_client.py   # Test client (WAV file)
└── test_microphone_client.py  # Test client (microphone)
```

## Building

### Prerequisites

Download third-party dependencies:

```bash
cd third_party
bash download_deps.sh
```

This downloads standalone sherpa-onnx, asio, websocketpp, and nlohmann/json to `third_party/`.

### Compile

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The executable is output to `build/zasr-server`.

### Clean Build

```bash
rm -rf build
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## Usage

### Start the Server

The `start-server.sh` script provides an easy way to start the server with different recognizers.

**Using Streaming Zipformer (recommended for low latency):**

```bash
RECOGNIZER_TYPE=streaming-zipformer ./start-server.sh
```

**Using SenseVoice (multi-lingual):**

```bash
RECOGNIZER_TYPE=sense-voice ./start-server.sh
```

### Manual Server Start

**Streaming Zipformer:**

```bash
./build/zasr-server \
  --recognizer-type streaming-zipformer \
  --zipformer-encoder ~/.cache/sherpa-onnx/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/encoder-epoch-99-avg-1.onnx \
  --zipformer-decoder ~/.cache/sherpa-onnx/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/decoder-epoch-99-avg-1.onnx \
  --zipformer-joiner ~/.cache/sherpa-onnx/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/joiner-epoch-99-avg-1.onnx \
  --tokens ~/.cache/sherpa-onnx/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tokens.txt \
  --punctuation-model ~/.cache/sherpa-onnx/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/model.onnx \
  --enable-punctuation true \
  --port 2026
```

**SenseVoice:**

```bash
./build/zasr-server \
  --recognizer-type sense-voice \
  --silero-vad-model ~/.cache/sherpa-onnx/silero_vad.int8.onnx \
  --sense-voice-model ~/.cache/sherpa-onnx/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/model.int8.onnx \
  --tokens ~/.cache/sherpa-onnx/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/tokens.txt \
  --port 2026
```

### Test Clients

Python test clients are provided:

- **test_simple_client.py** - Test with a WAV file
- **test_microphone_client.py** - Test with live microphone input

Example:

```bash
# Test with a WAV file
python3 test_simple_client.py test.wav --server ws://localhost:2026

# Test with microphone
python3 test_microphone_client.py --server ws://localhost:2026
```

## WebSocket Protocol

The server uses a JSON-based WebSocket protocol for real-time speech recognition.

### Message Types

#### Begin (Client → Server)

Start a transcription session:

```json
{
  "header": {
    "name": "Begin",
    "mid": "message-id"
  },
  "payload": {
    "fmt": "pcm",
    "rate": 16000,
    "itn": true,
    "silence": 500
  }
}
```

#### Started (Server → Client)

Session started confirmation:

```json
{
  "header": {
    "name": "Started",
    "sid": "session-id",
    "mid": "message-id"
  },
  "payload": {}
}
```

#### SentenceBegin (Server → Client)

Marks the start of a new sentence segment:

```json
{
  "header": {
    "name": "SentenceBegin",
    "sid": "session-id"
  },
  "payload": {
    "index": 0,
    "time": 1000
  }
}
```

#### Result (Server → Client)

Intermediate recognition results:

```json
{
  "header": {
    "name": "Result",
    "sid": "session-id"
  },
  "payload": {
    "index": 0,
    "time": 1500,
    "result": "识别到的文本"
  }
}
```

#### SentenceEnd (Server → Client)

Marks the end of a sentence with final result:

```json
{
  "header": {
    "name": "SentenceEnd",
    "sid": "session-id"
  },
  "payload": {
    "index": 0,
    "time": 3000,
    "begin_time": 1000,
    "result": "最终的识别结果"
  }
}
```

#### Completed (Server → Client)

Transcription session complete:

```json
{
  "header": {
    "name": "Completed",
    "sid": "session-id"
  },
  "payload": {}
}
```

#### End (Client → Server)

Request to stop transcription:

```json
{
  "header": {
    "name": "End",
    "sid": "session-id",
    "mid": "message-id"
  },
  "payload": {}
}
```

### Audio Format

- Binary audio data sent as WebSocket binary messages
- Format: PCM s16le (16-bit signed little-endian)
- Sample rate: 16000 Hz (configurable)
- Channels: Mono

## Architecture

### Threading Model

The server uses a dual `io_context` design for thread separation:

1. **io_conn_**: Runs on the main thread, handles WebSocket connections and message sending only. WebSocket++ is not thread-safe, so all sends must be posted to this context.

2. **io_work_**: Shared by worker thread pool (configurable via `--worker-threads`), handles:
   - VAD processing
   - ASR recognition
   - Audio data processing

All binary audio messages are dispatched to worker threads via `asio::post(io_work_, ...)`.

### Component Structure

- **ZAsrServer** (`src/zasr-server.h/.cc`): Main WebSocket server, connection lifecycle management, timeout checking
- **ZAsrConnection** (`src/zasr-connection.h/.cc`): Per-client connection state, VAD/ASR processing, protocol message handling
- **ZAsrConfig** (`src/zasr-config.h/.cc`): Command-line argument parsing and configuration validation

### Connection State Machine

Connections progress through states: `CONNECTED` → `STARTED` → `PROCESSING` → `CLOSING` → `CLOSED`

### Static Linking

The server is statically linked with sherpa-onnx components (approximately 35MB final binary). No external runtime dependencies beyond system libraries.

### sherpa-onnx API Usage

This project uses sherpa-onnx's **public C++ API** (`cxx-api.h`), not internal implementation headers. This provides API stability and allows independent sherpa-onnx version upgrades.

Key sherpa-onnx classes used:
- `sherpa_onnx::cxx::VoiceActivityDetector`: Silero VAD
- `sherpa_onnx::cxx::OfflineRecognizer`: SenseVoice ASR
- `sherpa_onnx::cxx::OnlineRecognizer`: Streaming Zipformer ASR
- `sherpa_onnx::cxx::OfflineStream`: Audio stream for offline recognition
- `sherpa_onnx::cxx::OnlineStream`: Audio stream for online recognition
- `sherpa_onnx::cxx::OfflinePunctuation`: Punctuation restoration

## Command-Line Options

```
Server Configuration:
  --host arg                 Server host (default: 0.0.0.0)
  --port arg                 Server port (default: 2026)
  --max-connections arg      Maximum connections (default: 256)
  --worker-threads arg       Worker threads (default: 4)

Audio Configuration:
  --sample-rate arg          Sample rate (default: 16000)

VAD Configuration:
  --silero-vad-model arg     Path to Silero VAD model
  --vad-threshold arg        VAD threshold (default: 0.5)
  --min-silence-duration arg Minimum silence duration in seconds (default: 0.1)
  --min-speech-duration arg  Minimum speech duration in seconds (default: 0.25)
  --max-speech-duration arg  Maximum speech duration in seconds (default: 8.0)

ASR Configuration:
  --recognizer-type arg      Recognizer type: sense-voice or streaming-zipformer
                             (default: sense-voice)

SenseVoice Options:
  --sense-voice-model arg    Path to SenseVoice model
  --tokens arg               Path to tokens.txt
  --use-itn arg              Enable inverse text normalization (default: true)
  --num-threads arg          Number of threads for ASR (default: 2)

Streaming Zipformer Options:
  --zipformer-encoder arg    Path to encoder model
  --zipformer-decoder arg    Path to decoder model
  --zipformer-joiner arg     Path to joiner model

Punctuation Configuration:
  --enable-punctuation arg   Enable punctuation (default: false)
  --punctuation-model arg    Path to punctuation model

Processing Configuration:
  --vad-window-size-ms arg   VAD window size in milliseconds (default: 30)
  --update-interval-ms arg   Update interval in milliseconds (default: 200)

Timeouts:
  --connection-timeout-seconds arg     Connection timeout (default: 15)
  --recognition-timeout-seconds arg    Recognition timeout (default: 30)

Logging:
  --log-file arg            Log file path (default: stdout)
```

## Key Constants

- Default sample rate: 16000 Hz
- Default port: 2026
- Default max connections: 256
- Default worker threads: 4
- Audio format: PCM s16le (16-bit signed little-endian)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

This project uses sherpa-onnx which is licensed under the Apache 2.0 License.
