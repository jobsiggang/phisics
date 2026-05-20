/*
 * ESP32-CAM 식물 사진 업로더
 * - 보드: AI Thinker ESP32-CAM
 * - 서버: https://eyiot.vercel.app/plant-image
 * - 주기: 60초마다 사진 1장 업로드
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"

// ===== Wi-Fi 설정 =====
const char* WIFI_SSID = "bugs";
const char* WIFI_PASSWORD = "bugs1234";

// ===== 서버 설정 =====
const char* SERVER_HOST = "eyiot.vercel.app";
const int SERVER_PORT = 443;
const char* SERVER_PATH = "/plant-image";

// ===== 업로드 주기 =====
const unsigned long CAPTURE_INTERVAL_MS = 60UL * 1000UL;  // 60초
unsigned long lastCaptureMs = 0;

// ===== AI Thinker ESP32-CAM 핀맵 =====
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

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("[WiFi] 연결 시도: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] 연결됨: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("[WiFi] 연결 실패");
  return false;
}

bool initCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] 초기화 실패: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
  }

  Serial.println("[CAM] 초기화 완료");
  return true;
}

bool uploadPhoto() {
  if (!ensureWiFi()) return false;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] 캡처 실패");
    return false;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    Serial.println("[CAM] JPEG 포맷 아님");
    esp_camera_fb_return(fb);
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(SERVER_HOST, SERVER_PORT)) {
    Serial.println("[HTTP] 서버 연결 실패");
    esp_camera_fb_return(fb);
    return false;
  }

  String boundary = "----ESP32CAMBoundary7MA4YWxkTrZu0gW";
  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"image\"; filename=\"plant.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t contentLength = head.length() + fb->len + tail.length();

  client.print(String("POST ") + SERVER_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + SERVER_HOST + "\r\n");
  client.print("User-Agent: ESP32-CAM\r\n");
  client.print("Connection: close\r\n");
  client.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
  client.print(String("Content-Length: ") + contentLength + "\r\n\r\n");

  client.print(head);
  client.write(fb->buf, fb->len);
  client.print(tail);

  esp_camera_fb_return(fb);

  unsigned long start = millis();
  while (!client.available() && millis() - start < 15000) {
    delay(20);
  }

  if (!client.available()) {
    Serial.println("[HTTP] 응답 타임아웃");
    client.stop();
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.print("[HTTP] ");
  Serial.println(statusLine);

  String body = "";
  bool inBody = false;
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r") inBody = true;
      continue;
    }
    body += line;
    if (body.length() > 600) {
      body = body.substring(0, 600);
      break;
    }
  }

  Serial.print("[HTTP] body: ");
  Serial.println(body);

  client.stop();
  return statusLine.indexOf("200") >= 0;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[BOOT] ESP32-CAM 시작");

  if (!initCamera()) {
    Serial.println("[BOOT] 카메라 초기화 실패 - 재부팅 필요");
    return;
  }

  ensureWiFi();

  // 부팅 직후 즉시 1회 전송
  uploadPhoto();
  lastCaptureMs = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    ensureWiFi();
  }

  unsigned long now = millis();
  if (now - lastCaptureMs >= CAPTURE_INTERVAL_MS) {
    lastCaptureMs = now;
    bool ok = uploadPhoto();
    Serial.println(ok ? "[CAM] 업로드 성공" : "[CAM] 업로드 실패");
  }

  delay(200);
}
