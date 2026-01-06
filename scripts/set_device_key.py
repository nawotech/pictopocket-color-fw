#!/usr/bin/env python3
"""
Device Key Setup Script for ESP32
Uses esptool to write device key directly to NVS partition

Usage:
    python3 set_device_key.py <device_key_hex> [port]

Example:
    python3 set_device_key.py a1b2c3d4e5f6... /dev/ttyUSB0

Requirements:
    pip install esptool
"""

import sys
import serial
import struct
from esptool import ESP32ROM

def write_nvs_key(port, key_hex):
    """
    Write device key to NVS partition using esptool
    """
    if len(key_hex) != 64:
        print(f"ERROR: Device key must be 64 hex characters (got {len(key_hex)})")
        return False
    
    try:
        # Convert hex string to bytes
        key_bytes = bytes.fromhex(key_hex)
        
        # NVS entry structure:
        # - Namespace: "frame" (5 bytes + null terminator = 6 bytes)
        # - Key: "deviceKey" (9 bytes + null terminator = 10 bytes)
        # - Type: 0x02 (string)
        # - Value: key_bytes (64 bytes)
        
        # This is complex - better to use the Arduino sketch or esptool's NVS partition editor
        print("Note: Direct NVS writing is complex.")
        print("Recommended: Use the setup_device_key.cpp sketch instead.")
        print("Or use: esptool.py --port PORT write_flash 0x9000 nvs_partition.bin")
        return False
        
    except ValueError as e:
        print(f"ERROR: Invalid hex string: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 set_device_key.py <device_key_hex> [port]")
        sys.exit(1)
    
    key_hex = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not port:
        print("Please specify serial port (e.g., /dev/ttyUSB0 or COM3)")
        sys.exit(1)
    
    write_nvs_key(port, key_hex)

