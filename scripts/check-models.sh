#!/bin/bash
#
# ZASR - Streaming ASR Server
# Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
#
# Licensed under the MIT License
#
# check-models.sh - Verify Sherpa-ONNX Models
#
# Usage: ./check-models.sh [OPTIONS]
#

set -e

# Default values
MODEL_DIR="${MODELS_DIR:-./models}"
CONFIG="${CONFIG:-./config/default.yaml}"
VERBOSE=false

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
    echo "Verify Sherpa-ONNX models for ZASR server."
    echo ""
    echo "Options:"
    echo "  --dir DIR              Model directory (default: ./models)"
    echo "  --config FILE          Configuration file (default: ./config/default.yaml)"
    echo "  -v, --verbose          Show detailed output"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Check default models"
    echo "  $0 --dir /opt/zasr/models            # Check specific directory"
    echo "  $0 --config config/custom.yaml       # Check models from config"
}

# Parse arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dir)
                MODEL_DIR="$2"
                shift 2
                ;;
            --config)
                CONFIG="$2"
                shift 2
                ;;
            -v|--verbose)
                VERBOSE=true
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

# Check if file exists
check_file() {
    local file="$1"
    local description="$2"

    if [ -f "$file" ]; then
        local size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "unknown")
        local size_mb=$(echo "scale=2; $size / 1024 / 1024" | bc 2>/dev/null || echo "N/A")

        echo -e "${GREEN}✓${NC} $description"
        if [ "$VERBOSE" = true ]; then
            echo "    Path: $file"
            echo "    Size: ${size_mb} MB"
        fi
        return 0
    else
        echo -e "${RED}✗${NC} $description"
        if [ "$VERBOSE" = true ]; then
            echo "    Expected: $file"
        fi
        return 1
    fi
}

# Check VAD models
check_vad() {
    echo -e "\n${BLUE}VAD Models (Silero)${NC}"
    echo "---------------------"

    local found=0

    # Common VAD model paths
    local vad_paths=(
        "$MODEL_DIR/vad/silero_vad.int8.onnx"
        "$MODEL_DIR/vad/silero_vad.onnx"
        "$HOME/.cache/sherpa-onnx/silero_vad.int8.onnx"
        "$HOME/.cache/sherpa-onnx/vad/silero_vad.int8.onnx"
    )

    for path in "${vad_paths[@]}"; do
        if check_file "$path" "Silero VAD"; then
            found=1
            break
        fi
    done

    if [ $found -eq 0 ]; then
        return 1
    fi
}

# Check SenseVoice models
check_sense_voice() {
    echo -e "\n${BLUE}SenseVoice Models${NC}"
    echo "-------------------"

    local found=0

    # Common SenseVoice model paths
    local model_paths=(
        "$MODEL_DIR/sense-voice/model.int8.onnx"
        "$MODEL_DIR/sense-voice/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/model.int8.onnx"
        "$HOME/.cache/sherpa-onnx/sense-voice/model.int8.onnx"
    )

    local tokens_paths=(
        "$MODEL_DIR/sense-voice/tokens.txt"
        "$MODEL_DIR/sense-voice/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/tokens.txt"
        "$HOME/.cache/sherpa-onnx/sense-voice/tokens.txt"
    )

    for path in "${model_paths[@]}"; do
        if check_file "$path" "SenseVoice Model"; then
            found=1
            break
        fi
    done

    for path in "${tokens_paths[@]}"; do
        if check_file "$path" "SenseVoice Tokens"; then
            found=$((found + 1))
            break
        fi
    done

    if [ $found -lt 2 ]; then
        return 1
    fi
}

