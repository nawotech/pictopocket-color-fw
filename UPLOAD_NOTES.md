# Upload Notes - Preserving NVS Partition

## Problem

When uploading firmware, esptool may erase sectors that overlap with the NVS partition (0x9000-0xF000), which contains the device key. This happens because esptool automatically erases sectors it needs to write to.

## Solution

**IMPORTANT**: After running the setup sketch to save your device key, when uploading the main firmware:

1. **Do NOT use `--erase-flash` or `-e` flag** - This will erase the entire flash including NVS
2. **Use standard upload command**: `pio run -e esp32-c3-m1i-kit -t upload`
3. **If the key gets erased**, simply re-run the setup sketch to save it again

## Workflow

1. **First time setup**:
   ```bash
   cd picto-color-setup
   pio run -e esp32-c3-m1i-kit -t upload
   pio device monitor
   # Enter your device key when prompted
   # Verify it was saved successfully
   ```

2. **Upload main firmware** (preserves NVS):
   ```bash
   cd pictopocket-color-fw
   pio run -e esp32-c3-m1i-kit -t upload
   # Do NOT use: pio run -e esp32-c3-m1i-kit -t upload -t erase
   ```

3. **If key is missing after upload**:
   - Re-run the setup sketch (step 1)
   - The key will be saved again

## Why This Happens

esptool erases flash in 4KB sectors. The erase output shows:
- `0x0000e000 to 0x0000ffff` - This overlaps with NVS partition (0x9000-0xF000)

Even though we're not writing to NVS, esptool may erase these sectors if:
- The partition table is being updated
- esptool is being conservative and erasing more than necessary

## Alternative: Custom Upload Script

If you need to ensure NVS is never erased, you can create a custom upload script that:
1. Backs up NVS partition before upload
2. Restores it after upload

However, for development, the simpler approach is to just re-run the setup sketch if needed.

