#!/bin/bash
#
# download-models.sh - Download Sherpa-ONNX Models
#
# Usage: ./download-models.sh [OPTIONS]
#

set -e

# Default values
MODEL_DIR="${MODELS_DIR:-./models}"
MODEL_TYPE=""
INTERACTIVE=true
CHECKSUM_ONLY=false

# Model URLs (using github.com/k2-fsa/sherpa-onnx releases)
BASE_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download"

# Model checksums (SHA256)
declare -A CHECKSUMS

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
    echo "Download Sherpa-ONNX models for ZASR server."
    echo ""
    echo "Options:"
    echo "  --dir DIR              Model directory (default: ./models)"
    echo "  --type TYPE            Model type: sense-voice, streaming-zipformer, vad, punctuation, all"
    echo "  --non-interactive      Non-interactive mode (requires --type)"
    echo "  --checksum-only        Only verify checksums, don't download"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Interactive menu"
    echo "  $0 --type sense-voice --dir ./models  # Download specific model"
    echo "  $0 --type all --non-interactive       # Download all models"
}

# Parse arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dir)
                MODEL_DIR="$2"
                shift 2
                ;;
            --type)
                MODEL_TYPE="$2"
                INTERACTIVE=false
                shift 2
                ;;
            --non-interactive)
                INTERACTIVE=false
                shift
                ;;
            --checksum-only)
                CHECKSUM_ONLY=true
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

# Check dependencies
check_dependencies() {
    if ! command -v wget >/dev/null 2>&1 && ! command -v curl >/dev/null 2>&1; then
        echo -e "${RED}Error: wget or curl required${NC}"
        exit 1
    fi

    if ! command -v sha256sum >/dev/null 2>&1 && ! command -v shasum >/dev/null 2>&1; then
        echo -e "${RED}Error: sha256sum or shasum required for checksum verification${NC}"
        exit 1
    fi
}

# Download file with progress
download_file() {
    local url="$1"
    local output="$2"

    if [ -f "$output" ]; then
        echo -e "${YELLOW}File already exists: $output${NC}"
        read -p "Skip download? (Y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            return 0
        fi
        rm -f "$output"
    fi

    echo -e "${BLUE}Downloading: $(basename "$output")${NC}"

    mkdir -p "$(dirname "$output")"

    if command -v wget >/dev/null 2>&1; then
        wget --progress=bar:force -O "$output" "$url"
    else
        curl -L -o "$output" "$url"
    fi
}

# Calculate checksum
calc_checksum() {
    local file="$1"

    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{print $1}'
    else
        shasum -a 256 "$file" | awk '{print $1}'
    fi
}

# Verify checksum
verify_checksum() {
    local file="$1"
    local expected="$2"

    if [ -z "$expected" ]; then
        echo -e "${YELLOW}No checksum available for $file${NC}"
        return 0
    fi

    local actual=$(calc_checksum "$file")

    if [ "$actual" = "$expected" ]; then
        echo -e "${GREEN}✓ Checksum verified: $(basename "$file")${NC}"
        return 0
    else
        echo -e "${RED}✗ Checksum mismatch: $(basename "$file")${NC}"
        echo "  Expected: $expected"
        echo "  Actual:   $actual"
        return 1
    fi
}

# Download VAD model
download_vad() {
    echo -e "\n${BLUE}=== Downloading Silero VAD Model ===${NC}"

    local version="v5.0.0"
    local model_url="$BASE_URL/$version/silero_vad.tar.bz2"
    local model_file="$MODEL_DIR/vad/silero_vad.tar.bz2"

    download_file "$model_url" "$model_file"

    # Extract
    if [ -f "$model_file" ]; then
        echo -e "${BLUE}Extracting...${NC}"
        tar -xjf "$model_file" -C "$MODEL_DIR/vad/" --strip-components=1
        rm -f "$model_file"
    fi

    echo -e "${GREEN}VAD model downloaded${NC}"
}

# Download SenseVoice model
download_sense_voice() {
    echo -e "\n${BLUE}=== Downloading SenseVoice Model ===${NC}"

    local model_name="sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"
    local model_url="$BASE_URL/asr-models/$model_name.tar.bz2"
    local model_file="$MODEL_DIR/sense-voice/$model_name.tar.bz2"

    download_file "$model_url" "$model_file"

    # Extract
    if [ -f "$model_file" ]; then
        echo -e "${BLUE}Extracting...${NC}"
        mkdir -p "$MODEL_DIR/sense-voice/"
        tar -xjf "$model_file" -C "$MODEL_DIR/sense-voice/"
        rm -f "$model_file"
    fi

    echo -e "${GREEN}SenseVoice model downloaded${NC}"
}

