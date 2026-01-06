# ESP32-C3 E-Ink Photo Frame Firmware

## Overview

This firmware implements the slideshow architecture for the ESP32-C3 e-ink photo frame. The device wakes every 4 hours, checks for new slideshow updates, and advances through images once per 24 hours.

## Architecture

### Key Components

1. **NVS Storage** (`nvs_storage.h/cpp`): Stores device state including:
   - Device key (for authentication)
   - Current image index
   - Wake counter
   - Slideshow version
   - Image IDs and hashes

2. **Flash Storage** (`flash_storage.h/cpp`): Stores images in LittleFS partition
   - Each image is 120KB (400x600 pixels, 2 pixels per byte)
   - Can store up to 12 images (1.5MB partition)

3. **API Client** (`api_client.h/cpp`): HTTP client for Firebase Cloud Functions
   - `getSlideshowVersion()`: Check if new slideshow available
   - `getSlideshowManifest()`: Get list of image IDs and hashes
   - `getSignedUrls()`: Get download URLs for images
   - `downloadImage()`: Download image from signed URL
   - `ackDisplayed()`: Acknowledge slideshow display

4. **Main Loop** (`main.cpp`): Orchestrates the wake cycle
   - WiFi connection (with saved credentials for fast reconnect)
   - Check for new slideshow
   - Download and store images
   - Display current image
   - Deep sleep for 4 hours

## Configuration

### 1. Update `wifi_config.h`

```cpp
// WiFi credentials
const char *WIFI_SSID = "your-wifi-ssid";
const char *WIFI_PASSWORD = "your-wifi-password";

// Firebase Cloud Function URLs (get from Firebase Console)
const char *GET_SLIDESHOW_VERSION_URL = "https://YOUR-PROJECT.cloudfunctions.net/get_slideshow_version";
const char *GET_SLIDESHOW_MANIFEST_URL = "https://YOUR-PROJECT.cloudfunctions.net/get_slideshow_manifest";
const char *GET_SIGNED_URLS_URL = "https://YOUR-PROJECT.cloudfunctions.net/get_signed_urls";
const char *ACK_DISPLAYED_URL = "https://YOUR-PROJECT.cloudfunctions.net/ack_displayed";

// Note: Device ID is automatically generated from MAC address
// Format: XXXXXXXXXXXX (12 hex digits, e.g., A1B2C3D4E5F6)
```

### 2. Set Device Key in NVS

The device key must be stored in NVS before the device can authenticate. The device key is a 64-character hex string (32 bytes).

**Recommended Method: Use Setup Sketch**

1. Upload `setup_device_key.cpp` to your ESP32 (rename `main.cpp` temporarily)
2. Open Serial Monitor (115200 baud)
3. Enter your device key when prompted (64 hex characters)
4. The key will be saved to NVS
5. Upload the main firmware (`main.cpp`)

**Alternative: Add Setup Mode to Main Firmware**

You can add a setup mode that activates on first boot or button press:
- Check if device key exists in NVS
- If not, enter setup mode and read key from Serial
- Save key to NVS

**Device ID**

The device ID is automatically generated from the ESP32's MAC address:
- Format: `XXXXXXXXXXXX` (12 hex digits, MAC address)
- Example: `A1B2C3D4E5F6`
- This ensures each device has a unique ID based on hardware
- Use this ID (MAC address) as the Firestore document ID for the frame

### 3. Button Pin (Optional)

If your hardware has a button for manual image advance, update `BUTTON_PIN` in `main.cpp`:

```cpp
#define BUTTON_PIN 9  // Change to your GPIO pin
```

If no button, the code will compile but button functionality will be disabled.

## Partition Table

The partition table (`partitions.csv`) is configured as:
- **NVS**: 24KB (device state)
- **phy_init**: 4KB (PHY calibration)
- **factory**: 2.5MB (application code)
- **storage**: 1.5MB (image storage, LittleFS)

This allows storing ~12 images at 120KB each.

## Wake Cycle Behavior

1. **Wake from deep sleep** (every 4 hours)
2. **Initialize storage** (NVS and flash)
3. **Check button** (if pressed, advance image and go back to sleep)
4. **Connect WiFi** (uses saved credentials for fast reconnect)
5. **Check for new slideshow** (`get_slideshow_version`)
   - If new version available, download manifest and images
6. **Increment wake counter**
7. **Advance image** (if wake counter >= 6, i.e., 24 hours passed)
8. **Display current image** (from flash storage)
9. **Acknowledge display** (`ack_displayed`)
10. **Save state** (to NVS)
11. **Deep sleep** (4 hours)

## Image Display

- Images are stored in flash as raw 3-bit E-Ink format (120KB each)
- Display uses `EPD_4IN0E_Display()` function from the display library
- Display is put to sleep after showing image to save power

## Error Handling

- **No device key**: Device goes to sleep (needs configuration)
- **WiFi failure**: Displays current image if available, then sleeps
- **API failure**: Displays current image if available, then sleeps
- **Download failure**: Partial downloads are not saved (all-or-nothing)

## Dependencies

- `ArduinoJson` (v7.0.0): For parsing JSON responses
- ESP32 Arduino Core: For WiFi, NVS, LittleFS, deep sleep
- Display library: EPD_4in0e (included in project)

## Building and Flashing

1. Install PlatformIO
2. Update `wifi_config.h` with your credentials
3. Build: `pio run -e esp32-c3-m1i-kit`
4. Flash: `pio run -e esp32-c3-m1i-kit -t upload`
5. Monitor: `pio device monitor`

## Testing

1. **First Boot**: Device should go to sleep immediately (no device key)
2. **After setting device key**: Device should connect to WiFi and check for slideshow
3. **With slideshow**: Device should download images and display first image
4. **Wake cycles**: Device should wake every 4 hours and advance image every 24 hours

## Troubleshooting

- **Device won't wake**: Check deep sleep configuration and RTC memory
- **WiFi won't connect**: Check credentials and signal strength
- **Images won't download**: Check Cloud Function URLs and device key
- **Display blank**: Check display initialization and image format
- **Out of memory**: Reduce `MAX_IMAGES` or increase partition size

## Notes

- Serial output is commented out for production (uncomment for debugging)
- Device key is stored in plaintext in NVS (it's hashed before sending to server)
- Images are stored in flash and persist across deep sleep
- Wake counter resets when new slideshow is downloaded

