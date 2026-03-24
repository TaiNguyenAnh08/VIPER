#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "img_converters.h"

// ===========================
// VIPER Phase 2: SHAPE-BASED Obstacle Detection
// ===========================
// Logic: Phát hiện vật cản dựa trên hình dạng (OpenCV)
// - CIRCLE (tròn): OpenCV HoughCircles → Rẽ PHẢI
// - SQUARE (vuông): OpenCV Contour analysis → Rẽ TRÁI
// - NONE: Không nhận diện được → Rẽ TRÁI (mặc định)
//
// Ưu điểm:
// ✓ Chính xác cao - không bị ảnh hưởng bởi lighting
// ✓ Rõ ràng - phân biệt dạng hình học thực tế
// ✓ Không cần training - dùng OpenCV image processing
// ✓ Vật cản: hình vuông/tròn đen trên nền trắng (8cm)
// =============================

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Connect to VIPER as WiFi Client
// ===========================
const char *ssid = "VIPER";
const char *password = "12345678";

// OpenCV server IP (Python server on PC/Laptop)
// Change this to your PC's IP when connected to VIPER WiFi
#define OPENCV_SERVER_IP "192.168.4.4"
#define OPENCV_SERVER_PORT 5000

// Web server cho API endpoint
WebServer apiServer(80);

void startCameraServer();
void setupLedFlash();