# Check Streaming Zipformer models
check_streaming_zipformer() {
    echo -e "\n${BLUE}Streaming Zipformer Models${NC}"
    echo "----------------------------"

    local found=0

    # Common Streaming Zipformer model paths
    local encoder_paths=(
        "$MODEL_DIR/streaming-zipformer/encoder-epoch-99-avg-1.onnx"
        "$MODEL_DIR/streaming-zipformer/encoder.onnx"
        "$HOME/.cache/sherpa-onnx/streaming-zipformer/encoder-epoch-99-avg-1.onnx"
    )

    local decoder_paths=(
        "$MODEL_DIR/streaming-zipformer/decoder-epoch-99-avg-1.onnx"
        "$MODEL_DIR/streaming-zipformer/decoder.onnx"
        "$HOME/.cache/sherpa-onnx/streaming-zipformer/decoder-epoch-99-avg-1.onnx"
    )

    local joiner_paths=(
        "$MODEL_DIR/streaming-zipformer/joiner-epoch-99-avg-1.onnx"
        "$MODEL_DIR/streaming-zipformer/joiner.onnx"
        "$HOME/.cache/sherpa-onnx/streaming-zipformer/joiner-epoch-99-avg-1.onnx"
    )

    local tokens_paths=(
        "$MODEL_DIR/streaming-zipformer/tokens.txt"
        "$MODEL_DIR/streaming-zipformer/data/lang/tokens.txt"
        "$HOME/.cache/sherpa-onnx/streaming-zipformer/tokens.txt"
    )

    for path in "${encoder_paths[@]}"; do
        if check_file "$path" "Encoder"; then
            found=1
            break
        fi
    done

    for path in "${decoder_paths[@]}"; do
        if check_file "$path" "Decoder"; then
            found=$((found + 1))
            break
        fi
    done

    for path in "${joiner_paths[@]}"; do
        if check_file "$path" "Joiner"; then
            found=$((found + 1))
            break
        fi
    done

    for path in "${tokens_paths[@]}"; do
        if check_file "$path" "Tokens"; then
            found=$((found + 1))
            break
        fi
    done

    if [ $found -lt 4 ]; then
        return 1
    fi
}

# Check Punctuation models
check_punctuation() {
    echo -e "\n${BLUE}Punctuation Models${NC}"
    echo "--------------------"

    local found=0

    # Common Punctuation model paths
    local model_paths=(
        "$MODEL_DIR/punctuation/model.onnx"
        "$MODEL_DIR/punctuation/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12/model.onnx"
        "$HOME/.cache/sherpa-onnx/punctuation/model.onnx"
    )

    for path in "${model_paths[@]}"; do
        if check_file "$path" "Punctuation Model"; then
            found=1
            break
        fi
    done

    if [ $found -eq 0 ]; then
        return 1
    fi
}

# Check models from config
check_from_config() {
    if [ ! -f "$CONFIG" ]; then
        echo -e "${YELLOW}Config file not found: $CONFIG${NC}"
        return 1
    fi

    echo -e "\n${BLUE}Checking models from config${NC}"
    echo "Config: $CONFIG"

    # Extract model paths from config
    local vad_model=$(grep -E "vad\.model:" "$CONFIG" | awk '{print $2}' | tr -d '"')
    local sense_voice_model=$(grep -E "sense_voice\.model:" "$CONFIG" | awk '{print $2}' | tr -d '"')
    local tokens=$(grep -E "tokens:" "$CONFIG" | awk '{print $2}' | tr -d '"')

    # Expand environment variables
    vad_model=$(eval echo "$vad_model")
    sense_voice_model=$(eval echo "$sense_voice_model")
    tokens=$(eval echo "$tokens")

    if [ -n "$vad_model" ]; then
        check_file "$vad_model" "VAD Model (from config)"
    fi

    if [ -n "$sense_voice_model" ]; then
        check_file "$sense_voice_model" "SenseVoice Model (from config)"
    fi

    if [ -n "$tokens" ]; then
        check_file "$tokens" "Tokens (from config)"
    fi
}

# Print summary
print_summary() {
    local errors=$1

    echo ""
    echo "===================="
    if [ $errors -eq 0 ]; then
        echo -e "${GREEN}All models verified!${NC}"
    else
        echo -e "${YELLOW}Some models are missing${NC}"
        echo ""
        echo "To download missing models, run:"
        echo "  ./scripts/download-models.sh"
    fi
}

# Main execution
main() {
    parse_args "$@"

    echo -e "${BLUE}ZASR Model Verification${NC}"
    echo "========================="
    echo "Model directory: $MODEL_DIR"

    local errors=0

    # Check all model types
    check_vad || errors=$((errors + 1))
    check_sense_voice || errors=$((errors + 1))
    check_streaming_zipformer || errors=$((errors + 1))
    check_punctuation || errors=$((errors + 1))

    # Also check from config if provided
    if [ -f "$CONFIG" ]; then
        check_from_config || errors=$((errors + 1))
    fi

    print_summary $errors

    exit $errors
}

main "$@"
