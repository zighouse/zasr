#!/bin/bash
#
# ZASR - Streaming ASR Server
# Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
#
# Licensed under the MIT License
#
# install.sh - ZASR Server Installation Script
#
# Usage: ./install.sh [OPTIONS]
#

set -e

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default values
INSTALL_DIR="${INSTALL_DIR:-/opt/zasr}"
FROM_SOURCE=false
FROM_BINARY=false
DOWNLOAD_MODELS=false
CONFIG_ONLY=false
WITH_INTEGRATION=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Print usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Install ZASR server to a target directory."
    echo ""
    echo "Options:"
    echo "  --dir DIR              Installation directory (default: /opt/zasr)"
    echo "  --from-source          Build from source (requires cmake, g++)"
    echo "  --from-binary          Use pre-built binary (requires build/)"
    echo "  --download-models      Download required models"
    echo "  --config-only          Only install configuration templates"
    echo "  --with-integration     Prepare for embedding (e.g., zai.vim)"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --dir /opt/zasr --from-binary"
    echo "  $0 --dir ~/.local/share/zasr --download-models"
    echo "  $0 --config-only --with-integration"
}

# Parse arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dir)
                INSTALL_DIR="$2"
                shift 2
                ;;
            --from-source)
                FROM_SOURCE=true
                shift
                ;;
            --from-binary)
                FROM_BINARY=true
                shift
                ;;
            --download-models)
                DOWNLOAD_MODELS=true
                shift
                ;;
            --config-only)
                CONFIG_ONLY=true
                shift
                ;;
            --with-integration)
                WITH_INTEGRATION=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                echo -e "${RED}Unknown option: $1${NC}"
                usage
                exit 1
                ;;
        esac
    done
}

# Check if directory exists
check_directory() {
    if [ -d "$INSTALL_DIR" ] && [ "$(ls -A "$INSTALL_DIR" 2>/dev/null)" ]; then
        echo -e "${YELLOW}Warning: Installation directory exists and is not empty: $INSTALL_DIR${NC}"
        read -p "Continue? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# Create directory structure
create_directories() {
    echo -e "${BLUE}Creating directory structure...${NC}"
    mkdir -p "$INSTALL_DIR"/{bin,config,models,scripts,var,logs}
    echo -e "${GREEN}Directories created${NC}"
}

# Build from source
build_from_source() {
    echo -e "${BLUE}Building from source...${NC}"

    # Check dependencies
    if ! command -v cmake >/dev/null 2>&1; then
        echo -e "${RED}Error: cmake not found${NC}"
        exit 1
    fi

    if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
        echo -e "${RED}Error: C++ compiler not found${NC}"
        exit 1
    fi

    # Download dependencies if needed
    if [ ! -d "$PROJECT_ROOT/third_party/sherpa-onnx" ]; then
        echo -e "${YELLOW}Downloading third-party dependencies...${NC}"
        bash "$PROJECT_ROOT/third_party/download_deps.sh"
    fi

    # Build
    local build_dir="$PROJECT_ROOT/build"
    mkdir -p "$build_dir"
    cd "$build_dir"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)

    if [ ! -f "$build_dir/zasr-server" ]; then
        echo -e "${RED}Error: Build failed${NC}"
        exit 1
    fi

    echo -e "${GREEN}Build successful${NC}"
}

# Copy binary from build directory
copy_binary() {
    echo -e "${BLUE}Installing binary...${NC}"

    local build_dir="$PROJECT_ROOT/build"
    if [ ! -f "$build_dir/zasr-server" ]; then
        echo -e "${RED}Error: Binary not found at $build_dir/zasr-server${NC}"
        echo "Please build first: mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
        exit 1
    fi

    cp "$build_dir/zasr-server" "$INSTALL_DIR/bin/"
    chmod +x "$INSTALL_DIR/bin/zasr-server"

    echo -e "${GREEN}Binary installed${NC}"
}

# Copy configuration files
copy_configs() {
    echo -e "${BLUE}Installing configuration files...${NC}"

    # Copy default config
    if [ -f "$PROJECT_ROOT/config/default.yaml" ]; then
        cp "$PROJECT_ROOT/config/default.yaml" "$INSTALL_DIR/config/"
    fi

    # Copy zai.vim template if exists
    if [ -f "$PROJECT_ROOT/config/zai.yaml.template" ]; then
        cp "$PROJECT_ROOT/config/zai.yaml.template" "$INSTALL_DIR/config/"
    fi

    echo -e "${GREEN}Configuration files installed${NC}"
}

# Copy scripts
copy_scripts() {
    echo -e "${BLUE}Installing scripts...${NC}"

    # Copy zasrctl
    if [ -f "$SCRIPT_DIR/zasrctl" ]; then
        cp "$SCRIPT_DIR/zasrctl" "$INSTALL_DIR/scripts/"
        chmod +x "$INSTALL_DIR/scripts/zasrctl"
    fi

    # Copy model download script
    if [ -f "$SCRIPT_DIR/download-models.sh" ]; then
        cp "$SCRIPT_DIR/download-models.sh" "$INSTALL_DIR/scripts/"
        chmod +x "$INSTALL_DIR/scripts/download-models.sh"
    fi

    # Copy model check script
    if [ -f "$SCRIPT_DIR/check-models.sh" ]; then
        cp "$SCRIPT_DIR/check-models.sh" "$INSTALL_DIR/scripts/"
        chmod +x "$INSTALL_DIR/scripts/check-models.sh"
    fi

    echo -e "${GREEN}Scripts installed${NC}"
}

