/*
 * Visage Display System - Arduino IoT Display Controller
 * 
 * This system provides a touch-enabled display interface for the Visage attendance system.
 * 
 * 
 * 
 * API Endpoints:
 * - POST /api/v1/display/notification - Accept JSON notification data 
 * - POST /api/v1/display/test-notification - Send test notification
 * - POST /api/v1/attendance/entry - Trigger entry attendance
 * - POST /api/v1/attendance/exit - Trigger exit attendance
 * - GET /health - Health check and system status
 * - GET / - Web interface for manual testing
 * - GET /config - Configuration management
 * 
 * 
 */

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include <HTTPClient.h>
// Using Arduino_GFX smooth font rendering for modern appearance

// Color definitions
#ifndef LIGHTBLUE
#define LIGHTBLUE 0xAEDCFF 
#endif

#ifndef VISAGE_BLUE
#define VISAGE_BLUE 0x0011 
#endif

#define ENTRY_GREEN 0x07E0    
#define EXIT_RED 0xF800         
#define LIGHT_GREEN 0x87E0     
#define LIGHT_RED 0xFBEA       

// Basic color definitions 
#ifndef BLACK
#define BLACK 0x0000          
#endif
#ifndef WHITE
#define WHITE 0xFFFF           
#endif
#ifndef RED
#define RED 0xF800            
#endif
#ifndef GREEN
#define GREEN 0x07E0           
#endif
#ifndef BLUE
#define BLUE 0x001F            
#endif
#ifndef CYAN
#define CYAN 0x07FF           
#endif
#ifndef MAGENTA
#define MAGENTA 0xF81F         
#endif
#ifndef YELLOW
#define YELLOW 0xFFE0        
#endif

// Pin definition for the JC4827W543C board's display bus [1]
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* cs */, 
    47 /* sck */, 
    21 /* d0 */, 
    48 /* d1 */, 
    40 /* d2 */, 
    39 /* d3 */
);

// Display driver initialization for the NV3041A [1]
Arduino_GFX *gfx = new Arduino_NV3041A(
    bus, 
    GFX_NOT_DEFINED /* RST */, 
    0 /* rotation */, 
    true /* IPS */
);

// WiFiManager instance
WiFiManager wm;

/*******************************************************************************
 * TOUCH PANEL SUPPORT (Capacitive Touch)
 ******************************************************************************/ 
#include "touch.h"

// Touch state tracking
int lastTouchX = -1, lastTouchY = -1;
unsigned long lastTouchTime = 0;

// Display state management
bool showingWelcome = true;

// Attendance type tracking
enum AttendanceType { ATTENDANCE_IN = 0, ATTENDANCE_OUT = 1 };
AttendanceType currentAttendanceType = ATTENDANCE_IN;

// Time (ms) to wait before returning to default page after user detected
const unsigned long RETURN_TO_DEFAULT_TIMEOUT = 8000;  
unsigned long lastUserDetectedTime = 0;
bool waitingForDefaultReturn = false;


// Button layout for message page 
const int updateButtonX = 120;
const int updateButtonY = 190;
const int updateButtonW = 240;
const int updateButtonH = 70;

// Edge API integration
String edgeApiBaseUrl = "http://192.168.1.100:8000"; 
bool isProcessingRecognition = false;
String lastRecognitionResult = "";
unsigned long recognitionStartTime = 0;
const unsigned long RECOGNITION_TIMEOUT = 10000; 

WebServer server(80);

// Current message and timestamp to display
String currentMessage = "Waiting for notifications...";
String messageTimestamp = "";
String messageFrom = "";
unsigned long messageTime = 0;
bool hasNewMessage = false;
bool hasReceivedName = false;

// Central server update functionality
String currentRecordId = "";
String currentEmployeeName = "";
bool showUpdateButton = false;

// Central server configuration
String centralServerEndpoint = "https://visage.sltdigitallab.lk/api/update_last_activity";
String centralServerApiKey = "26PytkCBcZ";
String centralServerUsername = "slt_interns";
bool centralServerEnabled = true;

// Helper to get greeting based on attendance type
String getGreeting() {
  return (currentAttendanceType == ATTENDANCE_IN) ? "Welcome!" : "Goodbye!";
}

// Helper to get current attendance type based on time
AttendanceType getAttendanceTypeFromTime() {
  // Always default to IN
  return ATTENDANCE_IN;
}

/*******************************************************************************
 * EDGE API INTEGRATION
 ******************************************************************************/


