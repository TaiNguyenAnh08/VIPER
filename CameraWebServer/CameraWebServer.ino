#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "img_converters.h"

// ===========================
// VIPER Phase 2: BRIGHTNESS-BASED Obstacle Detection
// ===========================
// Logic: Phát hiện vật cản dựa trên độ sáng (brightness) thay vì màu sắc
// - WHITE (trắng/sáng): brightness > 0.65, R≈G≈B → Rẽ PHẢI
// - DARK (đen/tối): brightness < 0.35 → Rẽ TRÁI
// - NONE: Không nhận diện được → Rẽ TRÁI (mặc định)
//
// Ưu điểm so với HSV color detection:
// ✓ Đơn giản hơn - không cần tính Hue phức tạp
// ✓ Nhanh hơn - chỉ cần tính trung bình RGB
// ✓ Tin cậy hơn - ít bị ảnh hưởng bởi lighting
// ✓ Dễ test - dùng giấy trắng/đen hoặc vật sáng/tối bất kỳ
// ===========================

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Connect to VIPER as WiFi Client
// ===========================
const char *ssid = "VIPER";
const char *password = "12345678";

// TensorFlow server IP (Python server on PC/Laptop)
// Change this to your PC's IP when connected to VIPER WiFi
#define TF_SERVER_IP "192.168.4.2"
#define TF_SERVER_PORT 5000

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
  config.jpeg_quality = 12;  // 12 = chất lượng tốt hơn cho color detection (cân bằng giữa quality và speed)
  config.fb_count = 2;  // Double buffering cho speed

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;  // 10 = chất lượng tốt cho màu sắc rõ nét
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
  // Laptop Python server = 192.168.4.2, ESP32-CAM = 192.168.4.3
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
  
  // API endpoint: /detect_color - Gọi khi cần detect màu
  apiServer.on("/detect_color", HTTP_GET, []() {
    Serial.println("\n[API] /detect_color called by VIPER");
    String color = detectColorWithVoting();  // Dùng voting thay vì single detection
    String json = "{\"color\":\"" + color + "\"}";
    apiServer.send(200, "application/json", json);
    Serial.printf("[API] Responded: %s\n", json.c_str());
  });
  
  // API endpoint: /detect_shape - TensorFlow shape detection
  apiServer.on("/detect_shape", HTTP_GET, []() {
    Serial.println("\n[API] /detect_shape called by VIPER");
    String shape = detectShapeWithTensorFlow();
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
  Serial.println("[API] Color detection API ready at /detect_color");
  Serial.println("[API] TensorFlow shape API ready at /detect_shape");
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
  Serial.print("API 1 (brightness): http://");
  Serial.print(WiFi.localIP());
  Serial.println("/detect_color");
  Serial.print("API 2 (TensorFlow): http://");
  Serial.print(WiFi.localIP());
  Serial.println("/detect_shape");
  Serial.print("TF Server: http://");
  Serial.print(TF_SERVER_IP);
  Serial.print(":");
  Serial.println(TF_SERVER_PORT);
  Serial.println("===========================\n");
}

// ================= COLOR DETECTION =================
String lastSentColor = "none";
unsigned long lastDetectionTime = 0;
const unsigned long DETECTION_INTERVAL = 5000; // 5 giây để streaming ổn định hơn

// DETECT ON-DEMAND: per-pixel brightness classification
// Mỗi pixel được phân loại riêng → đếm tỉ lệ pixel trắng/tối trong ROI
// Giữ nguyên QVGA (320x240) để camera không nhảy kích thước khi detect
// → Chậm hơn QQVGA nhưng NÉT HƠN và không bị resize
String detectColorNow() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return "none";

  // Xả frame cũ
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[DETECT] Frame capture failed");
    return "none";
  }

  Serial.printf("[DETECT] Frame %dx%d fmt=%d len=%d\n",
                fb->width, fb->height, fb->format, fb->len);

  int fw = fb->width;
  int fh = fb->height;

  // Decode JPEG → BMP (RGB888, BGR byte order)
  uint8_t *rgb_buf = NULL;
  size_t   rgb_len = 0;
  bool ok = frame2bmp(fb, &rgb_buf, &rgb_len);
  esp_camera_fb_return(fb);

  if (!ok || !rgb_buf) {
    Serial.println("[DETECT] frame2bmp failed");
    return "none";
  }

  // BMP header = 54 bytes; pixel data = BGR, bottom-to-top rows
  // Stride = fw*3 = 480 bytes (divisible by 4 → no row padding)
  uint8_t *pix    = rgb_buf + 54;
  size_t   pixLen = rgb_len - 54;

  // ROI: centre 60% of frame (loại bỏ viền ngoài nhiễu nền)
  int x0 = fw / 5, x1 = fw * 4 / 5;
  int y0 = fh / 5, y1 = fh * 4 / 5;

  int whitePx = 0, darkPx = 0, totalPx = 0;

  for (int y = y0; y < y1; y += 2) {
    for (int x = x0; x < x1; x += 2) {
      int idx = (y * fw + x) * 3;
      if (idx + 2 >= (int)pixLen) continue;

      // BMP = B G R
      float b = pix[idx]     / 255.0f;
      float g = pix[idx + 1] / 255.0f;
      float r = pix[idx + 2] / 255.0f;

      // Tính brightness (giá trị trung bình RGB)
      float brightness = (r + g + b) / 3.0f;
      
      totalPx++;

      // TRẮNG: brightness cao (>0.50) và không quá màu (R≈G≈B)
      // Giảm ngưỡng từ 0.65 xuống 0.50 cho camera chất lượng thấp
      float maxRGB = max(r, max(g, b));
      float minRGB = min(r, min(g, b));
      float delta = maxRGB - minRGB;
      
      if (brightness > 0.50f && delta < 0.30f) {
        // Sáng và gần như xám → TRẮNG
        whitePx++;
      }
      // ĐEN/TỐI: brightness thấp (<0.45)
      // Tăng ngưỡng từ 0.35 lên 0.45 để dễ detect hơn
      else if (brightness < 0.45f) {
        darkPx++;
      }
    }
  }

  free(rgb_buf);

  if (totalPx < 10) {
    Serial.println("[DETECT] Too few pixels sampled");
    return "none";
  }

  float whiteRatio = (float)whitePx / totalPx;
  float darkRatio  = (float)darkPx  / totalPx;

  Serial.printf("[DETECT] total=%d  white=%d(%.0f%%)  dark=%d(%.0f%%)\n",
                totalPx, whitePx, whiteRatio * 100, darkPx, darkRatio * 100);

  // Giảm threshold từ 20% xuống 12% để dễ detect với camera chất lượng thấp
  const float TH = 0.12f;
  if (whiteRatio >= TH && whiteRatio > darkRatio) { 
    Serial.println("[DETECT] → WHITE (sáng)"); 
    return "white"; 
  }
  if (darkRatio >= TH && darkRatio > whiteRatio) { 
    Serial.println("[DETECT] → DARK (tối)"); 
    return "dark"; 
  }

  Serial.printf("[DETECT] → none (white=%.0f%% dark=%.0f%% < %.0f%% threshold)\n",
                whiteRatio*100, darkRatio*100, TH*100);
  return "none";
}

