#include "Inkplate.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <CSV_Parser.h>

#include "config.h"

// RTC memory for wakeup history
RTC_DATA_ATTR time_t wakeup_history[WAKEUP_HISTORY_SIZE];
RTC_DATA_ATTR int wakeup_index = 0;
RTC_DATA_ATTR int wakeup_count = 0;

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTF(fmt, ...)
  #define DEBUG_PRINTLN(x)
#endif

Inkplate inkplate(INKPLATE_1BIT);

int max_status_lines = 0;
struct StockData {
  String symbol;
  float prices_1month[MONTH_DATA_POINTS];
  float prices_1year[YEAR_DATA_POINTS];
  int data_points_1month = 0;
  int data_points_1year = 0;
  float min_price_1month = INITIAL_MIN_PRICE;
  float max_price_1month = 0;
  float min_price_1year = INITIAL_MIN_PRICE;
  float max_price_1year = 0;
  String latest_data_date = "";
  bool using_open_price = false;
};

StockData stocks[MAX_STOCKS];
int num_stocks = 0;
int status_output_inkplate_line = 0;

void initializeStockSymbols();
void connectToWiFi();
bool fetchStockData(int stock_index);
void updatePriceData(float price, float prices[], int& data_points, int max_points, float& min_price, float& max_price);
void reverseArray(float arr[], int size);
void fetchStocks();
void displayStocks();
void drawStockCharts();
void drawStockChart(StockData &stock, int x, int y, int width, int height, bool show_labels);
void drawLineChart(float data[], int data_points, float min_val, float max_val, int x, int y, int width, int height);
void drawTimestamp();
void printStatusLine(const char* format, ...);
void printStatusProgress(const char* message, int current, int total);
void inkplateBatteryVoltage();
void goToDeepSleep();
void drawThickLine(int x0, int y0, int x1, int y1, int thickness);
void recordWakeupTime();
void displayWakeupHistory();
bool isPointInThickLine(int px, int py, int x0, int y0, int x1, int y1, int thickness);

// Initialize hardware and fetch stock data once per day
void setup() {
  max_status_lines = (inkplate.height() - STATUS_TOP_MARGIN) / STATUS_LINE_HEIGHT;
  if (DEBUG_SERIAL) {
    Serial.begin(115200);
  }
  
  inkplate.begin();
  
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);  // Enable wake button
  DEBUG_PRINTLN("Wake button enabled");

  inkplate.clearDisplay();
  inkplateBatteryVoltage();
  inkplate.partialUpdate();
  
  initializeStockSymbols();
  connectToWiFi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  struct tm timeinfo;
  int sync_attempts = 0;
  while (!getLocalTime(&timeinfo) && sync_attempts < NTP_SYNC_MAX_ATTEMPTS) {
    delay(1000);
    sync_attempts++;
    printStatusProgress("Syncing time", sync_attempts, NTP_SYNC_MAX_ATTEMPTS);
  }
  
  if (getLocalTime(&timeinfo)) {
    printStatusLine("Time synced!");
    recordWakeupTime();
    displayWakeupHistory();
  } else {
    printStatusLine("Time sync failed. Sleeping for an hour.");
    // sleep for an hour
    esp_sleep_enable_timer_wakeup(60 * 60 * 1000000ULL);
    esp_deep_sleep_start();
  }
  
  fetchStocks();
  displayStocks();
  goToDeepSleep();
}

// Required by Arduino framework but never reached due to deep sleep
void loop() {
}

// Load stock symbols from config array
void initializeStockSymbols() {
  for (int i = 0; i < MAX_STOCKS && i < sizeof(STOCK_SYMBOLS)/sizeof(STOCK_SYMBOLS[0]); i++) {
    if (strlen(STOCK_SYMBOLS[i]) > 0) {
      stocks[i].symbol = STOCK_SYMBOLS[i];
      num_stocks++;
    }
  }
  
}