// Call visage-edge trigger recognition API
bool callTriggerRecognition(String attendanceType) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot call API");
    return false;
  }
  
  HTTPClient http;
  String url = edgeApiBaseUrl + "/api/v1/attendance/trigger";
  
  http.begin(url);
  http.setTimeout(10000); 
  http.addHeader("Content-Type", "application/json");
  http.setReuse(false); // Don't reuse connections to avoid issues;
  
  Serial.println("Calling trigger recognition API: " + url);
  
  // Create JSON payload
  DynamicJsonDocument payload(1024);
  payload["attendance_type"] = attendanceType;
  String jsonString;
  serializeJson(payload, jsonString);
  
  Serial.println("Payload: " + jsonString);
  
  // Send POST request with JSON body
  int httpResponseCode = http.POST(jsonString);
  
  Serial.println("HTTP Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("API Response: " + response);
    
    // Parse response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      // Handle the response format from visage-edge API
      String status = doc["status"].as<String>();
      String responseMessage = doc["message"].as<String>();
      
      if (status == "success" || status == "ok") {
        // Success case - extract user data 
        String username = "";
        String message = responseMessage;
        String timestamp = "";
        
        // Check if data object exists 
        if (doc.containsKey("data") && !doc["data"].isNull()) {
          JsonObject data = doc["data"];
          username = data["username"].as<String>();
          if (data.containsKey("message")) {
            message = data["message"].as<String>();
          }
          if (data.containsKey("timestamp")) {
            timestamp = data["timestamp"].as<String>();
          }
          // Extract record_id and employee name for central server update functionality
          if (data.containsKey("record_id")) {
            currentRecordId = data["record_id"].as<String>();
          }
          if (data.containsKey("username")) {
            currentEmployeeName = data["username"].as<String>();
          } else if (username.length() > 0 && username != "Unknown User") {
            currentEmployeeName = username;
          }
          showUpdateButton = true;
        }
        
        // Fallback to root level fields if data object doesn't exist
        if (username.length() == 0) {
          username = doc["username"].as<String>();
        }
        
        // Check for null/empty values and provide fallbacks
        if (username == "null" || username.length() == 0) {
          username = "Unknown User";
        }
        
        if (message.length() > 0 && message != "null") {
          // Use the message from edge system
          lastRecognitionResult = "SUCCESS: " + message;
        } else {
          // Fallback to username if no message
          lastRecognitionResult = "SUCCESS: " + username;
        }
        
        // Store timestamp for display
        if (timestamp.length() > 0 && timestamp != "null") {
          messageTimestamp = timestamp;
          Serial.println("Stored timestamp: '" + messageTimestamp + "'");
          Serial.println("Time only: '" + getTimeOnly(messageTimestamp) + "'");
        } else {
          messageTimestamp = "";
          Serial.println("No valid timestamp received");
        }
        
        // Update current message with username for display in new format
        currentMessage = "Name: " + username;
        hasNewMessage = true;
        hasReceivedName = true;
        showingWelcome = false;
        
        // Store employee name and record ID for central server updates
        currentEmployeeName = username;
        if (doc.containsKey("data") && !doc["data"].isNull() && doc["data"].containsKey("record_id")) {
          currentRecordId = doc["data"]["record_id"].as<String>();
        } else if (doc.containsKey("record_id")) {
          currentRecordId = doc["record_id"].as<String>();
        }
        
        // Automatically update central server on successful recognition 
        Serial.println("Auto-updating central server for user: " + username);
        bool centralServerResult = callUpdateCentralServerDirect(username);
        if (centralServerResult) {
          Serial.println("Central server updated successfully");
        } else {
          Serial.println("Central server update failed");
        }
        
        // Show update button for manual re-updates if needed
        showUpdateButton = true;
        
        // Clear recognition result to avoid overlay conflict
        lastRecognitionResult = "";
        
        Serial.println("Recognition successful - showing message page for: " + username);
      } else {
        // API returned 200 but with error status 
        if (responseMessage.length() > 0) {
          lastRecognitionResult = "ERROR: " + responseMessage;
        } else {
          lastRecognitionResult = "ERROR: System error";
        }
        Serial.println("API returned error: " + responseMessage);
      }
    } else {
      Serial.println("JSON parsing failed: " + String(error.c_str()));
      lastRecognitionResult = "ERROR: Invalid response format";
    }
  } else if (httpResponseCode > 0) {
    // Other HTTP response codes (400, 404, 500, etc.) 
    String response = http.getString();
    Serial.println("API Response: " + response);
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      // Try to extract error message 
      String errorMsg = "";
      
      if (doc.containsKey("detail")) {
        errorMsg = doc["detail"].as<String>();
      } else if (doc.containsKey("message")) {
        errorMsg = doc["message"].as<String>();
      }
      
      if (errorMsg.length() == 0) {
        errorMsg = "HTTP " + String(httpResponseCode);
      }
      
      if (httpResponseCode == 400) {
        // Bad request - treat as info 
        lastRecognitionResult = "INFO: " + errorMsg;
        Serial.println("Bad request: " + errorMsg);
      } else if (httpResponseCode == 502) {
        // Bad gateway - service communication error
        lastRecognitionResult = "ERROR: Service unavailable - " + errorMsg;
        Serial.println("Service error (" + String(httpResponseCode) + "): " + errorMsg);
      } else if (httpResponseCode == 503) {
        // Service unavailable
        lastRecognitionResult = "ERROR: Service unavailable";
        Serial.println("Service unavailable (" + String(httpResponseCode) + ")");
      } else {
        // Other HTTP errors
        lastRecognitionResult = "ERROR: " + errorMsg;
        Serial.println("API Error (" + String(httpResponseCode) + "): " + errorMsg);
      }
    } else {
      lastRecognitionResult = "ERROR: HTTP " + String(httpResponseCode) + " - Invalid response";
      Serial.println("HTTP " + String(httpResponseCode) + " with invalid JSON response");
    }
  } else {
    // Network errors (negative response codes like -1, -11, etc.) 
    String errorMsg = "Connection failed";
    if (httpResponseCode == -1) {
      errorMsg = "Connection refused";
    } else if (httpResponseCode == -11) {
      errorMsg = "Timeout";
    } else if (httpResponseCode == -4) {
      errorMsg = "Connection lost";
    }
    
    lastRecognitionResult = "ERROR: " + errorMsg + " (" + String(httpResponseCode) + ")";
    Serial.println("Network Error: " + errorMsg + " (Code: " + String(httpResponseCode) + ")");
  }
  
  http.end();
  return httpResponseCode == 200;
}

// Draw update central server button
void drawUpdateButton(bool pressed = false, String targetType = "") {
  uint16_t topColor, bottomColor;
  
  // Set button colors based on current attendance type for visual consistency
  if (currentAttendanceType == ATTENDANCE_IN) {
    // Entry (IN) 
    topColor = pressed ? 0x0011 : 0x021F;     
    bottomColor = pressed ? 0x07E0 : 0x87F0;  
  } else {
    // Exit (OUT) 
    topColor = pressed ? 0x8000 : 0xA800;    
    bottomColor = pressed ? EXIT_RED : 0xFBEA;  
  }

  for (int y = 0; y < updateButtonH; y++) {
    float factor = (float)y / (float)(updateButtonH - 1);
    // Interpolate RGB565 manually
    uint8_t r1 = (topColor >> 11) & 0x1F;
    uint8_t g1 = (topColor >> 5) & 0x3F;
    uint8_t b1 = topColor & 0x1F;
    uint8_t r2 = (bottomColor >> 11) & 0x1F;
    uint8_t g2 = (bottomColor >> 5) & 0x3F;
    uint8_t b2 = bottomColor & 0x1F;
    uint8_t r = r1 + (r2 - r1) * factor;
    uint8_t g = g1 + (g2 - g1) * factor;
    uint8_t b = b1 + (b2 - b1) * factor;
    uint16_t color = (r << 11) | (g << 5) | b;
    if (y < 8 || y >= updateButtonH - 8) {
      int offset = (y < 8) ? (8 - y) : (y - (updateButtonH - 8));
      int startX = updateButtonX + offset;
      int endX = updateButtonX + updateButtonW - offset;
      gfx->drawFastHLine(startX, updateButtonY + y, endX - startX, color);
    } else {
      gfx->drawFastHLine(updateButtonX, updateButtonY + y, updateButtonW, color);
    }
  }
  // Draw border 
  gfx->drawRoundRect(updateButtonX, updateButtonY, updateButtonW, updateButtonH, 8, BLACK);
  gfx->drawRoundRect(updateButtonX + 1, updateButtonY + 1, updateButtonW - 2, updateButtonH - 2, 7, BLACK);
  
  // Draw text 
  gfx->setTextSize(2);
  
  // Create dynamic button text based on target attendance type
  String text;
  if (targetType.length() > 0) {
    text = "Change to " + targetType;
  } else {
    // Fallback to generic text if no target type specified
    String toggleTypeText = (currentAttendanceType == ATTENDANCE_IN) ? "Exit" : "Entry";
    text = "Change to " + toggleTypeText;
  }
  
  int charWidth = 6 * 2; // 6 pixels per char * text size 2
  int textWidth = text.length() * charWidth;
  int textX = updateButtonX + (updateButtonW - textWidth) / 2;
  int textY = updateButtonY + (updateButtonH / 2) - 8; 
  
  gfx->setTextColor(BLACK); // Dark shadow
  gfx->setCursor(textX + 2, textY + 2);
  gfx->print(text);
  
  // Draw main text in bright white
  gfx->setTextColor(WHITE);
  gfx->setCursor(textX, textY);
  gfx->print(text);
  
  gfx->setTextSize(1); 
}

