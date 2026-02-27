# Visage ESP32 Display

A touch-enabled ESP32 display controller for the Visage attendance system with face recognition integration. This system provides an interactive interface for attendance tracking with real-time notifications and central server synchronization.

## 🖥️ Overview

Visage ESP32 Display is an IoT display controller that serves as a visual interface for the Visage attendance system. It features a capacitive touchscreen interface, WiFi connectivity, and RESTful API endpoints for real-time attendance notifications and face recognition integration.

## 🔧 Hardware Requirements

### Main Components

- **ESP32 Board**: JC4827W543C (or compatible ESP32 board)
- **Display**: 480x272 pixels TFT LCD with NV3041A controller
- **Touch Panel**: GT911 Capacitive Touch Controller
- **Connection**: QSPI interface for display

### Pin Configuration

#### Display (QSPI)

- CS: GPIO 45
- SCK: GPIO 47
- D0: GPIO 21
- D1: GPIO 48
- D2: GPIO 40
- D3: GPIO 39

#### Touch (I2C)

- SCL: GPIO 4
- SDA: GPIO 8
- RST: GPIO 38
- INT: GPIO 3

## ⚡ Features

- **🎨 Touch-Enabled Interface**: Capacitive touchscreen with button interactions
- **📡 WiFi Connectivity**: WiFiManager for easy network configuration
- **🔄 Real-time Updates**: Live attendance notifications via REST API
- **👤 Face Recognition Integration**: Connects to Visage edge API for facial recognition
- **🌐 Web Server**: Built-in web interface for testing and configuration
- **🔔 Notification System**: Visual feedback for entry/exit attendance
- **⏰ Time-based Greetings**: Context-aware welcome/goodbye messages
- **☁️ Central Server Sync**: Automatic synchronization with central attendance server
- **🎯 Dual Mode Support**: Entry (IN) and Exit (OUT) attendance modes

## 📚 Required Libraries

Install the following libraries through the Arduino Library Manager or manually:

### Core Libraries

```
Arduino_GFX_Library       // Display graphics library
WiFi                      // ESP32 WiFi connectivity
WiFiManager               // WiFi configuration portal
WebServer                 // HTTP web server
ArduinoJson               // JSON parsing and serialization
HTTPClient                // HTTP client for API calls
```

### Touch Libraries

```
TouchLib                  // Capacitive touch support (GT911)
Wire                      // I2C communication
```

### Installation via Arduino IDE

1. Open Arduino IDE
2. Go to **Sketch → Include Library → Manage Libraries**
3. Search and install each library listed above

## 🚀 Getting Started

### 1. Hardware Setup

1. Connect the JC4827W543C display board to your development system
2. Ensure all pin connections match the configuration above
3. Power the board via USB or external power supply

### 2. Software Configuration

#### Update Edge API Endpoint

In `visage-display.ino`, modify the edge API base URL:

```cpp
String edgeApiBaseUrl = "http://192.168.1.100:8000";
```

#### Central Server Configuration

Update the central server settings:

```cpp
String centralServerEndpoint = "https://visage.sltdigitallab.lk/api/update_last_activity";
String centralServerApiKey = "YOUR_API_KEY";
String centralServerUsername = "YOUR_USERNAME";
bool centralServerEnabled = true;
```

### 3. Upload Code

1. Open `visage-display.ino` in Arduino IDE
2. Select **Board**: ESP32 Dev Module (or appropriate board)
3. Select the correct **Port**
4. Click **Upload**

### 4. WiFi Setup

On first boot:

1. The device creates an access point named "VisageDisplay_XXXXXX"
2. Connect to this WiFi network from your phone/computer
3. Open browser and navigate to `192.168.4.1`
4. Select your WiFi network and enter credentials
5. Device will connect and display IP address on screen

## 🌐 API Endpoints

The web server runs on port 80 and provides the following endpoints:

### Notification Endpoints

- `POST /api/v1/display/notification` - Send notification data

  ```json
  {
    "message": "Welcome John Doe!",
    "type": "entry",
    "timestamp": "2024-08-15 12:34:56",
    "from": "Main Gate"
  }
  ```

- `POST /api/v1/display/test-notification` - Send test notification

### Attendance Endpoints

- `POST /api/v1/attendance/entry` - Trigger entry attendance with face recognition
- `POST /api/v1/attendance/exit` - Trigger exit attendance with face recognition

