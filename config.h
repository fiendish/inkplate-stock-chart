#ifndef CONFIG_H
#define CONFIG_H

// Environment variables are injected during compilation as compiler defines
// Fallback to placeholder values if not provided during build
#ifndef ALPHA_VANTAGE_API_KEY
  #define ALPHA_VANTAGE_API_KEY "SET_ALPHA_VANTAGE_API_KEY_ENV_VAR"
#endif

#ifndef WIFI_SSID  
  #define WIFI_SSID "SET_WIFI_SSID_ENV_VAR"
#endif

#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "SET_WIFI_PASSWORD_ENV_VAR" 
#endif

#define ALPHA_VANTAGE_BASE_URL "https://www.alphavantage.co/query?"
#define ALPHA_VANTAGE_RETRY_DELAY_MS 15000
#define ALPHA_VANTAGE_MAX_ATTEMPTS 2
#define STOCK_FETCH_FAILURE_DELAY_HOURS 6

// Stock Configuration
#define MAX_STOCKS 4
const char* STOCK_SYMBOLS[MAX_STOCKS] = {
  "SPY",      // SPDR S&P 500 ETF
  "",         // Add more symbols as needed
  "",
  "",
};

// Update Schedule
#define UPDATE_HOUR 19    // Update at 7:00 PM
#define UPDATE_MINUTE 0

// Time Configuration
#define GMT_OFFSET_SEC -18000     // Eastern Time (UTC-5)
#define DAYLIGHT_OFFSET_SEC 3600  // Daylight saving time offset
#define NTP_SERVER "pool.ntp.org"

// Display Configuration
#define CHART_MARGIN 20
#define TITLE_HEIGHT 20

// Debug Configuration
#define DEBUG_SERIAL false  // Set to false to disable serial output for battery optimization

// Display Layout Constants
#define STATUS_LINE_HEIGHT 25
#define STATUS_TOP_MARGIN 30

// Battery Monitoring Constants  
#define LOW_BATTERY_THRESHOLD 3.4
#define MIN_VALID_BATTERY_READING 0.1

// Network Constants
#define WIFI_TIMEOUT_MS 20000
#define WIFI_RETRY_DELAY_MS 30000
#define NTP_SYNC_MAX_ATTEMPTS 15
#define NTP_SYNC_RETRY_INTERVAL_MS 1000

// Text Size Constants
#define TEXT_SIZE 3

// Wakeup History Constants
#define WAKEUP_HISTORY_SIZE 5

// Stock Data Constants
#define MONTH_DATA_POINTS 22
#define YEAR_DATA_POINTS 252
#define INITIAL_MIN_PRICE 99999
#define PROGRESS_BAR_WIDTH 12
#define CHAR_WIDTH_ESTIMATE 18

// Chart Drawing Constants
#define CHART_LINE_THICKNESS 4
#define CHART_BORDER_THICKNESS 2

#endif