// Draw processing state overlay
void drawProcessingState() {
  // Semi-transparent overlay
  gfx->fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x7BEF); 
  
  // Processing box
  int boxW = 300;
  int boxH = 100;
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;
  
  gfx->fillRoundRect(boxX, boxY, boxW, boxH, 15, WHITE);
  gfx->drawRoundRect(boxX, boxY, boxW, boxH, 15, BLACK);
  
  // Processing text
  gfx->setTextColor(BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(boxX + 20, boxY + 20);
  gfx->print("Processing...");
  gfx->setCursor(boxX + 20, boxY + 45);
  gfx->print("Please wait for face");
  gfx->setCursor(boxX + 20, boxY + 70);
  gfx->print("recognition...");
}

// Draw result state
void drawResultState(String resultType) {
  if (resultType == "SUCCESS") {
    gfx->fillScreen(LIGHTBLUE);
    
    String greeting = getGreeting();
    if (currentAttendanceType == ATTENDANCE_IN) {
        gfx->setTextColor(0x0011); 
    } else {
        gfx->setTextColor(EXIT_RED);
    }
    gfx->setTextSize(3);
    int charWidth = 6 * 3;
    int textWidth = greeting.length() * charWidth;
    int greetingX = (SCREEN_WIDTH - textWidth) / 2;
    gfx->setCursor(greetingX, 20);
    gfx->print(greeting);
    
    // Extract just the message part 
    String message = lastRecognitionResult;
    if (message.startsWith("SUCCESS: ")) {
      message = message.substring(9); 
    }
    
    // Display message centered 
    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    int charWidth2 = 6 * 2;
    int lineHeight = 24;
    int maxWidth = SCREEN_WIDTH - 100;
    int maxCharsPerLine = maxWidth / charWidth2;
    
    // Split message into lines
    std::vector<String> lines;
    int start = 0;
    while (start < message.length()) {
        int len = min(maxCharsPerLine, (int)(message.length() - start));
        int end = start + len;
        if (end < message.length() && message[end] != ' ') {
            int lastSpace = message.lastIndexOf(' ', end);
            if (lastSpace > start) {
                end = lastSpace;
            }
        }
        String lineStr = message.substring(start, end);
        lines.push_back(lineStr);
        start = end;
        while (start < message.length() && message[start] == ' ') start++;
    }
    
    // Calculate total height of message block and center it
    int totalMsgHeight = lines.size() * lineHeight;
    int msgBlockY = (SCREEN_HEIGHT - totalMsgHeight) / 2 - 20; /
    
    // Draw each line centered horizontally
    for (size_t i = 0; i < lines.size(); ++i) {
        int linePixelWidth = lines[i].length() * charWidth2;
        int lineX = (SCREEN_WIDTH - linePixelWidth) / 2;
        int lineY = msgBlockY + i * lineHeight;
        gfx->setCursor(lineX, lineY);
        gfx->print(lines[i]);
    }
    
    // Draw timestamp below message if available
    if (messageTimestamp.length() > 0) {
        String timeOnly = getTimeOnly(messageTimestamp);
        gfx->setTextColor(BLACK);
        gfx->setTextSize(2);
        int tsWidth = 6 * 2 * (6 + timeOnly.length()); // "Time: " + time
        int tsX = (SCREEN_WIDTH - tsWidth) / 2;
        int tsY = msgBlockY + totalMsgHeight + 10;
        gfx->setCursor(tsX, tsY);
        gfx->print("Time: ");
        gfx->print(timeOnly);
    }
    
    return;
  }
  
  // For non-success results, use the existing overlay style
  // Semi-transparent overlay
  gfx->fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x7BEF);
  
  // Result box
  int boxW = 400;
  int boxH = 120;
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;
  
  uint16_t bgColor, borderColor;
  String statusText;
  
  if (resultType == "INFO") {
    bgColor = 0xDEFB;       
    borderColor = 0xFFE0;   
    statusText = "NO MATCH";
  } else {
    bgColor = 0xFBEA;       
    borderColor = 0xF800;   
    statusText = "FAILED!";
  }
  
  gfx->fillRoundRect(boxX, boxY, boxW, boxH, 15, bgColor);
  gfx->drawRoundRect(boxX, boxY, boxW, boxH, 15, borderColor);
  
  // Result text
  gfx->setTextColor(BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(boxX + 20, boxY + 20);
  gfx->print(statusText);
  
  // Show result details 
  gfx->setTextSize(1);
  gfx->setCursor(boxX + 20, boxY + 50);
  
  String result = lastRecognitionResult;
  if (result.length() > 50) {
    int spaceIndex = result.indexOf(' ', 25);
    if (spaceIndex > 0) {
      gfx->print(result.substring(0, spaceIndex));
      gfx->setCursor(boxX + 20, boxY + 65);
      gfx->print(result.substring(spaceIndex + 1));
    } else {
      gfx->print(result.substring(0, 50));
      gfx->setCursor(boxX + 20, boxY + 65);
      gfx->print(result.substring(50));
    }
  } else {
    gfx->print(result);
  }
  
}

/*******************************************************************************
 * PAGE RENDERING
 ******************************************************************************/

// Helper to extract only the time part from a timestamp string
String getTimeOnly(const String& timestamp) {
    // Expected format: "2024-08-15 12:34:56" -> extract "12:34:56"
    int spaceIdx = timestamp.indexOf(' ');
    if (spaceIdx != -1 && spaceIdx + 1 < timestamp.length()) {
        String timeOnly = timestamp.substring(spaceIdx + 1);
        // Additional validation: ensure it looks like a time format
        if (timeOnly.length() >= 8 && timeOnly.indexOf(':') > 0) {
            return timeOnly;
        }
    }
    // Fallback: if format is unexpected, return the original but truncated
    if (timestamp.length() > 8) {
        return timestamp.substring(timestamp.length() - 8); 
    }
    return timestamp;
}