// Connect to WiFi
void connectToWiFi() {
  printStatusLine("Enabling WiFi...");
  WiFi.mode(WIFI_MODE_STA);
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  String statusMsg = "Connecting to ";
  statusMsg += WIFI_SSID;
  int elapsed = 0;
  do {
    printStatusProgress(statusMsg.c_str(), elapsed, WIFI_TIMEOUT_MS);
    delay(1000);
    elapsed += 1000;
  } while (WiFi.status() != WL_CONNECTED && elapsed < WIFI_TIMEOUT_MS);
  
  if (WiFi.status() == WL_CONNECTED) {
    printStatusLine("WiFi Connected!");
    return;
  }
  
  printStatusLine("WiFi failed.");
  printStatusLine("Retrying in 30 sec...");
  
  esp_sleep_enable_timer_wakeup(WIFI_RETRY_DELAY_MS * 1000ULL);
  esp_deep_sleep_start();
}

// Fetch and parse CSV stock data from Alpha Vantage API with retry
bool fetchStockData(int stock_index) {
  if (stock_index >= num_stocks) return false;
  
  String symbol = stocks[stock_index].symbol;
  String url_full = String(ALPHA_VANTAGE_BASE_URL) +
                    "function=TIME_SERIES_DAILY" +
                    "&symbol=" + symbol +
                    "&outputsize=full" +
                    "&datatype=csv" +
                    "&apikey=" + String(ALPHA_VANTAGE_API_KEY);

  for (int attempt = 0; attempt < ALPHA_VANTAGE_MAX_ATTEMPTS; attempt++) {
    String csvData = "";
    printStatusLine("Calling API for %s...", symbol.c_str());
    
    HTTPClient http;
    http.begin(url_full);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      printStatusLine("Downloading %s data...", symbol.c_str());
      
      WiFiClient* stream = http.getStreamPtr();
      String line;
      int rows_read = 0;
      bool is_header = true;
      
      while (stream->available() && rows_read < YEAR_DATA_POINTS) {
        line = stream->readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        csvData += line + "\n";
        
        if (!is_header) {
          rows_read++;
          if (rows_read % 12 == 0) {
            printStatusProgress("Downloading data", rows_read, YEAR_DATA_POINTS);
          }
        } else {
          is_header = false;
        }
      }
      
    } else if (httpCode == 429) {
      printStatusLine("Rate limited for %s (HTTP 429), attempt %d/%d", 
                      symbol.c_str(), attempt + 1, ALPHA_VANTAGE_MAX_ATTEMPTS);
    } else {
      printStatusLine("HTTP error for %s: %d (attempt %d/%d)", 
                      symbol.c_str(), httpCode, attempt + 1, ALPHA_VANTAGE_MAX_ATTEMPTS);
    }
    
    http.end();
    
    // Data validation section
    DEBUG_PRINTF("CSV data length: %d\n", csvData.length());
    if (csvData.length() > 0) {
      String error_message = "";
      bool parse_success = false;
      
      if (csvData.indexOf("Error Message") != -1 || csvData.indexOf("Note:") != -1) {
        error_message = "API Error: Check API key/symbol";
      } else {
        printStatusLine("Parsing %s data...", symbol.c_str());
        
        // Debug: show first 500 chars of CSV data
        DEBUG_PRINTF("CSV data preview: %.500s\n", csvData.c_str());
        
        stocks[stock_index].data_points_1month = 0;
        stocks[stock_index].data_points_1year = 0;
        stocks[stock_index].min_price_1month = INITIAL_MIN_PRICE;
        stocks[stock_index].max_price_1month = 0;
        stocks[stock_index].min_price_1year = INITIAL_MIN_PRICE;
        stocks[stock_index].max_price_1year = 0;
        stocks[stock_index].latest_data_date = "";
        stocks[stock_index].using_open_price = false;
        
        CSV_Parser cp(csvData.c_str(), "sffff-");
 
        int rows = cp.getRowsCount();
        printStatusLine("CSV Parser found %d rows", rows);
        
        if (rows > 0) {
          char **timestamps = (char**)cp["timestamp"];
          float *opens = (float*)cp["open"];
          float *closes = (float*)cp["close"];
          DEBUG_PRINTF("CSV Parser column access: timestamps=%p, opens=%p, closes=%p\n", timestamps, opens, closes);
          
          if (timestamps && opens && closes) {
            for (int i = 0; i < rows; i++) {
              float price = (closes[i] > 0) ? closes[i] : opens[i];
              
              if (price > 0) {
                if (i == 0) {
                  stocks[stock_index].latest_data_date = String(timestamps[i]);
                  stocks[stock_index].using_open_price = (closes[i] <= 0);
                }
                
                if (i < MONTH_DATA_POINTS) {
                  updatePriceData(price, stocks[stock_index].prices_1month, stocks[stock_index].data_points_1month, 
                                 MONTH_DATA_POINTS, stocks[stock_index].min_price_1month, stocks[stock_index].max_price_1month);
                }
                
                updatePriceData(price, stocks[stock_index].prices_1year, stocks[stock_index].data_points_1year, 
                               YEAR_DATA_POINTS, stocks[stock_index].min_price_1year, stocks[stock_index].max_price_1year);
              }
            }
            
            printStatusProgress("Processing data", rows, YEAR_DATA_POINTS);
            reverseArray(stocks[stock_index].prices_1month, stocks[stock_index].data_points_1month);
            reverseArray(stocks[stock_index].prices_1year, stocks[stock_index].data_points_1year);
            
            printStatusLine("Final data points: 1month=%d, 1year=%d", 
                           stocks[stock_index].data_points_1month, stocks[stock_index].data_points_1year);
            
            if (stocks[stock_index].data_points_1year > 0) {
              printStatusLine("%s data ready!", symbol.c_str());
              return true;
            } else {
              error_message = "Parse Error: No valid price data found";
            }
          } else {
            error_message = "Parse Error: Invalid CSV data structure";
          }
        } else {
          error_message = "Parse Error: No valid CSV data found";
        }
      }
      
      // Consolidated error handling - print CSV contents for all validation failures
      if (error_message.length() > 0) {
        printStatusLine(error_message.c_str());
        printStatusLine("CSV contents (first 500 chars):");
        printStatusLine("%.500s", csvData.c_str());
      }
    }
    
    // Retry delay (if not last attempt)
    if (attempt < ALPHA_VANTAGE_MAX_ATTEMPTS - 1) {
      int delay_ms = ALPHA_VANTAGE_RETRY_DELAY_MS * (1 << attempt);
      int total_seconds = delay_ms / 1000;
      for (int elapsed = 0; elapsed < total_seconds; elapsed++) {
        printStatusProgress("Retry in", elapsed, total_seconds);
        delay(1000);
      }
    }
  }
  
  printStatusLine("Failed to fetch valid data for %s after %d attempts", 
                  symbol.c_str(), ALPHA_VANTAGE_MAX_ATTEMPTS);
  return false;
}