// ================= MULTI-FRAME VOTING for Reliability =================
String detectColorWithVoting() {
  const int NUM_SAMPLES = 3;  // 3 frames để vote chính xác hơn
  int counts[3] = {0, 0, 0};  // [white, dark, none]

  Serial.println("\n[VOTING] 3-frame brightness detection...");

  for (int i = 0; i < NUM_SAMPLES; i++) {
    String color = detectColorNow();
    if      (color == "white") counts[0]++;
    else if (color == "dark")  counts[1]++;
    else                       counts[2]++;

    if (i < NUM_SAMPLES - 1) delay(80);  // Chờ frame mới
  }

  Serial.printf("[VOTING] white=%d dark=%d none=%d\n",
                counts[0], counts[1], counts[2]);

  // Cần ít nhất 2/3 đồng ý
  if (counts[0] >= 2) return "white";
  if (counts[1] >= 2) return "dark";

  // Chấp nhận 1/3 nếu không có kết quả khác cạnh tranh
  if (counts[0] == 1 && counts[1] == 0) return "white";
  if (counts[1] == 1 && counts[0] == 0) return "dark";

  return "none";
}

// ================= TENSORFLOW SHAPE DETECTION =================
// Captures JPEG image and POSTs to Python TensorFlow server
// Returns: "left", "right", or "none"
String detectShapeWithTensorFlow() {
  Serial.println("\n[TF] Capturing image for shape detection...");
  
  // Flush old frame
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);
  
  // Capture fresh JPEG frame
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[TF] ❌ Frame capture failed");
    return "none";
  }
  
  Serial.printf("[TF] Captured %dx%d JPEG, %d bytes\n", 
                fb->width, fb->height, fb->len);
  
  // POST to Python server
  HTTPClient http;
  String url = String("http://") + TF_SERVER_IP + ":" + String(TF_SERVER_PORT) + "/predict";
  
  Serial.printf("[TF] POSTing to %s\n", url.c_str());
  
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(5000);  // 5s timeout
  
  int httpCode = http.POST(fb->buf, fb->len);
  
  esp_camera_fb_return(fb);  // Release frame buffer immediately
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.printf("[TF] Response: %s\n", payload.c_str());
    
    // Parse JSON: {"shape":"left","confidence":180}
    // Simple parsing without ArduinoJson to save memory
    int shapeIdx = payload.indexOf("\"shape\":\"");
    if (shapeIdx >= 0) {
      shapeIdx += 9;  // Skip to value
      int endIdx = payload.indexOf("\"", shapeIdx);
      if (endIdx > shapeIdx) {
        String shape = payload.substring(shapeIdx, endIdx);
        Serial.printf("[TF] ✅ Detected shape: %s\n", shape.c_str());
        http.end();
        return shape;
      }
    }
    
    Serial.println("[TF] ⚠️  Failed to parse response");
  } else {
    Serial.printf("[TF] ❌ HTTP error: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.printf("[TF] Response: %s\n", http.getString().c_str());
    }
  }
  
  http.end();
  return "none";
}