// Render single message page with time-based greeting
void renderMessagePage() {
    gfx->fillScreen(LIGHTBLUE);
    // Get time-based greeting for the header
    String greeting = getGreeting();
    // Draw greeting at top center with appropriate color
    if (currentAttendanceType == ATTENDANCE_IN) {
        gfx->setTextColor(0x0011); 
    } else {
        gfx->setTextColor(EXIT_RED);
    }
    gfx->setTextSize(3);
    int charWidth = 6 * 3;
    int textWidth = greeting.length() * charWidth;
    int greetingX = (SCREEN_WIDTH - textWidth) / 2;
    gfx->setCursor(greetingX, 20);
    gfx->print(greeting);

    // Prepare message text for centering (Name: username)
    gfx->setTextColor(BLACK);
    gfx->setTextSize(2);
    int charWidth2 = 6 * 2;
    int lineHeight = 24;
    
    // Calculate Y position for name (center upper area)
    int nameY = 100; // Fixed position below greeting
    
    // Draw the name
    String nameMsg = currentMessage; // This should be "Name: username"
    int namePixelWidth = nameMsg.length() * charWidth2;
    int nameX = (SCREEN_WIDTH - namePixelWidth) / 2;
    gfx->setCursor(nameX, nameY);
    gfx->print(nameMsg);
    
    // Draw timestamp below name
    if (messageTimestamp.length() > 0) {
        String timeOnly = getTimeOnly(messageTimestamp);
        // Ensure timeOnly is valid and not too long
        if (timeOnly.length() > 20) {
            timeOnly = timeOnly.substring(0, 20); // Truncate if too long
        }
        String timeMsg = "Time: " + timeOnly;
        gfx->setTextColor(BLACK);
        gfx->setTextSize(2);
        // Fix width calculation: each character is 6 pixels wide at size 2, so 12 pixels per char
        int tsWidth = timeMsg.length() * 12;
        int tsX = (SCREEN_WIDTH - tsWidth) / 2;
        // Ensure timestamp doesn't go off screen
        if (tsX < 0) tsX = 10;
        if (tsX + tsWidth > SCREEN_WIDTH) tsX = SCREEN_WIDTH - tsWidth - 10;
        int tsY = nameY + lineHeight + 10; // Position below name with spacing
        gfx->setCursor(tsX, tsY);
        gfx->print(timeMsg);
    }
    
    // Show change attendance type button if enabled and we have employee name AND record ID
    if (showUpdateButton && currentEmployeeName.length() > 0 && currentRecordId.length() > 0) {
      // Determine the target attendance type (opposite of current)
      String toggleTypeText = (currentAttendanceType == ATTENDANCE_IN) ? "Exit" : "Entry";
        
      // Draw change attendance type button with dynamic text
      drawUpdateButton(false, toggleTypeText);
    }
}

// Render default welcome page with attendance type selection
void renderWelcomePage() {
    gfx->fillScreen(LIGHTBLUE);
    
    // Title - VISAGE in bold dark blue
    gfx->setTextColor(VISAGE_BLUE);
    gfx->setTextSize(4); // Larger size for bold appearance
    const char* visageMsg = "VISAGE";
    int charWidth = 6 * 4;
    int textWidth = strlen(visageMsg) * charWidth;
    int visageX = (SCREEN_WIDTH - textWidth) / 2;
    gfx->setCursor(visageX, 30);
    gfx->print(visageMsg);
    
  // Instruction text - prompt user to look at the camera (centered both horizontally and vertically)
  gfx->setTextColor(BLACK);
  gfx->setTextSize(2);
  const char* instructionMsg = "Please look at the camera";
  int charWidth2 = 6 * 2;
  int textWidth2 = strlen(instructionMsg) * charWidth2;
  int instructionX = (SCREEN_WIDTH - textWidth2) / 2;
  
  // Center vertically between the title and bottom of screen
  // Title is at Y=30 with size 4 (height ~32px), so title ends around Y=62
  // Available space from Y=70 to Y=272 = 202px
  // Center the instruction text in this space: Y=70 + (202-16)/2 = Y=163
  int instructionY = 70 + ((SCREEN_HEIGHT - 70 - 16) / 2); // 16 is approximate text height for size 2
  gfx->setCursor(instructionX, instructionY);
  gfx->print(instructionMsg);
    
    // Show processing or result overlay if needed
    if (isProcessingRecognition) {
      drawProcessingState();
    } else if (lastRecognitionResult.length() > 0) {
      String resultType = "ERROR"; // Default
      if (lastRecognitionResult.startsWith("SUCCESS")) {
        resultType = "SUCCESS";
      } else if (lastRecognitionResult.startsWith("INFO")) {
        resultType = "INFO";
      }
      drawResultState(resultType);
    }
}

/*******************************************************************************
 * TOUCH HANDLING
 ******************************************************************************/

void handleWelcomePageTouch(int x, int y) {
  // Clear any previous result when user touches screen after result display
  if (lastRecognitionResult.length() > 0 && !isProcessingRecognition) {
    lastRecognitionResult = "";
    messageTimestamp = ""; // Also clear timestamp
    currentMessage = "Waiting for notifications..."; // Reset message
    hasNewMessage = false;
    hasReceivedName = false;
    showUpdateButton = false; // Reset update button
    currentRecordId = ""; // Reset record ID
    currentEmployeeName = ""; // Reset employee name
    return; // Just clear result and re-render
  }
  
  // If showing message page after recognition, clear it and return to welcome
  if (!showingWelcome && hasReceivedName) {
    messageTimestamp = "";
    currentMessage = "Waiting for notifications...";
    hasNewMessage = false;
    hasReceivedName = false;
    showingWelcome = true;
    showUpdateButton = false; // Reset update button
    currentRecordId = ""; // Reset record ID
    currentEmployeeName = ""; // Reset employee name
    return;
  }
  
  // Ignore touches while processing
  if (isProcessingRecognition) {
    return;
  }
}

/*******************************************************************************
 * TOUCH HANDLING FOR MESSAGE PAGE
 ******************************************************************************/

void handleMessagePageTouch(int x, int y) {
  // Handle change attendance type button if it's shown
  if (showUpdateButton && currentEmployeeName.length() > 0 && currentRecordId.length() > 0) {
    // Handle CHANGE ATTENDANCE TYPE button with touch tolerance
    const int touchTolerance = 10; // pixels of tolerance for touch sensitivity
    if (x >= (updateButtonX - touchTolerance) && x <= (updateButtonX + updateButtonW + touchTolerance) && 
        y >= (updateButtonY - touchTolerance) && y <= (updateButtonY + updateButtonH + touchTolerance)) {
      
      // Determine new attendance type (toggle current type)
      String newAttendanceType = (currentAttendanceType == ATTENDANCE_IN) ? "exit" : "entry";
      String targetTypeText = (currentAttendanceType == ATTENDANCE_IN) ? "Exit" : "Entry";
      
      Serial.println("Change Attendance Type button pressed for employee: " + currentEmployeeName);
      Serial.println("Current type: " + String((currentAttendanceType == ATTENDANCE_IN) ? "entry" : "exit"));
      Serial.println("Changing to: " + newAttendanceType);
      Serial.println("Record ID: " + currentRecordId);
      
      // Prevent multiple rapid button presses
      static unsigned long lastButtonPress = 0;
      if (millis() - lastButtonPress < 3000) { // Ignore button presses within 3 seconds
        return;
      }
      lastButtonPress = millis();
      
      // Show immediate visual feedback
      drawUpdateButton(true, targetTypeText); // Draw pressed state with target type
      delay(200); // Increased visual feedback duration
      
      // Call API to change attendance type in edge system
      bool changeSuccess = callChangeAttendanceType(currentRecordId, newAttendanceType);
      
      if (changeSuccess) {
        // Update local attendance type
        currentAttendanceType = (newAttendanceType == "entry") ? ATTENDANCE_IN : ATTENDANCE_OUT;
        
        // Automatically update central server after successful change (like main.py)
        Serial.println("Auto-updating central server after attendance type change for user: " + currentEmployeeName);
        bool centralServerResult = callUpdateCentralServerDirect(currentEmployeeName);
        
        // Clear any previous results
        lastRecognitionResult = ""; // Clear to prevent overlay conflicts
        
        // Force immediate re-render of message page with new greeting
        renderMessagePage();
        
        // Show brief success feedback
        gfx->setTextColor(0x07E0); // Green color for success
        gfx->setTextSize(1);
        String successMsg = "Attendance type changed to " + newAttendanceType;
        if (centralServerResult) {
          successMsg += " & central server updated";
        } else {
          successMsg += " (central server update failed)";
        }
        
        // Display success message (truncate if too long)
        if (successMsg.length() > 60) {
          successMsg = "Type changed to " + newAttendanceType;
        }
        
        int successWidth = successMsg.length() * 6;
        int successX = (SCREEN_WIDTH - successWidth) / 2;
        if (successX < 5) successX = 5;
        int successY = 270; // Below button (button now ends at Y=260)
        gfx->setCursor(successX, successY);
        gfx->print(successMsg);
        
        delay(2000); // Show success for 2 seconds
        renderMessagePage(); // Re-render clean message page with new greeting
      } else if (lastRecognitionResult.length() == 0) {
        lastRecognitionResult = "ERROR: Failed to change attendance type";
      }
      
      return;
    }
  }
  
  // Any other touch on message page returns to welcome
  messageTimestamp = "";
  currentMessage = "Waiting for notifications...";
  hasNewMessage = false;
  hasReceivedName = false;
  showingWelcome = true;
  showUpdateButton = false;
  currentRecordId = "";
  currentEmployeeName = "";
}