void setup() {
  // ========== DISABLE BROWNOUT DETECTOR (tránh bootloop khi nguồn yếu) ==========
  #include "soc/soc.h"
  #include "soc/rtc_cntl_reg.h"
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("\n=== ESP32-CAM Starting ===");
  Serial.println("Connecting to VIPER AP...");

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
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_LATEST;  // Luôn lấy frame mới nhất
  config.fb_location = CAMERA_FB_IN_PSRAM;
  // Tăng nén JPEG một chút để giảm dung lượng frame, giúp realtime mượt hơn
  config.jpeg_quality = 20;  // 20: nhẹ hơn 15 nhưng vẫn đủ nét
  config.fb_count = 2;  // Double buffering cho speed

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      // Đồng bộ với cấu hình trên để đảm bảo kích thước frame ổn định
      config.jpeg_quality = 20;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 2);   // TĂNG brightness lên 2 (từ 1) cho hình sáng hơn
    s->set_saturation(s, 0);   // GIỮ saturation mặc định (từ -2) cho màu sắc chuẩn
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);  // 320x240 (từ QQVGA 160x120) → cân bằng quality/speed
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  // ===== STATIC IP: cố định IP = 192.168.4.3 (tránh DHCP cấp IP ngẫu nhiên) =====
  // Laptop Python server = 192.168.4.7, ESP32-CAM = 192.168.4.3 (thay đổi tùy máy)
  WiFi.config(
    IPAddress(192, 168, 4, 3),   // IP tĩnh cho ESP32-CAM
    IPAddress(192, 168, 4, 1),   // Gateway = VIPER AP
    IPAddress(255, 255, 255, 0)  // Subnet mask
  );
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[ERROR] Failed to connect to VIPER!");
    Serial.println("Check: 1) VIPER main board is powered on, 2) GND connected");
    return;
  }
  
  Serial.println("\n[OK] WiFi connected to VIPER!");
  Serial.print("VIPER-CAM IP: ");
  Serial.println(WiFi.localIP());

  // ===================== mDNS: esp32cam.local =====================
  if (!MDNS.begin("esp32cam")) {
    Serial.println("[mDNS] Failed to start mDNS responder");
  } else {
    Serial.println("[mDNS] mDNS responder started: http://esp32cam.local");
    MDNS.addService("http", "tcp", 80);   // API server
    MDNS.addService("http", "tcp", 81);   // Stream server
  }
  
  // API endpoint: /detect_shape - OpenCV shape detection
  apiServer.on("/detect_shape", HTTP_GET, []() {
    Serial.println("\n[API] /detect_shape called by VIPER");
    String shape = detectShapeWithOpenCV();  // OpenCV backend
    String json = "{\"shape\":\"" + shape + "\"}";
    apiServer.send(200, "application/json", json);
    Serial.printf("[API] Responded: %s\n", json.c_str());
  });
  
  // API endpoint: /capture - Get raw JPEG frame for training image capture
  apiServer.on("/capture", HTTP_GET, []() {
    Serial.println("\n[API] /capture called - returning JPEG frame");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      apiServer.send(500, "text/plain", "Camera capture failed");
      return;
    }
    apiServer.sendHeader("Content-Type", "image/jpeg");
    apiServer.sendHeader("Content-Length", String(fb->len));
    apiServer.send(200);
    apiServer.sendContent((const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    Serial.printf("[API] /capture responded with %d bytes\n", fb->len);
  });
  
  apiServer.begin();
  Serial.println("[API] OpenCV shape API ready at /detect_shape");
  Serial.println("[API] JPEG capture API ready at /capture");

  // ================= START CAMERA STREAMING SERVER (Port 81) =================
  startCameraServer();
  Serial.println("[STREAM] Camera streaming server started on port 81");

  Serial.println("\n=== Camera Server Ready ===");
  Serial.print("Stream URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
  Serial.print("Control URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
  Serial.print("API (OpenCV shape): http://");
  Serial.print(WiFi.localIP());
  Serial.println("/detect_shape");
  Serial.print("Server: http://");
  Serial.print(OPENCV_SERVER_IP);
  Serial.print(":");
  Serial.println(OPENCV_SERVER_PORT);
  Serial.println("===========================\n");
}

// ================= OPENCV SHAPE DETECTION =================
// Captures JPEG image and POSTs to Python OpenCV server
// Returns: "circle", "square", or "none"
String detectShapeWithOpenCV() {
  Serial.println("\n[CV] Capturing image for shape detection...");
  
  // Flush old frame
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);
  
  // Capture fresh JPEG frame
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CV] ❌ Frame capture failed");
    return "none";
  }
  
  Serial.printf("[CV] Captured %dx%d JPEG, %d bytes\n", 
                fb->width, fb->height, fb->len);
  
  // POST to Python server
  HTTPClient http;
  String url = String("http://") + OPENCV_SERVER_IP + ":" + String(OPENCV_SERVER_PORT) + "/predict";
  
  Serial.printf("[CV] POSTing to %s\n", url.c_str());
  
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(5000);  // 5s timeout
  
  int httpCode = http.POST(fb->buf, fb->len);
  
  esp_camera_fb_return(fb);  // Release frame buffer immediately
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.printf("[CV] Response: %s\n", payload.c_str());
    
    // Parse JSON: {"shape":"left","confidence":180}
    // Simple parsing without ArduinoJson to save memory
    int shapeIdx = payload.indexOf("\"shape\":\"");
    if (shapeIdx >= 0) {
      shapeIdx += 9;  // Skip to value
      int endIdx = payload.indexOf("\"", shapeIdx);
      if (endIdx > shapeIdx) {
        String shape = payload.substring(shapeIdx, endIdx);
        Serial.printf("[CV] ✅ Detected shape: %s\n", shape.c_str());
        http.end();
        return shape;
      }
    }
    
    Serial.println("[CV] ⚠️  Failed to parse response");
  } else {
    Serial.printf("[CV] ❌ HTTP error: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.printf("[CV] Response: %s\n", http.getString().c_str());
    }
  }
  
  http.end();
  return "none";
}

void loop() {
  // Xử lý API requests (WiFi HTTP)
  apiServer.handleClient();
  
  // ================= WiFi HTTP Mode Only =================
  // Camera sẽ nhận request từ VIPER qua HTTP API: /detect_shape
  // UART đã TẮT - Không cần nối dây TX/RX
  
  delay(10);
}