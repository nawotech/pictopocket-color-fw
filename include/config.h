/*****************************************************************************
 * | File      	:   config.h
 * | Function    :   Device configuration constants
 ******************************************************************************/
#ifndef _CONFIG_H_
#define _CONFIG_H_

// Image storage constants
#define IMAGE_SIZE_BYTES 120000  // 400x600 pixels, 2 pixels per byte = 120,000 bytes
#define MAX_IMAGES 12            // Maximum number of images we can store
#define STORAGE_PARTITION_LABEL "storage"

// Display constants
#define DISPLAY_WIDTH 400
#define DISPLAY_HEIGHT 600

// Wake cycle constants
#define WAKE_INTERVAL_HOURS 4
#define WAKE_INTERVAL_MICROSECONDS (WAKE_INTERVAL_HOURS * 3600ULL * 1000000ULL)
#define WAKES_PER_DAY 6  // 24 hours / 4 hours = 6 wakes

#endif