// Update price data array with min/max tracking
void updatePriceData(float price, float prices[], int& data_points, int max_points, float& min_price, float& max_price) {
  if (data_points < max_points) {
    prices[data_points] = price;
    data_points++;
    if (price < min_price) min_price = price;
    if (price > max_price) max_price = price;
  }
}

// Reverse array elements in place
void reverseArray(float arr[], int size) {
  for (int i = 0; i < size / 2; i++) {
    float temp = arr[i];
    arr[i] = arr[size - 1 - i];
    arr[size - 1 - i] = temp;
  }
}

// Fetch stock data for all configured symbols
void fetchStocks() {
  printStatusLine("Fetching stock data...");
  bool any_failed = false;
  
  for (int i = 0; i < num_stocks; i++) {
    if (fetchStockData(i)) {
      printStatusLine("%s - OK", stocks[i].symbol.c_str());
    } else {
      printStatusLine("%s - Failed", stocks[i].symbol.c_str());
      any_failed = true;
    }
  }
  
  if (any_failed) {
    printStatusLine("Some stocks failed to fetch");
    printStatusLine("Waiting %d hours before retry...", STOCK_FETCH_FAILURE_DELAY_HOURS);
    
    // Go to deep sleep for the specified hours
    uint64_t sleep_microseconds = STOCK_FETCH_FAILURE_DELAY_HOURS * 60 * 60 * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_microseconds);
    esp_deep_sleep_start();
  }
}

