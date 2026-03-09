#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "img_converters.h"

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Connect to VIPER as WiFi Client
// ===========================
const char *ssid = "VIPER";
const char *password = "12345678";

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

  // ===== STATIC IP: cố định IP = 192.168.4.2 (tránh DHCP cấp IP ngẫu nhiên) =====
  WiFi.config(
    IPAddress(192, 168, 4, 2),   // IP tĩnh cho ESP32-CAM
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
  
  apiServer.begin();
  Serial.println("[API] Color detection API ready at /detect_color");

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
  Serial.print("API: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/detect_color");
  Serial.println("===========================\n");
}

// ================= COLOR DETECTION =================
String lastSentColor = "none";
unsigned long lastDetectionTime = 0;
const unsigned long DETECTION_INTERVAL = 5000; // 5 giây để streaming ổn định hơn

// DETECT ON-DEMAND: per-pixel HSV classification
// Mỗi pixel được phân loại riêng → đếm tỉ lệ pixel đỏ/xanh trong ROI
// Approach này chính xác ngay cả khi vật cản chỉ chiếm một phần khung hình
String detectColorNow() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return "none";

  framesize_t old_size = s->status.framesize;
  s->set_framesize(s, FRAMESIZE_QQVGA);  // 160x120, stride=480 (4-byte aligned, no padding)
  delay(60);

  // Xả frame cũ (có thể là QVGA từ streaming)
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    s->set_framesize(s, old_size);
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
  s->set_framesize(s, old_size);

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

  int redPx = 0, greenPx = 0, totalPx = 0;

  for (int y = y0; y < y1; y += 2) {
    for (int x = x0; x < x1; x += 2) {
      int idx = (y * fw + x) * 3;
      if (idx + 2 >= (int)pixLen) continue;

      // BMP = B G R
      float b = pix[idx]     / 255.0f;
      float g = pix[idx + 1] / 255.0f;
      float r = pix[idx + 2] / 255.0f;

      float vmax  = max(r, max(g, b));
      float vmin  = min(r, min(g, b));
      float delta = vmax - vmin;

      // Bỏ qua pixel quá tối (<15%) hoặc không bão hoà (<12%)
      if (vmax < 0.15f || delta < 0.12f) continue;

      float sat = delta / vmax;
      if (sat < 0.22f) continue;   // xám / trắng / nền → bỏ

      // Tính Hue
      float hue = 0;
      if      (vmax == r) hue = 60.0f * fmodf((g - b) / delta, 6.0f);
      else if (vmax == g) hue = 60.0f * ((b - r) / delta + 2.0f);
      else                hue = 60.0f * ((r - g) / delta + 4.0f);
      if (hue < 0) hue += 360.0f;

      totalPx++;

      // ĐỎ: H ∈ [0,28] ∪ [332,360]
      if (hue <= 28.0f || hue >= 332.0f)           redPx++;
      // XANH LÁ: H ∈ [80,170]
      else if (hue >= 80.0f && hue <= 170.0f) greenPx++;
    }
  }

  free(rgb_buf);

  if (totalPx < 6) {
    Serial.println("[DETECT] Too few saturated pixels (check lighting)");
    return "none";
  }

  float redRatio   = (float)redPx   / totalPx;
  float greenRatio = (float)greenPx / totalPx;

  Serial.printf("[DETECT] saturated=%d  red=%d(%.0f%%)  green=%d(%.0f%%)\n",
                totalPx, redPx, redRatio * 100, greenPx, greenRatio * 100);

  // Cần ≥18% pixel có màu để kết luận (vật cản chỉ cần chiếm ~1/5 ROI)
  const float TH = 0.18f;
  if (redRatio   >= TH && redRatio   > greenRatio) { Serial.println("[DETECT] → RED");   return "red";   }
  if (greenRatio >= TH && greenRatio > redRatio)   { Serial.println("[DETECT] → GREEN"); return "green"; }

  Serial.printf("[DETECT] → none (red=%.0f%% green=%.0f%% < %.0f%% threshold)\n",
                redRatio*100, greenRatio*100, TH*100);
  return "none";
}

// ================= MULTI-FRAME VOTING for Reliability =================
String detectColorWithVoting() {
  const int NUM_SAMPLES = 3;  // 3 frames để vote chính xác hơn
  int counts[4] = {0, 0, 0, 0};  // [red, green, yellow, none]

  Serial.println("\n[VOTING] 3-frame color detection...");

  for (int i = 0; i < NUM_SAMPLES; i++) {
    String color = detectColorNow();
    if      (color == "red")    counts[0]++;
    else if (color == "green")  counts[1]++;
    else if (color == "yellow") counts[2]++;
    else                        counts[3]++;

    if (i < NUM_SAMPLES - 1) delay(80);  // Chờ frame mới
  }

  Serial.printf("[VOTING] red=%d green=%d yellow=%d none=%d\n",
                counts[0], counts[1], counts[2], counts[3]);

  // Cần ít nhất 2/3 đồng ý
  if (counts[0] >= 2) return "red";
  if (counts[1] >= 2) return "green";
  if (counts[2] >= 2) return "yellow";

  // Chấp nhận 1/3 nếu không có màu nào khác cạnh tranh
  if (counts[0] == 1 && counts[1] == 0 && counts[2] == 0) return "red";
  if (counts[1] == 1 && counts[0] == 0 && counts[2] == 0) return "green";

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