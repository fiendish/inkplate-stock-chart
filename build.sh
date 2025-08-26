#!/bin/bash

# Build script for Inkplate Stock Chart
# This script reads environment variables and passes them as compiler defines
# Usage: ./build.sh [--upload-only]
# --upload-only: Skip build and only upload existing build

# Parse command line arguments
UPLOAD_ONLY=false
if [ "$1" = "--upload-only" ]; then
    UPLOAD_ONLY=true
    echo "Upload-only mode: Skipping build, uploading existing binary..."
fi

# Check for required environment variables (only if building)
if [ "$UPLOAD_ONLY" = false ]; then
    if [ -z "$ALPHA_VANTAGE_API_KEY" ]; then
        echo "Error: ALPHA_VANTAGE_API_KEY environment variable not set"
        echo "Set it with: export ALPHA_VANTAGE_API_KEY='your_key_here'"
        exit 1
    fi
fi

# Auto-detect WiFi SSID if not set (only if building)
if [ "$UPLOAD_ONLY" = false ] && [ -z "$WIFI_SSID" ]; then
    echo "Auto-detecting current WiFi network..."
    WIFI_SSID=$(system_profiler SPAirPortDataType | grep -A 1 "Current Network Information:" | grep -v "Current Network Information:" | grep -v "Network Type:" | awk -F: '{print $1}' | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//' | head -n 1)
    
    if [ -n "$WIFI_SSID" ]; then
        echo "Detected WiFi network: $WIFI_SSID"
    else
        echo "Error: Could not detect WiFi network name"
        echo "Set it with: export WIFI_SSID='YourWiFiName'"
        exit 1
    fi
fi

# Auto-retrieve WiFi password from Keychain if not set (only if building)
if [ "$UPLOAD_ONLY" = false ] && [ -z "$WIFI_PASSWORD" ]; then
    echo "Retrieving WiFi password from Keychain for network '$WIFI_SSID'..."
    
    # Try to get password from Keychain (may prompt for Touch ID)
    WIFI_PASSWORD=$(security find-generic-password -wa "$WIFI_SSID")
    
    if [ -z "$WIFI_PASSWORD" ]; then
        echo "Could not automatically retrieve WiFi password from Keychain."
        echo "Please set the WiFi password manually:"
        read -s -p "Enter WiFi password for '$WIFI_SSID': " WIFI_PASSWORD
        echo
        
        if [ -z "$WIFI_PASSWORD" ]; then
            echo "Error: No WiFi password provided"
            exit 1
        fi
    else
        echo "Retrieved WiFi password from Keychain (length: ${#WIFI_PASSWORD} characters)"
    fi
fi

# Only build if not upload-only mode
if [ "$UPLOAD_ONLY" = false ]; then
    echo "Building with environment variables..."
    echo "API Key: ${ALPHA_VANTAGE_API_KEY:0:8}..."
    echo "WiFi SSID: $WIFI_SSID"

    # Use arduino-cli to compile with environment variables as defines
    arduino-cli compile \
        --fqbn Inkplate_Boards:esp32:Inkplate5V2:UploadSpeed=115200 \
        --build-property "compiler.cpp.extra_flags=-DALPHA_VANTAGE_API_KEY=\"$ALPHA_VANTAGE_API_KEY\" -DWIFI_SSID=\"$WIFI_SSID\" -DWIFI_PASSWORD=\"$WIFI_PASSWORD\"" \
        --output-dir ./build \
        inkplate_stock_chart.ino

    if [ $? -eq 0 ]; then
        echo "Build successful!"
    else
        echo "Build failed!"
        exit 1
    fi
fi

# Check if build directory exists
if [ ! -d "./build" ]; then
    echo "Error: Build directory not found. Run without --upload-only first."
    exit 1
fi

# Auto-detect USB serial port
echo "Detecting USB serial ports..."
USB_PORT=$(arduino-cli board list | grep "Serial Port (USB)" | awk '{print $1}' | head -n 1)

if [ -z "$USB_PORT" ]; then
    echo "Error: No USB serial port found. Connect the Inkplate device."
    echo "Available ports:"
    arduino-cli board list
    exit 1
fi

echo "Detected USB port: $USB_PORT"

# Erase flash before uploading to ensure clean state
echo "Erasing flash..."

# Use uv to run esptool
if command -v uv >/dev/null 2>&1; then
    uv run esptool --port "$USB_PORT" erase-flash
else
    echo "Error: uv not found. Install with: curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

if [ $? -eq 0 ]; then
    echo "Flash erased successfully!"
else
    echo "Flash erase failed!"
    exit 1
fi

echo "Uploading..."
echo "Command: arduino-cli upload --fqbn Inkplate_Boards:esp32:Inkplate5V2:UploadSpeed=115200 --port $USB_PORT --input-dir ./build"
arduino-cli upload --fqbn Inkplate_Boards:esp32:Inkplate5V2:UploadSpeed=115200 --port $USB_PORT --input-dir ./build

if [ $? -eq 0 ]; then
    echo "Upload successful!"
    echo "Monitoring boot process..."
    echo "Command: arduino-cli monitor --port $USB_PORT --config baudrate=115200"
    arduino-cli monitor --port $USB_PORT --config baudrate=115200
else
    echo "Upload failed!"
    exit 1
fi