// Draw charts for all fetched stock data
void displayStocks() {
  printStatusLine("Drawing charts...");
  delay(500);
  drawStockCharts();
}

// Clear display and draw all stock charts with timestamps
void drawStockCharts() {
  inkplate.clearDisplay();
  drawTimestamp();
  int charts_per_row = (num_stocks <= 2) ? 1 : 2;
  int chart_width = (inkplate.width() - 40) / charts_per_row;
  int chart_height = (inkplate.height() - 160) / ((num_stocks + charts_per_row - 1) / charts_per_row); // More space for timestamps and bottom labels
  
  int total_rows = (num_stocks + charts_per_row - 1) / charts_per_row;
  
  for (int i = 0; i < num_stocks; i++) {
    int row = i / charts_per_row;
    int col = i % charts_per_row;
    
    int x = 20 + col * (chart_width + 20);
    int y = 93 + row * (chart_height + 30); // Move charts down more for both timestamp lines
    
    bool is_bottom_row = (row == total_rows - 1);
    if (is_bottom_row) {
      y += 8; // Move bottom row charts down additional 8 pixels
    }
    drawStockChart(stocks[i], x, y, chart_width, chart_height, is_bottom_row);
  }
  
  inkplate.display();
}

// Draw individual stock chart with symbol and line plots
void drawStockChart(StockData &stock, int x, int y, int width, int height, bool show_labels) {
  inkplate.setTextSize(4);
  inkplate.setTextColor(BLACK);
  inkplate.setCursor(x, y - 30);
  inkplate.println(stock.symbol);
  
  // Draw 1-month chart (left half)
  int chart_1month_width = width / 2 - 10;
  drawLineChart(stock.prices_1month, stock.data_points_1month, 
                stock.min_price_1month, stock.max_price_1month,
                x, y + 2, chart_1month_width, height - 25);
  
  // Draw 1-year chart (right half)
  int chart_1year_width = width / 2 - 10;
  drawLineChart(stock.prices_1year, stock.data_points_1year, 
                stock.min_price_1year, stock.max_price_1year,
                x + width / 2 + 10, y + 2, chart_1year_width, height - 25);
  
  // X-axis labels (only show on bottom row charts)
  if (show_labels) {
    inkplate.setTextSize(4);
    // Month label in center of left chart
    inkplate.setCursor(x + chart_1month_width / 2 - 50, y + height - 8);
    inkplate.println("1 Month");
    // Year label in center of right chart
    inkplate.setCursor(x + width / 2 + 10 + chart_1year_width / 2 - 40, y + height - 8);
    inkplate.println("1 Year");
  }
}

