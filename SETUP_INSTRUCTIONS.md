# Device Setup Instructions

## Getting the Device ID

The device ID is automatically generated from the ESP32's MAC address. To get it:

1. **Via Serial Monitor** (if you have Serial enabled):
   - Upload a simple sketch that prints the MAC address
   - Or check the Serial output when device boots

2. **Via Code**:
   ```cpp
   WiFi.mode(WIFI_STA);
   uint8_t mac[6];
   WiFi.macAddress(mac);
   char deviceId[32];
   snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   ```

3. **Format**: `XXXXXXXXXXXX` (12 hex digits, MAC address)
   - Example: `A1B2C3D4E5F6`

## Setting the Device Key

### Method 1: Setup Sketch (Recommended)

1. **Temporarily rename files**:
   ```bash
   cd src
   mv main.cpp main.cpp.backup
   cp setup_device_key.cpp main.cpp
   ```

2. **Upload the setup sketch**:
   ```bash
   pio run -e esp32-c3-m1i-kit -t upload
   ```

3. **Open Serial Monitor** (115200 baud):
   ```bash
   pio device monitor
   ```

4. **Enter device key** when prompted:
   - Device key should be 64 hex characters (32 bytes)
   - Example: `a1b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef123456`

5. **Restore main firmware**:
   ```bash
   cd src
   mv main.cpp setup_device_key.cpp
   mv main.cpp.backup main.cpp
   ```

6. **Upload main firmware**:
   ```bash
   pio run -e esp32-c3-m1i-kit -t upload
   ```

### Method 2: Add Setup Mode to Main Firmware

You can modify `main.cpp` to include setup mode on first boot:

```cpp
// In setup(), after NVS initialization:
if (!NVSStorage::hasDeviceKey()) {
  // Enter setup mode
  Serial.begin(115200);
  delay(2000);
  Serial.println("Enter device key (64 hex chars):");
  while (!Serial.available()) delay(10);
  String key = Serial.readStringUntil('\n');
  key.trim();
  if (key.length() == 64) {
    NVSStorage::saveDeviceKey(key);
  }
}
```

### Method 3: Use ESP-IDF NVS Tools

1. Install ESP-IDF
2. Use `nvs_partition_gen.py` to create NVS partition image
3. Flash partition using `esptool.py`

## Creating Firestore Document

After getting the device ID and setting the device key:

1. **Generate device key** (if not already done):
   ```bash
   # Generate 32 random bytes, convert to hex
   openssl rand -hex 32
   ```

2. **Hash the device key** (for Firestore):
   ```bash
   # Using Node.js
   node -e "const crypto = require('crypto'); console.log(crypto.createHash('sha256').update('YOUR_DEVICE_KEY').digest('hex'))"
   ```

3. **Create Firestore document**:
   - Collection: `frames`
   - Document ID: `XXXXXXXXXXXX` (your device ID, MAC address)
   - Fields:
     ```json
     {
       "deviceKeyHash": "sha256_hash_of_device_key",
       "friendlyName": "Living Room Frame",
       "ownerUid": "your-firebase-user-id",
       "slideshowVersion": 0,
       "imageIds": [],
       "imageHashes": [],
       "lastDisplayedVersion": 0
     }
     ```

## Verification

1. **Check device key in NVS**:
   - Upload setup sketch
   - It will show existing key if present

2. **Check device ID**:
   - Upload a test sketch that prints MAC address
   - Or check Serial output

3. **Test connection**:
   - Upload main firmware
   - Device should connect to WiFi
   - Check Firebase Console for `deviceLastSeenAt` timestamp updates

## Troubleshooting

- **"No device key found"**: Run setup sketch to write key
- **"Device ID mismatch"**: Make sure Firestore document ID matches device ID format
- **"Authentication failed"**: Check that device key hash in Firestore matches the hash of your key
- **"WiFi connection failed"**: Check WiFi credentials in `wifi_config.h`