/*******************************************************************************
 * SETUP
 ******************************************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println("Starting Display Test...");




  // Initialize the display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  // Turn on the backlight 
  // On this board, backlight is often handled automatically or tied to power.
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);

  // Initialize touch device (capacitive touch)
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());

  Serial.println("Display initialized. Setting up WiFi...");
  
  // Display initial message
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 100);
  gfx->print("Initializing WiFi...");

  // WiFiManager settings
  wm.setAPCallback(configModeCallback);
  
  // Set timeout for configuration portal (3 minutes)
  wm.setConfigPortalTimeout(180);
  
  // Display connecting message
  gfx->fillScreen(BLACK);
  gfx->setTextColor(CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 100);
  gfx->print("Connecting to WiFi...");
  
  // Automatically connect using saved credentials or start configuration portal
  if (!wm.autoConnect("Visage-Display")) {
    Serial.println("Failed to connect and hit timeout");
    gfx->fillScreen(RED);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(20, 100);
    gfx->print("WiFi Setup Failed");
    gfx->setCursor(20, 130);
    gfx->print("Restarting...");
    delay(3000);
    ESP.restart();
  }

  // If we reach here, WiFi is connected
  Serial.println("WiFi Connected Successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength (RSSI): ");
  Serial.println(WiFi.RSSI());
  
  // Display success message
  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 80);
  gfx->print("WiFi Connected!");
  gfx->setCursor(20, 110);
  gfx->print("SSID: ");
  gfx->print(WiFi.SSID());
  gfx->setCursor(20, 140);
  gfx->print("IP: ");
  gfx->print(WiFi.localIP());
  gfx->setCursor(20, 170);
  gfx->print("Signal: ");
  gfx->print(WiFi.RSSI());
  gfx->print(" dBm");
  
  delay(3000); // Show connection info for 3 seconds
  
  // Start web server
  server.on("/", handleRoot);
  server.on("/health", handleHealth);
  server.on("/notify", handleNotifyForm);
  server.on("/api/notify", handleNotification);
  server.on("/api/v1/display/notification", handleNotification);
  server.on("/api/v1/display/test-notification", handleTestNotification);
  server.on("/api/v1/attendance/entry", handleAttendanceEntry);
  server.on("/api/v1/attendance/exit", handleAttendanceExit);
  server.on("/api/v1/attendance/change_type", handleAttendanceTypeChange);
  server.on("/config", handleConfig);
  server.on("/api/config", handleConfigUpdate);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
  server.begin();
  
  Serial.println("Web server started!");
  Serial.println("Send notifications to: http://" + WiFi.localIP().toString() + "/api/notify");
  Serial.println("Web interface at: http://" + WiFi.localIP().toString());
  Serial.println("Health check at: http://" + WiFi.localIP().toString() + "/health");
  
  
  
  // Display initial message
  renderWelcomePage();
  showingWelcome = true;
  
  Serial.println("Setup complete. Listening for notifications...");
}

/*******************************************************************************
 * LOOP
 ******************************************************************************/
