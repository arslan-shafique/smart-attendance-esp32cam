#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "config.h"

// ================= ONBOARD FLASH LED =================
#define FLASH_LED 4

// ================= CAMERA PINS AI THINKER =================
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

httpd_handle_t camera_httpd = NULL;

String currentStatus = "System Ready";
String lastMarkedName = "";
unsigned long lastMarkedTime = 0;

// 30 seconds gap to avoid repeated same student marking
const unsigned long MARK_COOLDOWN = 30000;

// ================= URL ENCODE =================
String urlEncode(String text) {
  String encoded = "";

  for (int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);

    if (isalnum(c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", c);
      encoded += buf;
    }
  }

  return encoded;
}

// ================= GOOGLE SHEET =================
void sendAttendanceToGoogleSheet(String name) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  HTTPClient http;

  String url = GOOGLE_SCRIPT_URL;
  url += "?name=" + urlEncode(name);
  url += "&status=Present";
  url += "&device=ESP32-CAM";

  Serial.println("Sending attendance to Google Sheet...");
  Serial.println(url);

  http.begin(url);
  http.setTimeout(15000);

  int httpCode = http.GET();

  Serial.print("Google Sheet response code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    Serial.println(http.getString());
  }

  http.end();
}

// ================= FACE SERVER TEST =================
void testBackendConnection() {
  HTTPClient http;

  Serial.println();
  Serial.println("Testing backend connection...");
  Serial.println(FACE_SERVER_TEST_URL);

  http.begin(FACE_SERVER_TEST_URL);
  http.setTimeout(10000);

  int code = http.GET();

  Serial.print("Backend test code: ");
  Serial.println(code);

  if (code > 0) {
    Serial.println("Backend response:");
    Serial.println(http.getString());
  } else {
    Serial.println("Backend connection failed.");
    Serial.println("Check laptop IP, firewall, same WiFi, and backend server.");
  }

  http.end();
}

// ================= FACE RECOGNITION FROM BACKEND =================
String recognizeStudent() {
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    currentStatus = "Camera capture failed";
    return "Unknown";
  }

  HTTPClient http;

  Serial.println();
  Serial.println("Sending image to face server...");
  Serial.println(FACE_SERVER_URL);

  http.begin(FACE_SERVER_URL);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(fb->buf, fb->len);

  String response = "";

  if (httpCode > 0) {
    response = http.getString();

    Serial.print("Face server HTTP code: ");
    Serial.println(httpCode);

    Serial.println("Face server response:");
    Serial.println(response);
  } else {
    Serial.print("Face server request failed. Code: ");
    Serial.println(httpCode);
  }

  http.end();
  esp_camera_fb_return(fb);

  if (response.indexOf("\"recognized\":true") >= 0) {
    int nameIndex = response.indexOf("\"name\":\"");

    if (nameIndex >= 0) {
      nameIndex += 8;
      int endIndex = response.indexOf("\"", nameIndex);

      if (endIndex > nameIndex) {
        return response.substring(nameIndex, endIndex);
      }
    }
  }

  return "Unknown";
}

// ================= STREAM HANDLER =================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;

  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");

  while (true) {
    fb = esp_camera_fb_get();

    if (!fb) {
      currentStatus = "Camera stream failed";
      digitalWrite(FLASH_LED, LOW);
      return ESP_FAIL;
    }

    httpd_resp_send_chunk(req, "--frame\r\n", strlen("--frame\r\n"));
    httpd_resp_send_chunk(req, "Content-Type: image/jpeg\r\n\r\n", strlen("Content-Type: image/jpeg\r\n\r\n"));
    httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    httpd_resp_send_chunk(req, "\r\n", 2);

    esp_camera_fb_return(fb);
    delay(30);
  }

  return ESP_OK;
}

// ================= ROOT PAGE =================
static esp_err_t index_handler(httpd_req_t *req) {
  String html = "";
  html += "<html><head>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>ESP32-CAM Attendance</title>";
  html += "</head><body>";
  html += "<h2>ESP32-CAM Smart Attendance</h2>";
  html += "<img src='/stream' style='width:100%; max-width:480px;'><br>";
  html += "<h3>Status: " + currentStatus + "</h3>";
  html += "<p>LED ON = Attendance marked successfully</p>";
  html += "</body></html>";

  httpd_resp_send(req, html.c_str(), html.length());
  return ESP_OK;
}

// ================= SERVER START =================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  httpd_start(&camera_httpd, &config);
  httpd_register_uri_handler(camera_httpd, &index_uri);
  httpd_register_uri_handler(camera_httpd, &stream_uri);
}

// ================= CAMERA SETUP =================
void setupCamera() {
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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  setupCamera();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");

  Serial.print("ESP32-CAM Stream URL: http://");
  Serial.println(WiFi.localIP());

  Serial.print("Face Server URL: ");
  Serial.println(FACE_SERVER_URL);

  testBackendConnection();

  startCameraServer();

  currentStatus = "System ready - stand in frame";
  Serial.println("Camera Ready! Open ESP32 IP in browser");
}

// ================= LOOP =================
void loop() {
  currentStatus = "Recognizing face...";
  Serial.println();
  Serial.println(currentStatus);

  String studentName = recognizeStudent();

  if (studentName != "Unknown") {
    currentStatus = studentName + " recognized";

    unsigned long now = millis();

    if (studentName != lastMarkedName || now - lastMarkedTime > MARK_COOLDOWN) {
      currentStatus = studentName + " marked present";
      Serial.println(currentStatus);

      digitalWrite(FLASH_LED, HIGH);

      sendAttendanceToGoogleSheet(studentName);

      delay(3000);
      digitalWrite(FLASH_LED, LOW);

      lastMarkedName = studentName;
      lastMarkedTime = now;
    } else {
      currentStatus = studentName + " already marked recently";
      Serial.println(currentStatus);
      digitalWrite(FLASH_LED, LOW);
    }
  } else {
    currentStatus = "Face not recognized";
    Serial.println(currentStatus);
    digitalWrite(FLASH_LED, LOW);
  }

  delay(5000);
}