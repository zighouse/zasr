# ZAsR Server Deployment Guide

This guide covers deploying the ZAsR (Streaming ASR) server for production use or integration with other applications.

## Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Configuration](#configuration)
- [Service Management](#service-management)
- [Model Management](#model-management)
- [Integration with zai.vim](#integration-with-zaivim)
- [Troubleshooting](#troubleshooting)

## Quick Start

```bash
# Install to /opt/zasr
./scripts/install.sh --dir /opt/zasr --from-binary

# Download models
/opt/zasr/scripts/download-models.sh --type all

# Start server
/opt/zasr/scripts/zasrctl start
```

## Installation

### Prerequisites

- Linux system (tested on Ubuntu 20.04+, Debian 11+)
- CMake 3.13+
- GCC/Clang with C++17 support
- 4GB+ RAM (for model loading)
- ~2GB disk space for models

### Install from Source

```bash
# Clone repository
git clone https://github.com/your-username/zasr.git
cd zasr

# Download dependencies
cd third_party
bash download_deps.sh

# Build
cd ..
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Install to Directory

```bash
# Install from existing build
./scripts/install.sh --dir /opt/zasr --from-binary

# Or build and install
./scripts/install.sh --dir /opt/zasr --from-source
```

### Install Options

| Option | Description |
|--------|-------------|
| `--dir DIR` | Installation directory (default: /opt/zasr) |
| `--from-source` | Build from source during installation |
| `--from-binary` | Use pre-built binary from build/ |
| `--download-models` | Download models automatically |
| `--config-only` | Only install configuration templates |
| `--with-integration` | Prepare for embedding (e.g., zai.vim) |

## Configuration

### Configuration File Format

ZAsR uses YAML configuration files with environment variable expansion:

```yaml
server:
  host: "0.0.0.0"
  port: 2026

vad:
  model: "${MODELS_DIR}/vad/silero_vad.int8.onnx"
```

### Configuration Priority

1. Command-line `--config` flag (highest)
2. Environment variable `ZASR_CONFIG`
3. `<deploy_dir>/config/default.yaml`
4. `~/.config/zasr/default.yaml`
5. `/etc/zasr/default.yaml` (lowest)

### Environment Variables

| Variable | Description |
|----------|-------------|
| `DEPLOY_DIR` | Deployment directory |
| `MODELS_DIR` | Model storage directory |
| `ZASR_CONFIG` | Configuration file path |

### Configuration Options

#### Server Settings

```yaml
server:
  host: "0.0.0.0"          # Server bind address
  port: 2026                # WebSocket port
  max_connections: 256      # Maximum concurrent connections
  worker_threads: 4         # Number of worker threads
```

#### Audio Settings

```yaml
audio:
  sample_rate: 16000        # Sample rate (Hz)
  sample_width: 2           # Bytes per sample (s16le = 2)
```

#### VAD Settings

```yaml
vad:
  enabled: true             # Enable VAD
  model: "${MODELS_DIR}/vad/silero_vad.int8.onnx"
  threshold: 0.5            # Speech probability threshold (0.0-1.0)
  min_silence_duration: 0.1 # Minimum silence for speech end (seconds)
  min_speech_duration: 0.25 # Minimum speech for speech start (seconds)
  max_speech_duration: 8.0  # Maximum speech duration (seconds)
```

#### ASR Settings

```yaml
asr:
  type: "sense-voice"       # Recognizer: sense-voice or streaming-zipformer
  num_threads: 2            # Number of recognition threads
  use_itn: true             # Inverse text normalization

  sense_voice:
    model: "${MODELS_DIR}/sense-voice/model.int8.onnx"
    tokens: "${MODELS_DIR}/sense-voice/tokens.txt"
```

#### Processing Settings

```yaml
processing:
  vad_window_size_ms: 30    # VAD window size (milliseconds)
  update_interval_ms: 200   # Recognition update interval (milliseconds)
  max_batch_size: 5         # Maximum batch size
```

## Service Management

### Using zasrctl

The `zasrctl` script provides service management:

```bash
# Start server
zasrctl start

# Stop server
zasrctl stop

# Restart server
zasrctl restart

# Check status
zasrctl status

# Generate configuration
zasrctl configure
```

### Command-Line Options

```bash
zasrctl start -c /path/to/config.yaml -d /deploy/dir
zasrctl status -p /custom/pid/file
```

### Manual Start

```bash
# Set environment variables
export DEPLOY_DIR=/opt/zasr
export MODELS_DIR=$DEPLOY_DIR/models

# Start server
$DEPLOY_DIR/bin/zasr-server --config $DEPLOY_DIR/config/default.yaml
```

## Model Management

### Download Models

Interactive download:
```bash
./scripts/download-models.sh
```

Non-interactive:
```bash
./scripts/download-models.sh --type sense-voice --dir /opt/zasr/models
```

Download all models:
```bash
./scripts/download-models.sh --type all --non-interactive
```

### Verify Models

```bash
./scripts/check-models.sh --dir /opt/zasr/models
```

### Model Search Paths

Models are searched in order:
1. `<deploy_dir>/models/`
2. `~/.cache/sherpa-onnx/`
3. `/usr/local/share/sherpa-onnx/`

### Model Types

| Model | Description | Use Case |
|-------|-------------|----------|
| Silero VAD | Voice activity detection | SenseVoice required |
| SenseVoice | Multilingual ASR | Chinese/English/Japanese/Korean |
| Streaming Zipformer | Streaming ASR | English only |
| Punctuation | Text punctuation | Post-processing |

## Integration with zai.vim

### Deployment for zai.vim

```bash
# Install to zai.vim directory
./scripts/install.sh --dir ~/.local/share/zai.vim/zasr --from-binary

# Generate zai.vim config
cp ~/.local/share/zai.vim/zasr/config/zai.yaml.template \
   ~/.local/share/zai.vim/zasr/config/zai.yaml

# Download models
~/.local/share/zai.vim/zasr/scripts/download-models.sh --type all
```

### zai.vim Configuration

Add to `.vimrc` or `init.lua`:

```vim
" zai.vim configuration
let g:zai_voice_enabled = 1
let g:zai_zasr_deploy_dir = expand('~/.local/share/zai.vim/zasr')
let g:zai_zasr_config = g:zai_zasr_deploy_dir . '/config/zai.yaml'
```

### Start from zai.vim

The zai.vim plugin will automatically start the server when voice input is enabled:

```vim
" In zai.vim
:ZaiVoiceEnable
" Server starts automatically
```

Manual start:
```vim
:call system('~/.local/share/zai.vim/zasr/scripts/zasrctl start')
```

## Troubleshooting

### Server Won't Start

**Check binary:**
```bash
ls -l $DEPLOY_DIR/bin/zasr-server
```

**Check configuration:**
```bash
$DEPLOY_DIR/bin/zasr-server --config $CONFIG --dry-run
```

**Check logs:**
```bash
tail -f $DEPLOY_DIR/logs/zasr.log
```

### Models Not Found

**Check model paths:**
```bash
./scripts/check-models.sh --verbose
```

**Verify models exist:**
```bash
ls -l $MODELS_DIR/vad/
ls -l $MODELS_DIR/sense-voice/
```

**Check config paths:**
```bash
grep "model:" $CONFIG
```

### Port Already in Use

**Find process using port:**
```bash
ss -tlnp | grep 2026
lsof -i :2026
```

**Change port in config:**
```yaml
server:
  port: 2027  # Use different port
```

### Memory Issues

**Check memory usage:**
```bash
ps aux | grep zasr-server
```

**Reduce worker threads:**
```yaml
server:
  worker_threads: 2  # Reduce from 4
```

**Use int8 models:**
```yaml
asr:
  sense_voice:
    model: "${MODELS_DIR}/sense-voice/model.int8.onnx"  # Use int8
```

### Permission Issues

**Fix permissions:**
```bash
chmod +x $DEPLOY_DIR/bin/zasr-server
chmod +x $DEPLOY_DIR/scripts/*.sh
```

**Fix ownership:**
```bash
sudo chown -R $USER:$USER $DEPLOY_DIR
```

## Advanced Configuration

### Multiple Instances

To run multiple instances:

1. Use different ports:
```yaml
# config1.yaml
server:
  port: 2026

# config2.yaml
server:
  port: 2027
```

2. Use different PID files:
```bash
zasrctl start -c config1.yaml -p /var/run/zasr1.pid
zasrctl start -c config2.yaml -p /var/run/zasr2.pid
```

### Logging

Enable file logging:
```yaml
logging:
  file: "/var/log/zasr/zasr.log"
  level: "debug"
```

Log rotation (systemd or logrotate):
```bash
# /etc/logrotate.d/zasr
/var/log/zasr/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
}
```

### Data Recording

Save audio and results for debugging:
```yaml
logging:
  data_dir: "/var/lib/zasr/data"
```

## Performance Tuning

### CPU Optimization

```yaml
server:
  worker_threads: $(nproc)  # Match CPU cores

asr:
  num_threads: 2  # Per-recognition threads
```

### Latency Reduction

```yaml
processing:
  vad_window_size_ms: 20      # Smaller window
  update_interval_ms: 100     # Faster updates
```

### Throughput Optimization

```yaml
server:
  max_connections: 512        # More connections
  worker_threads: 8           # More workers

processing:
  max_batch_size: 10          # Larger batches
```

## Support

- GitHub Issues: https://github.com/your-username/zasr/issues
- sherpa-onnx: https://github.com/k2-fsa/sherpa-onnx