void loop() {
  static unsigned long lastCheck = 0;
  unsigned long currentTime = millis();

  server.handleClient();

  // Touch handling with improved debouncing
  static bool tapHandled = false;
  static unsigned long lastTouchCheckTime = 0;
  
  // Only check touch every 50ms to reduce noise
  if (millis() - lastTouchCheckTime > 50 && touch_has_signal() && touch_touched()) {
    int x = touch_last_x;
    int y = touch_last_y;
    unsigned long now = millis();

    if (!tapHandled) {
      // Handle touches on welcome page to clear results
      if (showingWelcome) {
        handleWelcomePageTouch(x, y);
      } else {
        // Only handle touches when not on the welcome/default page
        if (hasReceivedName) {
          // Handle touch on message page after recognition
          handleMessagePageTouch(x, y);
        }
      }
      tapHandled = true;
    }

    if (lastTouchX == -1 && lastTouchY == -1) {
      lastTouchX = x;
      lastTouchY = y;
      lastTouchTime = now;
    } else if (now - lastTouchTime > 500) { // Reduced from 700ms to 500ms
      lastTouchX = -1;
      lastTouchY = -1;
      tapHandled = false;
    }
    lastTouchCheckTime = now;
  } else if (!touch_has_signal()) {
    // Reset touch state when no signal
    if (millis() - lastTouchCheckTime > 100) { // Only reset after 100ms delay
      lastTouchX = -1;
      lastTouchY = -1;
      tapHandled = false;
    }
  }

  // Handle recognition timeout
  if (isProcessingRecognition && millis() - recognitionStartTime > RECOGNITION_TIMEOUT) {
    isProcessingRecognition = false;
    lastRecognitionResult = "ERROR: Recognition timeout";
    Serial.println("Recognition timeout");
  }

  // Clear result after 5 seconds and re-render welcome page
  static unsigned long resultDisplayTime = 0;
  static bool userInteracting = false;
  
  // Check if user is currently touching the button area
  if (touch_has_signal() && touch_touched() && !showingWelcome && hasReceivedName) {
    int x = touch_last_x;
    int y = touch_last_y;
    if (showUpdateButton && currentEmployeeName.length() > 0 && currentRecordId.length() > 0 &&
        x >= (updateButtonX - 10) && x <= (updateButtonX + updateButtonW + 10) && 
        y >= (updateButtonY - 10) && y <= (updateButtonY + updateButtonH + 10)) {
      userInteracting = true;
    } else {
      userInteracting = false;
    }
  } else {
    userInteracting = false;
  }
  
  if ((lastRecognitionResult.length() > 0 || (!showingWelcome && hasReceivedName)) && !isProcessingRecognition && !userInteracting) {
    if (resultDisplayTime == 0) {
      resultDisplayTime = millis();
    } else if (millis() - resultDisplayTime > 8000) { // Increased from 5000ms to 8000ms
      lastRecognitionResult = "";
      messageTimestamp = ""; // Also clear timestamp
      currentMessage = "Waiting for notifications..."; // Reset message
      hasNewMessage = false;
      hasReceivedName = false;
      showingWelcome = true;
      showUpdateButton = false; // Reset update button
      currentRecordId = ""; // Reset record ID
      currentEmployeeName = ""; // Reset employee name
      resultDisplayTime = 0;
    }
  } else {
    resultDisplayTime = 0;
  }

  // If waiting to return to default page, check timeout
  if (waitingForDefaultReturn && millis() - lastUserDetectedTime > RETURN_TO_DEFAULT_TIMEOUT) {
    // Reset state for next user
    hasReceivedName = false;
    currentMessage = "Waiting for notifications...";
    messageTimestamp = "";
    lastRecognitionResult = ""; // Also clear any recognition results
    waitingForDefaultReturn = false;
    showingWelcome = true;
    showUpdateButton = false; // Reset update button
    currentRecordId = ""; // Reset record ID
    currentEmployeeName = ""; // Reset employee name
    
    
    
    renderWelcomePage();
  }

  // Check WiFi connection status every 30 seconds
  if (currentTime - lastCheck > 30000) {
    lastCheck = currentTime;
    if (WiFi.status() != WL_CONNECTED) {
      gfx->fillScreen(RED);
      gfx->setTextColor(WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(20, 100);
      gfx->print("WiFi Disconnected");
      gfx->setCursor(20, 130);
      gfx->print("Reconnecting...");
      WiFi.reconnect();
      delay(5000);
      return;
    }
  }

  // Render message page if a new message arrives
  if (hasNewMessage && !showingWelcome) {
    renderMessagePage();
    hasNewMessage = false;
  }

  // Re-render welcome page if state changed (with debouncing to prevent excessive rendering)
  static bool lastProcessingState = false;
  static String lastResultState = "";
  static unsigned long lastRenderTime = 0;
  
  if (showingWelcome && 
      (lastProcessingState != isProcessingRecognition || 
       lastResultState != lastRecognitionResult) &&
      millis() - lastRenderTime > 250) { // Add 250ms debounce to prevent excessive rendering
    renderWelcomePage();
    lastProcessingState = isProcessingRecognition;
    lastResultState = lastRecognitionResult;
    lastRenderTime = millis();
  }

  delay(100);
}

// WiFi configuration portal callback
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  
  gfx->fillScreen(BLACK);
  gfx->setTextColor(YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(20, 50);
  gfx->print("WiFi Setup Mode");
  gfx->setCursor(20, 80);
  gfx->print("Connect to:");
  gfx->setCursor(20, 110);
  gfx->print(myWiFiManager->getConfigPortalSSID());
  gfx->setCursor(20, 140);
  gfx->print("IP: ");
  gfx->print(WiFi.softAPIP());
  gfx->setCursor(20, 170);
  gfx->print("Then go to:");
  gfx->setCursor(20, 200);
  gfx->print("192.168.4.1");
}

// Handle test notification request
void handleTestNotification() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  // Create test notification
  currentMessage = "Name: Test User";
  // Create a test timestamp in the same format as the edge system
  messageTimestamp = "2024-08-15 12:34:56";
  messageTime = millis();
  hasNewMessage = true;
  hasReceivedName = true;
  showingWelcome = false;

  // Set attendance type to default (IN)
  currentAttendanceType = ATTENDANCE_IN;

  // Start/reset timer to return to default page
  lastUserDetectedTime = millis();
  waitingForDefaultReturn = true;

  Serial.println("Test notification sent");
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Test notification sent\"}");
}

// Handle attendance entry trigger
void handleAttendanceEntry() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  Serial.println("Entry attendance triggered via API");
  
  // Set attendance type
  currentAttendanceType = ATTENDANCE_IN;
  
  // Start/reset timer to return to default page
  lastUserDetectedTime = millis();
  waitingForDefaultReturn = true;
  
  // Start processing
  isProcessingRecognition = true;
  recognitionStartTime = millis();
  
  // Trigger API call
  bool success = callTriggerRecognition("entry");
  
  isProcessingRecognition = false;
  
  if (!success && lastRecognitionResult.length() == 0) {
    lastRecognitionResult = "ERROR: Failed to connect to edge system";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(502, "application/json", "{\"status\":\"error\",\"message\":\"Failed to trigger recognition on Visage Edge\"}");
    return;
  }
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Entry attendance triggered\"}");
}

// Handle attendance exit trigger
void handleAttendanceExit() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  Serial.println("Exit attendance triggered via API");
  
  // Set attendance type
  currentAttendanceType = ATTENDANCE_OUT;
  
  // Start/reset timer to return to default page
  lastUserDetectedTime = millis();
  waitingForDefaultReturn = true;
  
  // Start processing
  isProcessingRecognition = true;
  recognitionStartTime = millis();
  
  // Trigger API call
  bool success = callTriggerRecognition("exit");
  
  isProcessingRecognition = false;
  
  if (!success && lastRecognitionResult.length() == 0) {
    lastRecognitionResult = "ERROR: Failed to connect to edge system";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(502, "application/json", "{\"status\":\"error\",\"message\":\"Failed to trigger recognition on Visage Edge\"}");
    return;
  }
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Exit attendance triggered\"}");
}

// Call central server directly to update last activity (like main.py does)
bool callUpdateCentralServerDirect(String employeeName) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot call central server");
    return false;
  }
  
  if (!centralServerEnabled) {
    Serial.println("Central server integration disabled");
    return true; // Return true since it's disabled by choice
  }
  
  HTTPClient http;
  http.begin(centralServerEndpoint);
  http.setTimeout(10000); // 10 second timeout
  http.addHeader("Content-Type", "application/json");
  http.addHeader("api", centralServerApiKey);
  http.addHeader("user", centralServerUsername);
  http.addHeader("employee", employeeName);
  http.setReuse(false); // Don't reuse connections to avoid issues
  
  Serial.println("Calling central server directly: " + centralServerEndpoint);
  Serial.println("Employee: " + employeeName);
  
  // Send POST request (empty body, headers contain the data)
  int httpResponseCode = http.POST("");
  
  Serial.println("HTTP Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("Central server response: " + response);
    http.end();
    return true;
  } else if (httpResponseCode > 0) {
    String errorResponse = http.getString();
    Serial.println("Central server error response: " + errorResponse);
  } else {
    Serial.println("Central server connection failed: " + String(httpResponseCode));
  }
  
  http.end();
  return false;
}

