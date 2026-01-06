#include "../include/wifi_config.h"

// WiFi configuration - UPDATE THESE WITH YOUR CREDENTIALS
const char *WIFI_SSID = "monitorlizard";
const char *WIFI_PASSWORD = "BeanFart69!true";

// Firebase Cloud Function URLs - Update these after deploying functions
// Find URLs in Firebase Console > Functions > [function name] > Copy URL
const char *GET_SLIDESHOW_VERSION_URL = "https://YOUR-PROJECT-ID.cloudfunctions.net/get_slideshow_version";
const char *GET_SLIDESHOW_MANIFEST_URL = "https://YOUR-PROJECT-ID.cloudfunctions.net/get_slideshow_manifest";
const char *GET_SIGNED_URLS_URL = "https://YOUR-PROJECT-ID.cloudfunctions.net/get_signed_urls";
const char *ACK_DISPLAYED_URL = "https://YOUR-PROJECT-ID.cloudfunctions.net/ack_displayed";