void drawLineChart(float data[], int data_points, float min_val, float max_val, 
                   int x, int y, int width, int height) {
  if (data_points < 2) return;
  
  // Draw thick chart border
  inkplate.drawRect(x, y, width, height, BLACK);
  inkplate.drawRect(x + 1, y + 1, width - 2, height - 2, BLACK);
  
  float range = max_val - min_val;
  if (range == 0) range = 1; // Avoid division by zero
  
  // Draw tick marks
  if (data_points <= 30) {
    // Month plot - 4 (weekly) intervals
    for (int week = 1; week <= 3; week++) {
      int tick_pos = x + (week * width) / 4;
      drawThickLine(tick_pos, y + height - 1, tick_pos, y + height - 15, CHART_LINE_THICKNESS);
    }
  } else {
    // Year plot - 12 (monthly) intervals
    for (int month = 1; month <= 11; month++) {
      int tick_pos = x + (month * width) / 12;
      drawThickLine(tick_pos, y + height - 1, tick_pos, y + height - 15, CHART_LINE_THICKNESS);
    }
  }
  
  // Draw data points and lines
  for (int i = 0; i < data_points - 1; i++) {
    int x1 = x + (i * width) / (data_points - 1);
    int y1 = y + height - ((data[i] - min_val) / range) * height;
    int x2 = x + ((i + 1) * width) / (data_points - 1);
    int y2 = y + height - ((data[i + 1] - min_val) / range) * height;
    
    // Draw thick line using proper thick line function
    drawThickLine(x1, y1, x2, y2, CHART_LINE_THICKNESS);
  }
  
  // Add axis labels inside the chart
  inkplate.setTextSize(TEXT_SIZE);

  // Y-axis labels (price range)
  char maxLabel[10], minLabel[10], mid66Label[10], mid33Label[10];
  snprintf(maxLabel, sizeof(maxLabel), "%.0f", max_val);
  snprintf(minLabel, sizeof(minLabel), "%.0f", min_val);

  // Max price (top-left inside)
  inkplate.setCursor(x + 5, y + 5);
  inkplate.println(maxLabel);

  // 66% label (33% down from top)
  float val_66 = min_val + (range * 0.66);
  snprintf(mid66Label, sizeof(mid66Label), "%.0f", val_66);
  inkplate.setCursor(x + 5, y + (height / 3) - 5);
  inkplate.println(mid66Label);

  // 33% label (66% down from top)
  float val_33 = min_val + (range * 0.33);
  snprintf(mid33Label, sizeof(mid33Label), "%.0f", val_33);
  inkplate.setCursor(x + 5, y + (2 * height / 3) - 5);
  inkplate.println(mid33Label);

  // Min price (below bottom border, outside chart)
  inkplate.setCursor(x + 5, y + height + 8);
  inkplate.println(minLabel);
}

void drawTimestamp() {
  char timeStr[64];
  struct tm timeinfo;
  
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "Updated: %Y-%m-%d %H:%M", &timeinfo);
  } else {
    strcpy(timeStr, "Updated: UNKNOWN");
  }
  
  inkplate.setTextSize(TEXT_SIZE);
  // Center the timestamp at the top of the screen
  int text_width = strlen(timeStr) * CHAR_WIDTH_ESTIMATE;
  int x_center = (inkplate.width() - text_width) / 2;
  inkplate.setCursor(x_center, 8);
  inkplate.println(timeStr);
  
  // Show most recent data timestamp below main timestamp
  if (num_stocks > 0 && stocks[0].latest_data_date.length() > 0) {
    char dataTimeStr[64];
    const char* priceType = stocks[0].using_open_price ? "opening" : "closing";
    snprintf(dataTimeStr, sizeof(dataTimeStr), "Data as of: %s (%s)", 
             stocks[0].latest_data_date.c_str(), priceType);
    
    inkplate.setTextSize(TEXT_SIZE);
    int data_text_width = strlen(dataTimeStr) * CHAR_WIDTH_ESTIMATE;
    int x_center = (inkplate.width() - data_text_width) / 2;
    inkplate.setCursor(x_center, 40); // Move down slightly
    inkplate.println(dataTimeStr);
  }
  
  // Battery voltage in upper right corner
  inkplateBatteryVoltage();
}

void printStatusLine(const char* format, ...) {
  // If we run out of lines, clear status area and start over
  if (status_output_inkplate_line >= max_status_lines) {
    // Clear the status area
    inkplate.fillRect(0, STATUS_TOP_MARGIN, inkplate.width(), 
                     max_status_lines * STATUS_LINE_HEIGHT, WHITE);
    status_output_inkplate_line = 0;
  }
  
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  // Send to debug output
  DEBUG_PRINTF("%s\n", buffer);
  
  // Single column layout
  int x_pos = 10;
  int y_pos = STATUS_TOP_MARGIN + status_output_inkplate_line * STATUS_LINE_HEIGHT;
  
  inkplate.setTextSize(TEXT_SIZE);
  inkplate.setTextColor(BLACK);
  inkplate.setCursor(x_pos, y_pos);
  inkplate.println(buffer);
  inkplate.partialUpdate();
  
  status_output_inkplate_line++;
}