// Hàm cũ (không dùng nữa, giữ để tham khảo)
String detectDominantColor() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Frame capture failed - camera busy");
    return "none";
  }

  long sum_high = 0;
  long sum_mid = 0;
  long sum_low = 0;
  int samples = 0;
  
  int start = fb->len / 3;
  int end = (fb->len * 2) / 3;
  int step = (end - start) / 300;
  if (step < 1) step = 1;
  
  for (int i = start; i < end; i += step) {
    uint8_t val = fb->buf[i];
    if (val > 180) sum_high++;
    else if (val > 100) sum_mid++;
    else sum_low++;
    samples++;
  }

  esp_camera_fb_return(fb);

  if (samples == 0) return "none";

  float ratio_high = (float)sum_high / samples;
  float ratio_mid = (float)sum_mid / samples;
  
  if (ratio_high > 0.35 && ratio_high > ratio_mid * 1.2) {
    return "red";
  }
  else if (ratio_mid > 0.40 && ratio_mid > ratio_high * 1.2) {
    return "green";
  }
  else if (ratio_high > 0.25 && ratio_mid > 0.30) {
    return "yellow";
  }
  
  return "none";
}

void sendColorToVIPER(String color) {
  HTTPClient http;
  http.begin("http://192.168.4.1/api/color");
  http.addHeader("Content-Type", "application/json");
  
  String json = "{\"color\":\"" + color + "\"}";
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    Serial.print("[VIPER] Color sent: ");
    Serial.println(color);
  } else {
    Serial.print("[VIPER] POST failed: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

void loop() {
  // Xử lý API requests (WiFi HTTP)
  apiServer.handleClient();
  
  // ================= WiFi HTTP Mode Only =================
  // Camera sẽ nhận request từ VIPER qua HTTP API: /detect_color
  // UART đã TẮT - Không cần nối dây TX/RX
  
  delay(10);
}