# Download models
download_models() {
    echo -e "${BLUE}Downloading models...${NC}"

    if [ -f "$INSTALL_DIR/scripts/download-models.sh" ]; then
        bash "$INSTALL_DIR/scripts/download-models.sh" --dir "$INSTALL_DIR/models"
    else
        echo -e "${YELLOW}Warning: download-models.sh not found${NC}"
        echo "Please download models manually"
    fi
}

# Create README
create_readme() {
    cat > "$INSTALL_DIR/README.md" << 'EOF'
# ZASR Server Installation

This directory contains the ZASR (Streaming ASR) server installation.

## Directory Structure

```
.
├── bin/           # Server binary
├── config/        # Configuration files
├── models/        # Model files
├── scripts/       # Control scripts
├── var/           # Runtime files (PID, etc.)
└── logs/          # Log files
```

## Quick Start

1. **Start the server:**
   ```bash
   ./scripts/zasrctl start
   ```

2. **Check status:**
   ```bash
   ./scripts/zasrctl status
   ```

3. **Stop the server:**
   ```bash
   ./scripts/zasrctl stop
   ```

## Configuration

Edit `config/default.yaml` to customize server settings.

Main configuration options:
- `server.port`: WebSocket server port (default: 2026)
- `asr.type`: Recognizer type ("sense-voice" or "streaming-zipformer")
- `vad.model`: Path to VAD model
- `asr.sense_voice.model`: Path to SenseVoice model

## Models

Download models using the provided script:
```bash
./scripts/download-models.sh
```

Or manually download to `models/` directory:
- SenseVoice: `https://github.com/k2-fsa/sherpa-onnx/releases`
- Streaming Zipformer: `https://github.com/k2-fsa/sherpa-onnx/releases`

## Integration

To integrate with other applications:

```bash
export DEPLOY_DIR=$(pwd)
./scripts/zasrctl start -c config/custom.yaml
```

## Troubleshooting

Check logs:
```bash
tail -f logs/zasr.log
```

Verify models:
```bash
./scripts/check-models.sh
```

## More Information

- Project: https://github.com/your-username/zasr
- sherpa-onnx: https://github.com/k2-fsa/sherpa-onnx
EOF

    echo -e "${GREEN}README created${NC}"
}

# Print summary
print_summary() {
    echo ""
    echo -e "${GREEN}=== Installation Complete ===${NC}"
    echo ""
    echo "Installation directory: $INSTALL_DIR"
    echo ""
    echo "Next steps:"
    echo "  1. Download models:"
    echo "     $INSTALL_DIR/scripts/download-models.sh"
    echo ""
    echo "  2. Edit configuration:"
    echo "     vim $INSTALL_DIR/config/default.yaml"
    echo ""
    echo "  3. Start server:"
    echo "     $INSTALL_DIR/scripts/zasrctl start"
    echo ""
    if [ "$WITH_INTEGRATION" = true ]; then
        echo "Integration mode:"
        echo "  export DEPLOY_DIR=$INSTALL_DIR"
        echo "  export CONFIG=\$DEPLOY_DIR/config/default.yaml"
        echo ""
    fi
}

# Main execution
main() {
    echo -e "${BLUE}ZASR Server Installation${NC}"
    echo "========================"
    echo ""

    parse_args "$@"

    # Config-only mode
    if [ "$CONFIG_ONLY" = true ]; then
        echo -e "${BLUE}Config-only mode${NC}"
        create_directories
        copy_configs
        copy_scripts
        create_readme
        print_summary
        return 0
    fi

    # Check directory
    check_directory

    # Create directories
    create_directories

    # Build or copy binary
    if [ "$FROM_SOURCE" = true ]; then
        build_from_source
        copy_binary
    elif [ "$FROM_BINARY" = true ]; then
        copy_binary
    else
        # Try to detect if build exists
        if [ -f "$PROJECT_ROOT/build/zasr-server" ]; then
            echo -e "${YELLOW}Found existing build, using binary...${NC}"
            copy_binary
        else
            echo -e "${YELLOW}No binary found, building from source...${NC}"
            build_from_source
            copy_binary
        fi
    fi

    # Copy configs and scripts
    copy_configs
    copy_scripts

    # Download models if requested
    if [ "$DOWNLOAD_MODELS" = true ]; then
        download_models
    else
        echo -e "${YELLOW}Note: Models not downloaded. Run the following to download models:${NC}"
        echo "  $INSTALL_DIR/scripts/download-models.sh"
    fi

    # Create README
    create_readme

    # Print summary
    print_summary
}

main "$@"
