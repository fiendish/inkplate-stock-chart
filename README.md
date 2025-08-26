# Inkplate Stock Chart Display

E-ink stock chart display for the Soldered Inkplate 5 v2. Updates once daily with 1-month and 1-year price charts.

## Quick Start

1. **Get Alpha Vantage API key** (free at [alphavantage.co](https://www.alphavantage.co/support/#api-key))

2. **Set environment variables**:
   ```bash
   export WIFI_SSID="YourNetwork"
   export WIFI_PASSWORD="YourPassword"  
   export ALPHA_VANTAGE_API_KEY="YOUR_KEY"
   ```

3. **Install dependencies**:
   ```bash
   # Install Arduino CLI with Homebrew
   brew install arduino-cli

   # Add board manager URL
   arduino-cli config add board_manager.additional_urls https://github.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/raw/master/package_Dasduino_Boards_index.json
   
   # Update and install board definitions
   arduino-cli core update-index
   arduino-cli core install Inkplate_Boards:esp32
   
   # Install libraries
   arduino-cli lib install InkplateLibrary
   arduino-cli lib install "CSV Parser"
   ```

4. **Build and upload**:
   ```bash
   ./build.sh
   ```

## Configuration

Edit `config.h` to customize:
- Stock symbols
- Update time