void printStatusProgress(const char* message, int current, int total) {
  static int progress_line = -1;
  static const char* last_message = "";
  
  if (strcmp(message, last_message) != 0) {
    // If we run out of lines, clear status area and start over
    if (status_output_inkplate_line >= max_status_lines) {
      inkplate.fillRect(0, STATUS_TOP_MARGIN, inkplate.width(), 
                       max_status_lines * STATUS_LINE_HEIGHT, WHITE);
      status_output_inkplate_line = 0;
      progress_line = -1; // Reset progress line
    }
    progress_line = status_output_inkplate_line;
    status_output_inkplate_line++; // Reserve this line
    last_message = message;

    // Send to debug output
    DEBUG_PRINTF("%s\n", message);
  }
  
  int x_pos = 10;
  int y_pos = STATUS_TOP_MARGIN + progress_line * STATUS_LINE_HEIGHT;
  
  inkplate.setCursor(x_pos, y_pos);
  inkplate.print(message);
  inkplate.print(" [");
  
  int progress = (current * PROGRESS_BAR_WIDTH) / total;
  for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
    inkplate.print(i < progress ? "#" : " ");
  }
  inkplate.print("]   ");
  inkplate.partialUpdate();
}

void inkplateBatteryVoltage() {
  float batteryVoltage = inkplate.readBattery();
  char voltageStr[32];
  
  snprintf(voltageStr, sizeof(voltageStr), "Batt: %.2fV", batteryVoltage);
  
  // Use timestamp text size for consistency with other corner text
  inkplate.setTextSize(TEXT_SIZE);
  int text_width = strlen(voltageStr) * CHAR_WIDTH_ESTIMATE;
  inkplate.setCursor(inkplate.width() - text_width - 10, 8);
  inkplate.println(voltageStr);
  
  // Show low battery warning if needed
  if (batteryVoltage < LOW_BATTERY_THRESHOLD && batteryVoltage > MIN_VALID_BATTERY_READING) {
    inkplate.setTextSize(TEXT_SIZE);
    inkplate.setCursor(inkplate.width() - 80, 35);
    inkplate.println("LOW BAT!");
  }
}

void goToDeepSleep() {
  DEBUG_PRINTLN("Going to deep sleep...");
  
  // Calculate time until next update
  struct tm timeinfo;
  uint64_t sleep_microseconds = 24 * 60 * 60 * 1000000ULL; // Default to 24 hours
  
  if (getLocalTime(&timeinfo)) {
    int current_day = timeinfo.tm_wday; // 0=Sunday, 1=Monday, ..., 6=Saturday
    int current_hour = timeinfo.tm_hour;
    int current_minute = timeinfo.tm_min;
    int current_time_in_minutes = current_hour * 60 + current_minute;
    int target_minutes = UPDATE_HOUR * 60 + UPDATE_MINUTE;
    
    int days_to_add;
    
    if (current_day == 5) { // Friday - sleep until Monday evening
      days_to_add = 3;
    } else if (current_day == 6) { // Saturday - sleep until Monday evening
      days_to_add = 2;
    } else if (current_day == 0) { // Sunday - sleep until Monday evening
      days_to_add = 1;
    } else { // Monday-Thursday - sleep until next evening
      days_to_add = 1;
    }
    
    // Calculate sleep time: days_to_add full days, minus current time, plus target time
    int sleep_minutes = days_to_add * 24 * 60 - current_time_in_minutes + target_minutes;
    sleep_microseconds = sleep_minutes * 60 * 1000000ULL;
    
    DEBUG_PRINTF("Current: Day %d, %02d:%02d\n", current_day, current_hour, current_minute);
    DEBUG_PRINTF("Sleeping for %.1f hours until next update at %02d:%02d\n", 
                  sleep_minutes / 60.0, UPDATE_HOUR, UPDATE_MINUTE);
  } else {
    DEBUG_PRINTLN("Could not get time, using 24-hour fallback");
  }
  
  esp_sleep_enable_timer_wakeup(sleep_microseconds);
  esp_deep_sleep_start();
}

void recordWakeupTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    time_t current_time = mktime(&timeinfo);
    wakeup_history[wakeup_index] = current_time;
    wakeup_index = (wakeup_index + 1) % WAKEUP_HISTORY_SIZE; // Ring buffer
    if (wakeup_count < WAKEUP_HISTORY_SIZE) wakeup_count++;
    DEBUG_PRINTF("Recorded wakeup time: %s", ctime(&current_time));
  }
}