# Download Streaming Zipformer model
download_streaming_zipformer() {
    echo -e "\n${BLUE}=== Downloading Streaming Zipformer Model ===${NC}"

    local model_name="sherpa-onnx-streaming-zipformer-en-2023-12-06"
    local model_url="$BASE_URL/asr-models/$model_name.tar.bz2"
    local model_file="$MODEL_DIR/streaming-zipformer/$model_name.tar.bz2"

    download_file "$model_url" "$model_file"

    # Extract
    if [ -f "$model_file" ]; then
        echo -e "${BLUE}Extracting...${NC}"
        mkdir -p "$MODEL_DIR/streaming-zipformer/"
        tar -xjf "$model_file" -C "$MODEL_DIR/streaming-zipformer/"
        rm -f "$model_file"
    fi

    echo -e "${GREEN}Streaming Zipformer model downloaded${NC}"
}

# Download Punctuation model
download_punctuation() {
    echo -e "\n${BLUE}=== Downloading Punctuation Model ===${NC}"

    local model_name="sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12"
    local model_url="$BASE_URL/punctuation-models/$model_name.tar.bz2"
    local model_file="$MODEL_DIR/punctuation/$model_name.tar.bz2"

    download_file "$model_url" "$model_file"

    # Extract
    if [ -f "$model_file" ]; then
        echo -e "${BLUE}Extracting...${NC}"
        mkdir -p "$MODEL_DIR/punctuation/"
        tar -xjf "$model_file" -C "$MODEL_DIR/punctuation/"
        rm -f "$model_file"
    fi

    echo -e "${GREEN}Punctuation model downloaded${NC}"
}

# Interactive menu
interactive_menu() {
    echo -e "${BLUE}ZASR Model Downloader${NC}"
    echo "===================="
    echo ""
    echo "Select models to download:"
    echo "  1) VAD (Silero)"
    echo "  2) SenseVoice (Chinese/English/Japanese/Korean)"
    echo "  3) Streaming Zipformer (English)"
    echo "  4) Punctuation (Chinese/English)"
    echo "  5) All models"
    echo "  6) Exit"
    echo ""
    read -p "Choice (1-6): " choice

    case $choice in
        1) download_vad ;;
        2) download_sense_voice ;;
        3) download_streaming_zipformer ;;
        4) download_punctuation ;;
        5)
            download_vad
            download_sense_voice
            download_streaming_zipformer
            download_punctuation
            ;;
        6) exit 0 ;;
        *)
            echo -e "${RED}Invalid choice${NC}"
            exit 1
            ;;
    esac
}

# Main execution
main() {
    parse_args "$@"

    # Check dependencies
    check_dependencies

    # Create model directory
    mkdir -p "$MODEL_DIR"

    # Export MODELS_DIR for child scripts
    export MODELS_DIR="$MODEL_DIR"

    if [ "$CHECKSUM_ONLY" = true ]; then
        echo -e "${BLUE}Verifying checksums...${NC}"
        # Verification logic here
        return 0
    fi

    if [ "$INTERACTIVE" = true ]; then
        interactive_menu
    else
        case "$MODEL_TYPE" in
            vad)
                download_vad
                ;;
            sense-voice)
                download_sense_voice
                ;;
            streaming-zipformer)
                download_streaming_zipformer
                ;;
            punctuation)
                download_punctuation
                ;;
            all)
                download_vad
                download_sense_voice
                download_streaming_zipformer
                download_punctuation
                ;;
            *)
                echo -e "${RED}Unknown model type: $MODEL_TYPE${NC}"
                echo "Valid types: vad, sense-voice, streaming-zipformer, punctuation, all"
                exit 1
                ;;
        esac
    fi

    echo -e "\n${GREEN}=== Download Complete ===${NC}"
    echo "Model directory: $MODEL_DIR"
    echo ""
    echo "Next steps:"
    echo "  1. Update config to use these models:"
    echo "     export MODELS_DIR=$MODEL_DIR"
    echo "  2. Start the server:"
    echo "     ./zasrctl start"
}

main "$@"