// Call visage-edge change attendance type API
bool callChangeAttendanceType(String recordId, String newAttendanceType) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot call API");
    return false;
  }
  
  HTTPClient http;
  String url = edgeApiBaseUrl + "/api/v1/attendance/change_attendance_type";
  
  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  http.addHeader("Content-Type", "application/json");
  http.setReuse(false); // Don't reuse connections to avoid issues
  
  Serial.println("Calling change attendance type API: " + url);
  
  // Create JSON payload
  DynamicJsonDocument payload(1024);
  payload["record_id"] = recordId;
  payload["new_attendance_type"] = newAttendanceType;
  String jsonString;
  serializeJson(payload, jsonString);
  
  Serial.println("Payload: " + jsonString);
  
  // Send POST request with JSON body
  int httpResponseCode = http.POST(jsonString);
  
  Serial.println("HTTP Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("API Response: " + response);
    
    // Parse response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      // Handle the response format from visage-edge API
      String status = doc["status"].as<String>();
      String responseMessage = doc["message"].as<String>();
      
      if (status == "success" || status == "ok") {
        // Extract employee name from the response if available (like main.py does)
        String extractedEmployeeName = "";
        if (doc.containsKey("data") && !doc["data"].isNull()) {
          if (doc["data"].containsKey("name")) {
            extractedEmployeeName = doc["data"]["name"].as<String>();
          }
        } else if (doc.containsKey("name")) {
          extractedEmployeeName = doc["name"].as<String>();
        }
        
        // Update currentEmployeeName if we got it from the response
        if (extractedEmployeeName.length() > 0 && extractedEmployeeName != "null") {
          currentEmployeeName = extractedEmployeeName;
          Serial.println("Extracted employee name from response: " + extractedEmployeeName);
        }
        
        lastRecognitionResult = "SUCCESS: Attendance type changed to " + newAttendanceType;
        Serial.println("Attendance type change successful for record: " + recordId);
      } else {
        // API returned 200 but with error status
        if (responseMessage.length() > 0) {
          lastRecognitionResult = "ERROR: " + responseMessage;
        } else {
          lastRecognitionResult = "ERROR: Failed to change attendance type";
        }
        Serial.println("API returned error: " + responseMessage);
      }
    } else {
      Serial.println("JSON parsing failed: " + String(error.c_str()));
      lastRecognitionResult = "ERROR: Invalid response format";
    }
  } else if (httpResponseCode > 0) {
    // Other HTTP response codes (400, 404, 500, etc.)
    String response = http.getString();
    Serial.println("API Response: " + response);
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      String errorMsg = "";
      
      if (doc.containsKey("detail")) {
        errorMsg = doc["detail"].as<String>();
      } else if (doc.containsKey("message")) {
        errorMsg = doc["message"].as<String>();
      }
      
      if (errorMsg.length() == 0) {
        errorMsg = "HTTP " + String(httpResponseCode);
      }
      
      if (httpResponseCode == 400) {
        lastRecognitionResult = "INFO: " + errorMsg;
        Serial.println("Bad request: " + errorMsg);
      } else if (httpResponseCode == 502) {
        lastRecognitionResult = "ERROR: Service unavailable - " + errorMsg;
        Serial.println("Service error (" + String(httpResponseCode) + "): " + errorMsg);
      } else if (httpResponseCode == 503) {
        lastRecognitionResult = "ERROR: Service unavailable";
        Serial.println("Service unavailable (" + String(httpResponseCode) + ")");
      } else {
        lastRecognitionResult = "ERROR: " + errorMsg;
        Serial.println("API Error (" + String(httpResponseCode) + "): " + errorMsg);
      }
    } else {
      lastRecognitionResult = "ERROR: HTTP " + String(httpResponseCode) + " - Invalid response";
      Serial.println("HTTP " + String(httpResponseCode) + " with invalid JSON response");
    }
  } else {
    // Network errors (negative response codes like -1, -11, etc.)
    String errorMsg = "Connection failed";
    if (httpResponseCode == -1) {
      errorMsg = "Connection refused";
    } else if (httpResponseCode == -11) {
      errorMsg = "Timeout";
    } else if (httpResponseCode == -4) {
      errorMsg = "Connection lost";
    }
    
    lastRecognitionResult = "ERROR: " + errorMsg + " (" + String(httpResponseCode) + ")";
    Serial.println("Network Error: " + errorMsg + " (Code: " + String(httpResponseCode) + ")");
  }
  
  http.end();
  return httpResponseCode == 200;
}

// Handle attendance type change
void handleAttendanceTypeChange() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  // Get query parameters
  String recordId = server.arg("record_id");
  String newAttendanceType = server.arg("new_attendance_type");
  
  // Validate parameters
  if (recordId.length() == 0) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing record_id parameter\"}");
    return;
  }
  
  if (newAttendanceType != "entry" && newAttendanceType != "exit") {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid attendance type. Use 'entry' or 'exit'.\"}");
    return;
  }
  
  Serial.println("Changing attendance type for record '" + recordId + "' to '" + newAttendanceType + "'");
  
  // Call API to change attendance type
  bool success = callChangeAttendanceType(recordId, newAttendanceType);
  
  if (!success && lastRecognitionResult.length() == 0) {
    lastRecognitionResult = "ERROR: Failed to connect to edge system";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(502, "application/json", "{\"status\":\"error\",\"message\":\"Failed to change attendance type on Visage Edge\"}");
    return;
  }
  
  if (success) {
    // Automatically update central server after successful attendance type change (like main.py)
    if (currentEmployeeName.length() > 0) {
      Serial.println("Auto-updating central server after attendance type change for user: " + currentEmployeeName);
      bool centralServerResult = callUpdateCentralServerDirect(currentEmployeeName);
      if (centralServerResult) {
        Serial.println("Central server updated successfully after attendance type change");
      } else {
        Serial.println("Central server update failed after attendance type change");
      }
    } else {
      Serial.println("No employee name available for central server update");
    }
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Attendance type changed and central server updated successfully\"}");
  } else {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(502, "application/json", "{\"status\":\"error\",\"message\":\"Failed to change attendance type\"}");
  }
}

// Handle notification POST request
void handleNotification() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  // Only handle JSON data 
  String body = server.arg("plain");
  Serial.printf("Body length: %d\n", body.length());
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    // If not JSON, treat the entire body as the message
    currentMessage = body;
    messageTimestamp = "";
    // Default to entry for non-JSON messages
    currentAttendanceType = ATTENDANCE_IN;
  } else {
    // Parse JSON - handle both old format and new format from main.py
    currentMessage = doc["message"].as<String>();
    if (doc.containsKey("timestamp")) {
      messageTimestamp = doc["timestamp"].as<String>();
    } else {
      messageTimestamp = "";
    }
    // Handle record_id and employee name for central server update functionality
    if (doc.containsKey("record_id")) {
      currentRecordId = doc["record_id"].as<String>();
    }
    // Handle main.py format
    if (doc.containsKey("name") || doc.containsKey("username")) {
      String username = doc.containsKey("name") ? doc["name"].as<String>() : doc["username"].as<String>();
      if (username.length() > 0 && username != "Unknown User") {
        currentMessage = "Name: " + username;
        currentEmployeeName = username; // Store for central server update
      }
    }
    // Handle attendance_type from notification (same logic as main.py)
    if (doc.containsKey("attendance_type")) {
      String attendanceType = doc["attendance_type"].as<String>();
      if (attendanceType == "entry") {
        currentAttendanceType = ATTENDANCE_IN;
      } else if (attendanceType == "exit") {
        currentAttendanceType = ATTENDANCE_OUT;
      } else {
        // Default to IN for unknown types
        currentAttendanceType = ATTENDANCE_IN;
      }
      Serial.println("Attendance type from notification: " + attendanceType);
    } else {
      // If no attendance_type in notification, default to IN
      currentAttendanceType = ATTENDANCE_IN;
    }
    showUpdateButton = true;
    Serial.printf("Extracted message: %s\n", currentMessage.c_str());
    if (messageTimestamp.length() > 0) {
      Serial.printf("Extracted timestamp: %s\n", messageTimestamp.c_str());
    }
    if (currentRecordId.length() > 0) {
      Serial.printf("Extracted record_id: %s\n", currentRecordId.c_str());
    }
  }
  
  messageTime = millis();
  hasNewMessage = true;
  hasReceivedName = true;
  showingWelcome = false;

  // Start/reset timer to return to default page
  lastUserDetectedTime = millis();
  waitingForDefaultReturn = true;

  Serial.println("Received notification:");
  Serial.println("Message: " + currentMessage);
  if (messageTimestamp.length() > 0) {
    Serial.println("Timestamp: " + messageTimestamp);
  }
  
  
  Serial.println("Attendance type set to: " + String((currentAttendanceType == ATTENDANCE_IN) ? "IN" : "OUT"));

  // Send response
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Notification received and broadcasted.\"}");
}

