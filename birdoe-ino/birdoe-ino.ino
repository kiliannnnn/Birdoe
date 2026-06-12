#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

#include "FS.h"
#include "SD_MMC.h"

// AI Thinker ESP32-CAM pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const char* WIFI_SSID = "Livebox-F270";
const char* WIFI_PASSWORD = "uMSTAGG6naNjZrCq45";

const char* SERVER_URL = "http://192.168.1.43:3000/capture";
const char* DEVICE_ID = "birdbox-esp32cam-01";

const unsigned long CAPTURE_INTERVAL_MS = 30000;
const int HTTP_TIMEOUT_MS = 10000;

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // important pour éviter les micro-coupures Wi-Fi
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WiFi connecting");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts > 60) {
      Serial.println("\nWiFi connection failed, restarting");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi lost, reconnecting...");
  WiFi.disconnect();
  delay(500);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi reconnect failed, restarting");
    ESP.restart();
  }

  Serial.println("WiFi reconnected");
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Stable pour les premiers tests.
  config.frame_size = FRAMESIZE_QVGA; // 320x240
  config.jpeg_quality = 15;           // plus haut = fichier plus petit / qualité plus basse
  config.fb_count = 1;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(3000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // Ajustements prudents, tu pourras affiner plus tard.
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  Serial.println("Camera initialized");
  Serial.print("PSRAM: ");
  Serial.println(psramFound() ? "yes" : "no");
}

int postImage(camera_fb_t* fb) {
  WiFiClient client;
  client.setTimeout(HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!http.begin(client, SERVER_URL)) {
    Serial.println("HTTP begin failed");
    client.stop();
    return -999;
  }

  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Connection", "close");
  http.addHeader("x-device", DEVICE_ID);
  http.addHeader("x-rssi", String(WiFi.RSSI()));
  http.addHeader("x-free-heap", String(ESP.getFreeHeap()));

  int status = http.POST(fb->buf, fb->len);

  Serial.printf("POST status: %d\n", status);

  if (status > 0) {
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.printf("HTTP error: %s\n", http.errorToString(status).c_str());
  }

  http.end();
  client.stop();

  return status;
}

void captureAndUpload() {
  ensureWifi();

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  Serial.println("-----");
  Serial.printf("Captured image: %u bytes\n", fb->len);
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  int status = postImage(fb);

  if (status <= 0) {
    Serial.println("Retrying once in 2 seconds...");
    delay(2000);
    ensureWifi();
    status = postImage(fb);
  }

  esp_camera_fb_return(fb);

  if (status == 201 || status == 200) {
    Serial.println("Upload OK");
  } else {
    Serial.println("Upload failed after retry");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Booting Birdbox ESP32-CAM");

  connectWifi();
  initCamera();

  // if (!SD_MMC.begin("/sdcard", true)) {
  //   Serial.println("SD init failed");
  // } else {
  //   Serial.println("SD init OK");

  //   uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  //   Serial.printf("SD size: %llu MB\n", cardSize);
  // }

  Serial.println("Birdbox ready");
}

void loop() {
  captureAndUpload();
  delay(CAPTURE_INTERVAL_MS);
}