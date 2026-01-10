#include "../include/wifi_config.h"

// WiFi configuration - UPDATE THESE WITH YOUR CREDENTIALS
const char *WIFI_SSID = "monitorlizard";
const char *WIFI_PASSWORD = "BeanFart69!true";

// Firebase Cloud Function URLs - Update these after deploying functions
// Find URLs in Firebase Console > Functions > [function name] > Copy URL
const char *GET_SLIDESHOW_VERSION_URL = "https://get-slideshow-version-5rtbel6b4a-uc.a.run.app";
const char *GET_SLIDESHOW_MANIFEST_URL = "https://get-slideshow-manifest-5rtbel6b4a-uc.a.run.app";
const char *GET_SIGNED_URLS_URL = "https://get-signed-urls-5rtbel6b4a-uc.a.run.app";
const char *ACK_DISPLAYED_URL = "https://ack-displayed-5rtbel6b4a-uc.a.run.app";