void displayWakeupHistory() {
  if (wakeup_count == 0) return;
  
  char line_buffer[100] = "Last wakeups: ";
  
  for (int i = 0; i < wakeup_count; i++) {
    int idx = (wakeup_index - wakeup_count + i + WAKEUP_HISTORY_SIZE) % WAKEUP_HISTORY_SIZE;
    time_t wake_time = wakeup_history[idx];
    
    struct tm* timeinfo = localtime(&wake_time);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%m/%dT%H:%M", timeinfo);
    
    if (i > 0) {
      strncat(line_buffer, "  ", sizeof(line_buffer) - strlen(line_buffer) - 1);
    }
    strncat(line_buffer, time_str, sizeof(line_buffer) - strlen(line_buffer) - 1);
  }
  
  printStatusLine("%s", line_buffer);
}

void drawThickLine(int x0, int y0, int x1, int y1, int thickness) {
  /* Draw a thick line by creating a filled rectangle along the line path */
  if (thickness <= 1) {
    inkplate.drawLine(x0, y0, x1, y1, BLACK);
    return;
  }
  
  // Calculate line direction and perpendicular vectors
  int dx = x1 - x0;
  int dy = y1 - y0;
  float length = sqrt(dx * dx + dy * dy);
  
  if (length == 0) return; // Avoid division by zero
  
  // Normalized direction vector
  float dir_x = dx / length;
  float dir_y = dy / length;
  
  // Normalized perpendicular vector
  float perp_x = -dy / length;
  float perp_y = dx / length;
  
  float half_thickness = thickness / 2.0;
  
  // Calculate the four corners of the thick line rectangle
  float corner1_x = x0 + perp_x * half_thickness;
  float corner1_y = y0 + perp_y * half_thickness;
  float corner2_x = x0 - perp_x * half_thickness;
  float corner2_y = y0 - perp_y * half_thickness;
  float corner3_x = x1 - perp_x * half_thickness;
  float corner3_y = y1 - perp_y * half_thickness;
  float corner4_x = x1 + perp_x * half_thickness;
  float corner4_y = y1 + perp_y * half_thickness;
  
  // Draw filled rectangle by scanning horizontally
  int min_x = (int)min(min(corner1_x, corner2_x), min(corner3_x, corner4_x));
  int max_x = (int)max(max(corner1_x, corner2_x), max(corner3_x, corner4_x));
  int min_y = (int)min(min(corner1_y, corner2_y), min(corner3_y, corner4_y));
  int max_y = (int)max(max(corner1_y, corner2_y), max(corner3_y, corner4_y));
  
  // Fill the rectangle area
  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      // Check if point is inside the thick line rectangle using cross product
      if (isPointInThickLine(x, y, x0, y0, x1, y1, thickness)) {
        inkplate.drawPixel(x, y, BLACK);
      }
    }
  }
}

bool isPointInThickLine(int px, int py, int x0, int y0, int x1, int y1, int thickness) {
  /* Check if point (px, py) is within the thick line defined by (x0, y0) to (x1, y1) */

  // Vector from line start to point
  float dx_point = px - x0;
  float dy_point = py - y0;
  
  // Line direction vector
  float dx_line = x1 - x0;
  float dy_line = y1 - y0;
  float line_length_sq = dx_line * dx_line + dy_line * dy_line;
  
  if (line_length_sq == 0) return false;
  
  // Project point onto line (parametric t)
  float t = (dx_point * dx_line + dy_point * dy_line) / line_length_sq;
  
  // Clamp t to line segment
  t = max(0.0f, min(1.0f, t));
  
  // Find closest point on line segment
  float closest_x = x0 + t * dx_line;
  float closest_y = y0 + t * dy_line;
  
  // Distance from point to closest point on line
  float dist_sq = (px - closest_x) * (px - closest_x) + (py - closest_y) * (py - closest_y);
  
  // Check if within thickness
  float half_thickness = thickness / 2.0;
  return dist_sq <= (half_thickness * half_thickness);
}