### System Endpoints

- `GET /health` - System health check and status
- `GET /` - Web interface for manual testing
- `GET /config` - Configuration management interface

### Response Format

```json
{
  "status": "success",
  "message": "Description of result",
  "data": {
    "username": "John Doe",
    "timestamp": "2024-08-15 12:34:56",
    "record_id": "12345"
  }
}
```

## 🎮 Usage

### Touch Interface

#### Welcome Screen

- Displays "Welcome!" or "Goodbye!" based on current mode
- Shows entry (green) or exit (red) button
- Tap button to trigger face recognition

#### Recognition Flow

1. User taps the attendance button
2. Display shows "Processing..." overlay
3. System calls edge API for face recognition
4. Results displayed with user name and timestamp
5. Automatically syncs with central server
6. Returns to welcome screen after timeout

#### Mode Switching

- Use the "Change to Entry/Exit" button on the message screen
- Switches between entry and exit modes
- Visual feedback with color-coded interface (green for entry, red for exit)

### Web Interface

Access the web interface by navigating to the device's IP address in a browser:

- View current status
- Send test notifications
- Configure settings
- Check system health

## 🔒 Network Configuration

### WiFi Manager Portal

If connection fails or to reconfigure WiFi:

1. Press and hold the reset button (if available)
2. Or power cycle the device
3. Connect to the "VisageDisplay_XXXXXX" access point
4. Configure network settings

### Static IP (Optional)

To set a static IP, modify in `setup()`:

```cpp
IPAddress local_ip(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
WiFi.config(local_ip, gateway, subnet);
```

## 🎨 Display Modes

### Entry Mode (IN)

- **Color Theme**: Blue and Green
- **Greeting**: "Welcome!"
- **Button**: Entry button (green gradient)
- **Use Case**: Morning check-ins, building entry

### Exit Mode (OUT)

- **Color Theme**: Red
- **Greeting**: "Goodbye!"
- **Button**: Exit button (red gradient)
- **Use Case**: Evening check-outs, building exit

## 🔧 Troubleshooting

### Display Not Working

- Check all pin connections match configuration
- Verify power supply is adequate (5V, 2A recommended)
- Ensure display driver (NV3041A) is properly initialized

### Touch Not Responding

- Verify I2C connections (SCL, SDA)
- Check touch controller address (GT911_SLAVE_ADDRESS1)
- Run touch calibration if needed

### WiFi Connection Issues

- Reset WiFi credentials using WiFiManager portal
- Check router compatibility (2.4GHz required)
- Verify network credentials are correct
- Check signal strength at device location

### API Calls Failing

- Verify edge API endpoint URL is correct
- Ensure device and edge API are on same network or have connectivity
- Check firewall settings
- Monitor Serial output for HTTP response codes

### Face Recognition Not Working

- Confirm edge API service is running
- Check camera connection to edge device
- Verify API endpoint returns valid JSON
- Review Serial Monitor for detailed error messages

## 📊 Serial Monitor Output

Enable Serial Monitor (115200 baud) to view:

- WiFi connection status
- API call details and responses
- Touch events and coordinates
- System status and errors
- HTTP request/response data

## 🔄 System Architecture

```
┌─────────────────┐
│  Visage Display │
│    (ESP32)      │
└────────┬────────┘
         │
         ├─→ Touch Input → Trigger Recognition
         │
         ├─→ Edge API → Face Recognition
         │
         ├─→ Central Server → Update Records
         │
         └─→ Display Output → Show Results
```

## 🛠️ Development

### File Structure

```
visage-esp32-display/
├── visage-display.ino    # Main Arduino sketch
├── touch.h               # Touch controller configuration
├── .gitignore           # Git ignore rules
└── README.md            # This file
```
             |

## 🤝 Contributing

To contribute to this project:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly on hardware
5. Submit a pull request

## 📄 License

This project is part of the Visage attendance system. Please refer to the main repository for licensing information.

## 🆘 Support

For issues, questions, or contributions:

- Check the Serial Monitor output for debugging information
- Review API endpoint responses
- Verify hardware connections
- Consult the Visage system documentation

## 🔗 Related Projects

- **Visage Edge API**: Face recognition service
- **Visage Central Server**: Main attendance management system

---

**Note**: This system is designed for the JC4827W543C ESP32 display board. Pin configurations and display drivers may need adjustment for other hardware.

Last Updated: February 2026