// Handle health check request
void handleHealth() {
  String response = "{";
  response += "\"status\":\"ok\",";
  response += "\"device_id\":\"DISPLAY_001\",";
  response += "\"location\":\"Main Entrance\"";
  response += "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

// Handle root GET request - show API info
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Visage Display</title></head><body>";
  html += "<h1>Visage Display Notification Server</h1>";
  html += "<p>Device IP: " + WiFi.localIP().toString() + "</p>";
  html += "<p>Current Message: " + currentMessage + "</p>";
  html += "<p>Edge API URL: " + edgeApiBaseUrl + "</p>";
  
  html += "<h2>Send Notification</h2>";
  html += "<form method='POST' action='/notify'>";
  html += "<label>Message:</label><br>";
  html += "<textarea name='message' rows='4' cols='50'></textarea><br><br>";
  html += "<input type='submit' value='Send Notification'>";
  html += "</form>";
  html += "<h2>API Usage</h2>";
  html += "<p>POST JSON to /api/v1/display/notification: {\"message\":\"Your message\", \"name\":\"Username\"}</p>";
  html += "<p>POST to /api/v1/display/test-notification: Send test notification</p>";
  html += "<p>POST to /api/v1/attendance/entry: Trigger entry attendance</p>";
  html += "<p>POST to /api/v1/attendance/exit: Trigger exit attendance</p>";
  html += "<p>GET /health: Health check</p>";
  html += "<h2>Configuration</h2>";
  html += "<p><a href='/config'>Configure Edge API Settings</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handle form submission from web interface
void handleNotifyForm() {
  if (server.method() == HTTP_POST) {
    currentMessage = server.arg("message");
    messageTime = millis();
    hasNewMessage = true;
    hasReceivedName = true;
    showingWelcome = false;
    
    // Set attendance type to default (IN)
    currentAttendanceType = ATTENDANCE_IN;
    
    // Start/reset timer to return to default page
    lastUserDetectedTime = millis();
    waitingForDefaultReturn = true;
    
    Serial.println("Received notification via web form:");
    Serial.println("Message: " + currentMessage);
    Serial.println("Attendance type set to: " + String((currentAttendanceType == ATTENDANCE_IN) ? "IN" : "OUT"));
    
    // Manual redirect using HTTP headers
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    handleNotification();
  }
}

// Handle configuration page
void handleConfig() {
  String html = "<!DOCTYPE html><html><head><title>Visage Display Configuration</title></head><body>";
  html += "<h1>Visage Display Configuration</h1>";
  html += "<h2>API Settings</h2>";
  html += "<form method='POST' action='/api/config'>";
  html += "<label>Edge API Base URL:</label><br>";
  html += "<input type='text' name='edge_api_url' value='" + edgeApiBaseUrl + "' style='width: 300px;'><br><br>";
  html += "<h3>Central Server Settings (Direct Mode)</h3>";
  html += "<label>Central Server Enabled:</label><br>";
  html += "<input type='checkbox' name='central_server_enabled' " + String(centralServerEnabled ? "checked" : "") + "> Enable Direct Central Server Updates<br><br>";
  html += "<label>Central Server Endpoint:</label><br>";
  html += "<input type='text' name='central_server_endpoint' value='" + centralServerEndpoint + "' style='width: 400px;'><br><br>";
  html += "<label>API Key:</label><br>";
  html += "<input type='text' name='central_server_api_key' value='" + centralServerApiKey + "' style='width: 300px;'><br><br>";
  html += "<label>Username:</label><br>";
  html += "<input type='text' name='central_server_username' value='" + centralServerUsername + "' style='width: 300px;'><br><br>";
  html += "<input type='submit' value='Update Configuration'>";
  html += "</form>";
  html += "<h2>Current Status</h2>";
  html += "<p>Device IP: " + WiFi.localIP().toString() + "</p>";
  html += "<p>Edge API URL: " + edgeApiBaseUrl + "</p>";
  html += "<p><strong>Central Server Direct Mode: " + String(centralServerEnabled ? "ENABLED" : "DISABLED") + "</strong></p>";
  html += "<p>Central Server Endpoint: " + centralServerEndpoint + "</p>";
  html += "<p>Processing Recognition: " + String(isProcessingRecognition ? "Yes" : "No") + "</p>";
  html += "<p>Last Result: " + lastRecognitionResult + "</p>";
  html += "<p><a href='/'>Back to Main</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handle configuration update
void handleConfigUpdate() {
  if (server.method() == HTTP_POST) {
    String newEdgeApiUrl = server.arg("edge_api_url");
    if (newEdgeApiUrl.length() > 0) {
      edgeApiBaseUrl = newEdgeApiUrl;
      Serial.println("Edge API URL updated to: " + edgeApiBaseUrl);
    }
    
    // Update central server settings
    centralServerEnabled = server.hasArg("central_server_enabled");
    Serial.println("Central Server Enabled: " + String(centralServerEnabled ? "Yes" : "No"));
    
    String newCentralServerEndpoint = server.arg("central_server_endpoint");
    if (newCentralServerEndpoint.length() > 0) {
      centralServerEndpoint = newCentralServerEndpoint;
      Serial.println("Central Server Endpoint updated to: " + centralServerEndpoint);
    }
    
    String newCentralServerApiKey = server.arg("central_server_api_key");
    if (newCentralServerApiKey.length() > 0) {
      centralServerApiKey = newCentralServerApiKey;
      Serial.println("Central Server API Key updated");
    }
    
    String newCentralServerUsername = server.arg("central_server_username");
    if (newCentralServerUsername.length() > 0) {
      centralServerUsername = newCentralServerUsername;
      Serial.println("Central Server Username updated to: " + centralServerUsername);
    }
    
    // Redirect back to config page
    server.sendHeader("Location", "/config", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}