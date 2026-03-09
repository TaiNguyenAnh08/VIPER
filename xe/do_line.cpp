#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "do_line.h"

// ================= External from xe.ino =================
extern String lastDetectedColor;

/* ================= ESP32 30P + L298N + analogWrite =================
   Mapping:
   - Right motor: IN1=12, IN2=14, ENA=13
   - Left  motor: IN3=4,  IN4=2,  ENB=15
   - Line sensors: L2=34, L1=32, M=33, R1=25, R2=27
   - Encoders: ENC_L=26, ENC_R=22
   - HC-SR04: TRIG=21, ECHO=19
==================================================================== */

// ================= Motor pins (ESP32 + L298N) =================
#define IN1 12
#define IN2 14
#define ENA 13
#define IN3 4
#define IN4 2
#define ENB 15

// ================= Line sensors =================
#define L2_SENSOR   34   // outer-left
#define L1_SENSOR   32   // left
#define M_SENSOR    33   // middle
#define R1_SENSOR   25   // right
#define R2_SENSOR   27   // outer-right
// TCRT: LOW khi trên vạch đen

// ================= Encoders =================
#define ENC_L 26
#define ENC_R 22

#define PULSES_PER_REV   20
#define PPR_EFFECTIVE    (PULSES_PER_REV * 2) // đếm CHANGE → 2 sườn

// Lọc nhiễu xung trong ISR: bỏ xung < MIN_EDGE_US
#define MIN_EDGE_US 300

// ================= HC-SR04 =================
#define TRIG_PIN 21
#define ECHO_PIN 19
const float OBSTACLE_TH_CM   = 15.0f;    // cm
const unsigned long US_TIMEOUT = 30000;  // 30 ms (~5m)

// ==== Biến cho SR04 non-blocking ====
enum USState { US_IDLE, US_WAIT_HIGH, US_WAIT_LOW };
USState us_state = US_IDLE;

unsigned long us_last_ms        = 0;         // lần cuối bắn trigger
const unsigned long US_PERIOD_MS = 80;       // chu kỳ đo ~80ms (nhanh hơn 3x)

unsigned long us_wait_start_us  = 0;         // bắt đầu chờ ECHO HIGH
unsigned long echo_start_us     = 0;         // thời điểm ECHO HIGH

float  ultrasonic_distance_cm   = -1.0f;     // kết quả đo gần nhất
bool   ultrasonic_new           = false;     // true khi có mẫu mới
float  ultrasonic_last_valid    = 999.0f;    // giá trị hợp lệ gần nhất (khởi tạo xa)

// ================= Màu vật cản detected =================
String last_detected_color = "none";         // Màu vật cản phát hiện gần nhất
float obstacle_last_distance = 0.0f;         // Khoảng cách vật cản gần nhất (cm)
String obstacle_last_action = "none";        // Hành động: red→right, green→left, none→default
unsigned long obstacle_last_time = 0;        // Thời điểm phát hiện vật cản

// ================= Thông số cơ khí =================
const float WHEEL_RADIUS_M = 0.0325f;
const float CIRC           = 2.0f * 3.1415926f * WHEEL_RADIUS_M; // m/vòng
const float TRACK_WIDTH_M  = 0.0950f;

// ================= Tham số điều khiển =================
float v_base   = 0.5f;     // m/s cơ sở cho line-follow
const unsigned long CTRL_DT_MS = 10;  // chu kỳ PID

// Bù lệch 2 bánh (tune giống code PID đi thẳng)
const float L_SCALE = 1.00f;   // nếu xe lệch trái → tăng L_SCALE
const float R_SCALE = 1.00f;

// Lái bằng PWM (thêm/bớt sau PID)
const int STEER_PWM_SOFT = 4;   // lệch nhẹ
const int STEER_PWM_HARD = 7;   // lệch mạnh

// ================= PID cho từng bánh =================
struct PID {
  float Kp, Ki, Kd;
  float i_term;
  float prev_err;
  float out_min, out_max;
};

PID pidL{250.0f, 0.0f, 0.0f, 0, 0, 0, 255};
PID pidR{250.0f, 0.0f, 0.0f, 0, 0, 0, 255};

// ================= Biến encoder =================
volatile long encL_count = 0;
volatile long encR_count = 0;
volatile long encL_total = 0;
volatile long encR_total = 0;

volatile uint32_t encL_last_us = 0;
volatile uint32_t encR_last_us = 0;

// Shaper PWM: deadband + slew-rate
const int PWM_MIN_RUN = 50;  // 65–90
const int PWM_SLEW    = 50;  // bước tối đa mỗi chu kỳ
static int pwmL_prev  = 0;
static int pwmR_prev  = 0;

// Lưu hướng lần cuối thấy line
enum Side { NONE, LEFT, RIGHT };
Side last_seen = NONE;

// Đã từng thấy line chưa
bool seen_line_ever = false;

// ================= Recovery =================
bool recovering      = false;
unsigned long rec_t0 = 0;
const unsigned long RECOV_TIME_MS = 2000; // 2 s

// ================= Cờ enable =================
static volatile bool g_line_enabled = true;

// ================= Utils =================
inline int clamp255(int v){
  if (v < 0)   return 0;
  if (v > 255) return 255;
  return v;
}

inline float clampf(float v, float lo, float hi){
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// TCRT: LOW trên line
inline bool onLine(int pin){ return digitalRead(pin) == LOW; }

inline bool isValidLineSample5(bool L2,bool L1,bool M,bool R1,bool R2){
  bool allH =  L2 && L1 && M && R1 && R2;
  bool allL = !L2 && !L1 && !M && !R1 && !R2;
  return !(allH || allL);
}

// Shaper PWM: deadband + slew
static inline int shape_pwm(int target, int prev){
  int s = target;
  if (s > 0 && s < PWM_MIN_RUN)  s = PWM_MIN_RUN;
  if (s < 0 && s > -PWM_MIN_RUN) s = -PWM_MIN_RUN;
  int d = s - prev;
  if (d >  PWM_SLEW) s = prev + PWM_SLEW;
  if (d < -PWM_SLEW) s = prev - PWM_SLEW;
  return clamp255(s);
}

// Đổi ticks → vận tốc m/s
float ticksToVel(long ticks, float dt_s){
  float rev = (float)ticks / (float)PPR_EFFECTIVE;
  return (rev * CIRC) / dt_s;
}

// 1 bước PID
int pidStep(PID &pid, float v_target, float v_meas, float dt_s){
  float err = v_target - v_meas;
  pid.i_term += pid.Ki * err * dt_s;
  pid.i_term = clampf(pid.i_term, pid.out_min, pid.out_max);
  float d = (err - pid.prev_err) / dt_s;
  float u = pid.Kp * err + pid.i_term + pid.Kd * d;
  pid.prev_err = err;
  return clamp255((int)u);
}

/* ================= ISR encoder ================= */
void IRAM_ATTR encL_isr(){
  uint32_t now = micros();
  if (now - encL_last_us >= MIN_EDGE_US){
    encL_count++;
    encL_total++;
    encL_last_us = now;
  }
}

void IRAM_ATTR encR_isr(){
  uint32_t now = micros();
  if (now - encR_last_us >= MIN_EDGE_US){
    encR_count++;
    encR_total++;
    encR_last_us = now;
  }
}

/* ================= Motor control ================= */

// Left motor: IN1/IN2 = chiều, ENA = PWM
void driveWheelLeft(float v_cmd, int pwm){
  int d = clamp255(abs(pwm));
  if (v_cmd >= 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, d);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    analogWrite(ENA, d);
  }
}

// Right motor: IN3/IN4 = chiều, ENB = PWM
void driveWheelRight(float v_cmd, int pwm){
  int d = clamp255(abs(pwm));
  if (v_cmd >= 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    analogWrite(ENB, d);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    analogWrite(ENB, d);
  }
}

void motorsStop(){
  digitalWrite(IN1, HIGH); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, HIGH);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

/* ================= HC-SR04 NON-BLOCKING ================= */

// cập nhật state machine HC-SR04, không chặn
void ultrasonic_update() {
  unsigned long now_ms = millis();
  unsigned long now_us = micros();

  switch (us_state) {
    case US_IDLE:
      // tới chu kỳ thì bắn trigger
      if (now_ms - us_last_ms >= US_PERIOD_MS) {
        us_last_ms    = now_ms;
        ultrasonic_new = false;

        // Trigger 10 µs
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);

        us_wait_start_us = micros();
        echo_start_us    = 0;
        us_state         = US_WAIT_HIGH;
      }
      break;

    case US_WAIT_HIGH:
      if (digitalRead(ECHO_PIN) == HIGH) {
        echo_start_us = now_us;
        us_state      = US_WAIT_LOW;
      } else if (now_us - us_wait_start_us > US_TIMEOUT) {
        // không thấy cạnh lên
        ultrasonic_distance_cm = -1.0f;
        ultrasonic_new         = true;
        us_state               = US_IDLE;
      }
      break;

    case US_WAIT_LOW:
      if (digitalRead(ECHO_PIN) == LOW) {
        if (echo_start_us != 0) {
          unsigned long dur = now_us - echo_start_us;
          ultrasonic_distance_cm = (dur * 0.0343f) / 2.0f;
        } else {
          ultrasonic_distance_cm = -1.0f;
        }
        ultrasonic_new = true;
        us_state       = US_IDLE;
      } else if (now_us - echo_start_us > US_TIMEOUT) {
        // echo quá dài / lỗi
        ultrasonic_distance_cm = -1.0f;
        ultrasonic_new         = true;
        us_state               = US_IDLE;
      }
      break;
  }
}

// API: trả về giá trị mới nếu có, ngược lại trả giá trị gần nhất
// FIX: KHÔNG BAO GIỜ trả về -1 (gây false positive obstacle detection)
float readDistanceCM_nonblock() {
  ultrasonic_update();
  if (ultrasonic_new) {
    ultrasonic_new = false;
    if (ultrasonic_distance_cm > 0 && ultrasonic_distance_cm < 400) {
      ultrasonic_last_valid = ultrasonic_distance_cm;  // Lưu giá trị hợp lệ (0-400cm)
    }
    // KHÔNG trả về giá trị lỗi (-1 hoặc >400cm)
  }
  // Luôn trả về giá trị hợp lệ gần nhất (khởi tạo 999.0f = xa = an toàn)
  return ultrasonic_last_valid;
}

/* ================= Hình học encoder / quay / tiến ================= */

long countsForDistance(double dist_m){
  double rot = dist_m / CIRC;
  return (long)(rot * PPR_EFFECTIVE + 0.5);
}
// SỐ XUNG CẦN CHO MỖI BÁNH KHI QUAY "deg" ĐỘ
long countsForSpinDeg(double deg){
  double theta = deg * 3.141592653589793 / 180.0;       // rad
  double arc   = (TRACK_WIDTH_M * 0.5) * theta;         // quãng đường mỗi bánh
  double rot   = arc / CIRC;                            // số vòng bánh
  return (long)(rot * PPR_EFFECTIVE + 0.5);             // đổi sang xung
}
inline void motorWriteLR_signed(int pwmL, int pwmR){
  pwmL = pwmL < -255 ? -255 : (pwmL > 255 ? 255 : pwmL);
  pwmR = pwmR < -255 ? -255 : (pwmR > 255 ? 255 : pwmR);
  // Right
  if (pwmR >= 0){ digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  analogWrite(ENB, pwmR); }
  else           { digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); analogWrite(ENB, -pwmR); }
  // Left
  if (pwmL >= 0){ digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  analogWrite(ENA, pwmL); }
  else           { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); analogWrite(ENA, -pwmL); }
}

static inline double theta_from_counts(long dL, long dR, int signL, int signR){
  double sL = (double)dL / PPR_EFFECTIVE * CIRC * signL;
  double sR = (double)dR / PPR_EFFECTIVE * CIRC * signR;
  return (sR - sL) / TRACK_WIDTH_M;
}

void spin_left_deg(double deg, int pwmAbs){
  long target = countsForSpinDeg(deg);
  long L0, R0;
  noInterrupts();
  L0 = encL_total;
  R0 = encR_total;
  interrupts();

  const unsigned long T_FAIL_MS = 4000;   // watchdog 4s
  unsigned long t0 = millis();

  while (true){
    // EMERGENCY: Check abort flag EVERY LOOP!
    if (!g_line_enabled) {
      motorsStop();
      return;
    }
    
    long L, R;
    noInterrupts();
    L = encL_total;
    R = encR_total;
    interrupts();

    long dL = labs(L - L0);
    long dR = labs(R - R0);

    // đủ xung cho cả hai bánh → dừng
    if (dL >= target && dR >= target) break;

    // an toàn: quá thời gian thì dừng, tránh quay mãi
    if (millis() - t0 > T_FAIL_MS) break;

    // quay trái: bánh trái lùi, bánh phải tiến
    motorWriteLR_signed(-pwmAbs, +pwmAbs);
    delay(1);
  }
  motorsStop();
}

void spin_right_deg(double deg, int pwmAbs){
  long target = countsForSpinDeg(deg);
  long L0, R0;
  noInterrupts();
  L0 = encL_total;
  R0 = encR_total;
  interrupts();

  const unsigned long T_FAIL_MS = 4000;   // watchdog 4s
  unsigned long t0 = millis();

  while (true){
    // EMERGENCY: Check abort flag EVERY LOOP!
    if (!g_line_enabled) {
      motorsStop();
      return;
    }
    
    long L, R;
    noInterrupts();
    L = encL_total;
    R = encR_total;
    interrupts();

    long dL = labs(L - L0);
    long dR = labs(R - R0);

    if (dL >= target && dR >= target) break;
    if (millis() - t0 > T_FAIL_MS)   break;

    // quay phải: bánh trái tiến, bánh phải lùi
    motorWriteLR_signed(+pwmAbs, -pwmAbs);
    delay(1);
  }
  motorsStop();
}

void move_forward_distance(double dist_m, int pwmAbs){
  long target = countsForDistance(dist_m);
  long sL, sR; noInterrupts(); sL = encL_total; sR = encR_total; interrupts();
  
  const unsigned long T_FAIL_MS = 10000; // Timeout 10s
  unsigned long t0 = millis();
  
  motorWriteLR_signed(+pwmAbs, +pwmAbs);
  while (true){
    // EMERGENCY: Check abort flag
    if (!g_line_enabled){ motorsStop(); return; }
    
    // Timeout safety
    if (millis() - t0 > T_FAIL_MS) {
      Serial.println("[SAFETY] move_forward timeout!");
      break;
    }
    
    long cL, cR; noInterrupts(); cL = encL_total; cR = encR_total; interrupts();
    bool left_done  = (labs(cL - sL) >= target);
    bool right_done = (labs(cR - sR) >= target);
    if (left_done && right_done) break;
    if (left_done && !right_done)      motorWriteLR_signed(0, +pwmAbs);
    else if (!left_done && right_done) motorWriteLR_signed(+pwmAbs, 0);
    delay(1);
  }
  motorsStop();
}

bool move_forward_distance_until_line(double dist_m, int pwmAbs){
  long target = countsForDistance(dist_m);
  long sL, sR; noInterrupts(); sL = encL_total; sR = encR_total; interrupts();
  motorWriteLR_signed(+pwmAbs, +pwmAbs);
  while (true){
    if (!g_line_enabled){ motorsStop(); return false; }
    long cL, cR; noInterrupts(); cL = encL_total; cR = encR_total; interrupts();
    // Check bất kỳ sensor nào thấy line
    if (onLine(L2_SENSOR)||onLine(L1_SENSOR)||onLine(M_SENSOR)||onLine(R1_SENSOR)||onLine(R2_SENSOR)){
      motorsStop(); return true;
    }
    bool left_done  = (labs(cL - sL) >= target);
    bool right_done = (labs(cR - sR) >= target);
    if (left_done && right_done) break;
    if (left_done && !right_done)      motorWriteLR_signed(0, +pwmAbs);
    else if (!left_done && right_done) motorWriteLR_signed(+pwmAbs, 0);
    delay(1);
  }
  motorsStop();
  return false;
}

// ================= TÌM VÀ CANH CHỈNH LINE SAU TRÁNH VẬT CẢN =================
// Tìm line từ từ, kiểm tra hướng, canh chỉnh góc nếu cần
// Return true nếu tìm thấy line với hướng an toàn
bool findAndAlignLine(double dist_m, int pwmAbs, const char* direction_name) {
  Serial.print("  [Line Search] ");
  Serial.print(direction_name);
  Serial.println(" searching...");
  
  long target = countsForDistance(dist_m);
  long sL, sR;
  noInterrupts();
  sL = encL_total;
  sR = encR_total;
  interrupts();

  motorWriteLR_signed(+pwmAbs, +pwmAbs);
  
  const unsigned long SEARCH_TIMEOUT_MS = 5000; // 5s timeout
  unsigned long search_start = millis();

  while (true) {
    // EMERGENCY: Check abort flag!
    if (!g_line_enabled) {
      Serial.println("  [Line Search] ABORTED by mode switch!");
      motorsStop();
      return false;
    }

    // Kiểm tra timeout
    if (millis() - search_start > SEARCH_TIMEOUT_MS) {
      Serial.println("  [Line Search] Timeout - line not found");
      motorsStop();
      return false;
    }

    // Đọc 5 cảm biến
    bool L2 = onLine(L2_SENSOR);
    bool L1 = onLine(L1_SENSOR);
    bool M  = onLine(M_SENSOR);
    bool R1 = onLine(R1_SENSOR);
    bool R2 = onLine(R2_SENSOR);

    // ===== TÌMTHẤY LINE =====
    if ((L2 || L1 || M || R1 || R2)) {
      motorsStop();
      int lineCount = (int)L2 + (int)L1 + (int)M + (int)R1 + (int)R2;

      // Trường hợp 1: Tất cả 5 cảm biến ON (5/5) → CÓ THỂ ĐI NGƯỢC
      if (lineCount == 5) {
        Serial.println("  [Line Search] ⚠️  All 5 sensors ON - possible reverse direction!");
        Serial.println("    Rotating 180° to correct direction");
        spin_left_deg(180.0, 100);
        motorsStop();
        delay(300);
        return true; // Đã quay 180°, sẵn sàng
      }

      // Trường hợp 2: Hình dáng line hợp lệ (không phải all-on/all-off)
      bool valid = isValidLineSample5(L2, L1, M, R1, R2);
      if (valid) {
        // Kiểm tra nếu lệch quá mạnh về một bên → canh chỉnh góc nhẹ
        bool heavy_left = (L2 && !R2 && !R1);
        bool heavy_right = (R2 && !L2 && !L1);

        if (heavy_left) {
          Serial.println("  [Line Search] Correcting heavy-left offset by ~10° right");
          spin_right_deg(10.0, 80);
          motorsStop();
          delay(200);
        } else if (heavy_right) {
          Serial.println("  [Line Search] Correcting heavy-right offset by ~10° left");
          spin_left_deg(10.0, 80);
          motorsStop();
          delay(200);
        }

        Serial.print("  [Line Search] ✓ Line found! Pattern: ");
        Serial.print(L2 ? "L2 " : "-- ");
        Serial.print(L1 ? "L1 " : "-- ");
        Serial.print(M ? "M " : "-- ");
        Serial.print(R1 ? "R1 " : "-- ");
        Serial.println(R2 ? "R2" : "--");
        return true;
      }

      // Trường hợp 3: Invalid (tất cả OFF) → tiếp tục tìm
    }

    // Kiểm tra khoảng cách
    long cL, cR;
    noInterrupts();
    cL = encL_total;
    cR = encR_total;
    interrupts();

    bool left_done  = (labs(cL - sL) >= target);
    bool right_done = (labs(cR - sR) >= target);

    if (left_done && right_done) {
      Serial.print("  [Line Search] Reached max distance (");
      Serial.print(dist_m);
      Serial.println("m) - line not found");
      motorsStop();
      return false;
    }

    // Bù tay khoảng cách cho 2 bánh
    if (left_done && !right_done) {
      motorWriteLR_signed(0, +pwmAbs);
    } else if (!left_done && right_done) {
      motorWriteLR_signed(+pwmAbs, 0);
    }

    delay(2); // Delay 2ms thay vì 1ms để tránh quá tải
  }
}

void avoidObstacle(){
  const int TURN_PWM = 120;
  const int FWD_PWM  = 130;
  const int SEARCH_PWM = 100; // PWM thấp hơn để tìm line từ từ

  Serial.println("\n>>> OBSTACLE AVOIDANCE STARTED <<<");

  Serial.println("  Step 1: Turn left 60°");
  spin_left_deg(60.0, TURN_PWM);
  motorsStop(); delay(300);

  Serial.println("  Step 2: Forward 0.2m");
  move_forward_distance(0.2, FWD_PWM);
  motorsStop(); delay(300);

  Serial.println("  Step 3: Turn right 60°");
  spin_right_deg(60.0, TURN_PWM);
  motorsStop(); delay(300);

  Serial.println("  Step 4: Forward 0.2m");
  move_forward_distance(0.2, FWD_PWM);
  motorsStop(); delay(300);

  Serial.println("  Step 5: Turn right 50°");
  spin_right_deg(50.0, TURN_PWM);
  motorsStop(); delay(300);

  // ===== TÌM LINE VỚI CANH CHỈNH THÔNG MINH =====
  Serial.println("  Step 6: Search for line (forward 1.2m) + auto-align");
  bool found = findAndAlignLine(1.2, SEARCH_PWM, "Forward");
  if (found) {
    Serial.println(">>> OBSTACLE AVOIDANCE SUCCESSFUL <<<\n");
    return;
  }

  // Không tìm thấy → quay lại hướng gốc + tìm lại
  Serial.println("  Step 7: Return to original direction + search again");
  spin_left_deg(50.0, TURN_PWM); // Quay lại 0°
  motorsStop(); delay(300);
  
  found = findAndAlignLine(1.0, SEARCH_PWM, "From original direction");
  if (found) {
    Serial.println(">>> OBSTACLE AVOIDANCE SUCCESSFUL <<<\n");
    return;
  }

  // Vẫn không tìm → quay 180° để tìm ở hướng ngược
  Serial.println("  Step 8: Turn 180° and search opposite direction");
  spin_left_deg(180.0, TURN_PWM);
  motorsStop(); delay(300);

  found = findAndAlignLine(1.5, SEARCH_PWM, "Opposite direction");
  if (found) {
    Serial.println(">>> OBSTACLE AVOIDANCE SUCCESSFUL <<<\n");
    return;
  }

  // Thất bại hoàn toàn
  Serial.println(">>> OBSTACLE AVOIDANCE FAILED <<<");
  Serial.println("  ✗ Could not find line after all attempts");
  Serial.println("  → Stopping and waiting for recovery\n");
  motorsStop();
  delay(1000);
}

// ================= DETECT COLOR FROM CAMERA (ON-DEMAND) =================
// Cache để tránh HTTP calls liên tục
unsigned long lastColorDetectTime = 0;
String cachedObstacleColor = "none";
const unsigned long COLOR_CACHE_DURATION = 3000; // Cache 3 giây

String detectColorFromCamera() {
  // Nếu vừa detect trong 3 giây qua → dùng cache
  if (millis() - lastColorDetectTime < COLOR_CACHE_DURATION && cachedObstacleColor != "none") {
    Serial.printf("[CAM] Using cached color: %s (age: %lu ms)\n", 
                  cachedObstacleColor.c_str(), 
                  millis() - lastColorDetectTime);
    return cachedObstacleColor;
  }
  
  // ================= WiFi HTTP Communication =================
  HTTPClient http;
  
  Serial.println("[CAM] Requesting color detection from camera...");
  http.begin("http://192.168.4.2/detect_color");
  http.setTimeout(2500); // Timeout 2.5s (đủ cho camera voting 3 frame + WiFi)
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.printf("[CAM] Response: %s\n", payload.c_str());
    
    // Parse JSON: {"color":"red"}
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      String color = doc["color"].as<String>();
      Serial.printf("[CAM] Detected color: %s\n", color.c_str());
      
      // Cache kết quả
      cachedObstacleColor = color;
      lastColorDetectTime = millis();
      
      http.end();
      return color;
    } else {
      Serial.println("[CAM] JSON parse error");
    }
  } else {
    Serial.printf("[CAM] HTTP error: %d\n", httpCode);
  }
  
  http.end();
  return "none";
}

// ================= SMART AVOIDANCE: Avoid RIGHT (for RED object) =================
// Hình học hình chữ U đúng:
//
//   LINE: ───────[VẬT CẢN]───────
//                ↑ xe dừng
//
//   [1] Quay PHẢI 90°
//   [2] Tiến SIDE_M sang ngang (thoát khỏi line)
//   [3] Quay TRÁI 90°  → song song với line, lệch SIDE_M về phải
//   [4] Tiến FWD_M (qua vật cản)
//   [5] Quay TRÁI 90°  → mũi quay về phía line gốc
//   [6] Tiến SIDE_M về line (dừng sớm nếu cảm biến thấy line)
//   [7] Quay PHẢI 90°  → lại hướng ban đầu, trên line ✅
void avoidObstacleRight(){
  const int TURN_PWM = 120;
  const int FWD_PWM  = 100;
  const double SIDE_M = 0.12;   // 12cm sang ngang (thoát khỏi line)
  const double FWD_M  = 0.15;   // 15cm về phía trước (qua vật cản nhỏ)

  Serial.println("\n>>> AVOIDANCE RIGHT (U-shape) <<<");
  if (!g_line_enabled) return;

  Serial.println("  [1] Spin RIGHT 90°");
  spin_right_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [2] Forward 22cm (sideways)");
  move_forward_distance(SIDE_M, FWD_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [3] Spin LEFT 90°");
  spin_left_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [4] Forward 30cm (past obstacle)");
  move_forward_distance(FWD_M, FWD_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [5] Spin LEFT 90°");
  spin_left_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [6] Move 14cm back toward line (stop on line)");
  bool hit = move_forward_distance_until_line(0.14, FWD_PWM);
  motorsStop(); delay(150);
  if (!g_line_enabled) return;
  Serial.printf("  [6] Line hit early: %s\n", hit ? "YES" : "NO");

  Serial.println("  [7] Spin RIGHT 90°");
  spin_right_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  if (!hit) {
    // Encoder drift nhỏ: tìm thêm tối đa 10cm
    Serial.println("  [8] Fine search 10cm");
    findAndAlignLine(0.10, 75, "Fine align");
  }

  Serial.println(">>> RIGHT AVOIDANCE DONE <<<\n");
}

// ================= SMART AVOIDANCE: Avoid LEFT (for GREEN object) =================
// Mirror của avoidObstacleRight() - quẹo TRÁI
void avoidObstacleLeft(){
  const int TURN_PWM = 120;
  const int FWD_PWM  = 100;
  const double SIDE_M = 0.12;
  const double FWD_M  = 0.15;

  Serial.println("\n>>> AVOIDANCE LEFT (U-shape) <<<");
  if (!g_line_enabled) return;

  Serial.println("  [1] Spin LEFT 90°");
  spin_left_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [2] Forward 22cm (sideways)");
  move_forward_distance(SIDE_M, FWD_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [3] Spin RIGHT 90°");
  spin_right_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [4] Forward 30cm (past obstacle)");
  move_forward_distance(FWD_M, FWD_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [5] Spin RIGHT 90°");
  spin_right_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  Serial.println("  [6] Move 14cm back toward line (stop on line)");
  bool hit = move_forward_distance_until_line(0.14, FWD_PWM);
  motorsStop(); delay(150);
  if (!g_line_enabled) return;
  Serial.printf("  [6] Line hit early: %s\n", hit ? "YES" : "NO");

  Serial.println("  [7] Spin LEFT 90°");
  spin_left_deg(90.0, TURN_PWM); motorsStop(); delay(150);
  if (!g_line_enabled) return;

  if (!hit) {
    Serial.println("  [8] Fine search 10cm");
    findAndAlignLine(0.10, 75, "Fine align");
  }

  Serial.println(">>> LEFT AVOIDANCE DONE <<<\n");
}

// API abort
void do_line_abort(){
  g_line_enabled = false;
  motorsStop();
}

// ================= GET LINE STATUS (for Web UI debug) =================
String getLineStatus() {
  // Đọc sensors
  bool L2 = onLine(L2_SENSOR);
  bool L1 = onLine(L1_SENSOR);
  bool M  = onLine(M_SENSOR);
  bool R1 = onLine(R1_SENSOR);
  bool R2 = onLine(R2_SENSOR);
  
  // Đọc ultrasonic
  float dist = readDistanceCM_nonblock();
  
  // JSON format
  String json = "{";
  json += "\"L2\":" + String(L2 ? 1 : 0) + ",";
  json += "\"L1\":" + String(L1 ? 1 : 0) + ",";
  json += "\"M\":" + String(M ? 1 : 0) + ",";
  json += "\"R1\":" + String(R1 ? 1 : 0) + ",";
  json += "\"R2\":" + String(R2 ? 1 : 0) + ",";
  json += "\"dist\":" + String(dist, 1) + ",";
  json += "\"recovery\":" + String(recovering ? 1 : 0) + ",";
  json += "\"seen\":" + String(seen_line_ever ? 1 : 0) + ",";
  json += "\"color\":\"" + last_detected_color + "\"";
  json += "}";
  
  return json;
}

// ================= GET OBSTACLE STATUS (for Web UI) =================
String getObstacleStatus() {
  String json = "{";
  json += "\"distance\":" + String(obstacle_last_distance, 1) + ",";
  json += "\"color\":\"" + last_detected_color + "\",";
  json += "\"action\":\"" + obstacle_last_action + "\",";
  json += "\"time\":" + String(obstacle_last_time);
  json += "}";
  return json;
}

// ================= Reset PID =================
inline void resetPID(PID &pid) {
  pid.i_term = 0.0f;
  pid.prev_err = 0.0f;
}

inline void resetBothPID() {
  resetPID(pidL);
  resetPID(pidR);
}

/* ================= Setup → do_line_setup ================= */
void do_line_setup() {
  // Motor DIR + PWM
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  // Sensors
  pinMode(L2_SENSOR, INPUT);
  pinMode(L1_SENSOR, INPUT);
  pinMode(M_SENSOR,  INPUT);
  pinMode(R1_SENSOR, INPUT);
  pinMode(R2_SENSOR, INPUT);

  // Encoders
  pinMode(ENC_L, INPUT_PULLUP);
  pinMode(ENC_R, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_L), encL_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_R), encR_isr, CHANGE);

  // Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  us_state               = US_IDLE;
  us_last_ms             = 0;
  ultrasonic_distance_cm = -1.0f;
  ultrasonic_new         = false;
  ultrasonic_last_valid  = 999.0f;  // Khởi tạo xa (tránh false positive)

  Serial.println("[Line Follow] Setup complete");
  Serial.println("  - 5 Line sensors ready");
  Serial.println("  - 2 Encoders attached");
  Serial.println("  - HC-SR04 ultrasonic ready (TRIG=21, ECHO=19)");
  Serial.print("  - Obstacle threshold: ");
  Serial.print(OBSTACLE_TH_CM);
  Serial.println(" cm");

  g_line_enabled   = true;
  seen_line_ever   = false;
  recovering       = false;
  last_seen        = NONE;

  pwmL_prev = 0;
  pwmR_prev = 0;

  pidL.i_term = pidL.prev_err = 0;
  pidR.i_term = pidR.prev_err = 0;

  motorsStop();
}

/* ================= Loop → do_line_loop ================= */
void do_line_loop() {
  if (!g_line_enabled) {
    motorsStop();
    return;
  }

  static unsigned long t_prev  = millis();
  static unsigned long bad_t   = 0;

  // ================================================================
  // OBSTACLE CHECK - ĐẦU TIÊN, trước mọi logic khác
  // Đảm bảo không bị bỏ qua bởi các early-return bên dưới
  // ================================================================
  {
    float dist = readDistanceCM_nonblock();

    static int  obs_hits    = 0;
    static unsigned long obs_t = 0;

    if (dist > 0.1f && dist < OBSTACLE_TH_CM) {
      if (millis() - obs_t > 600) obs_hits = 0;   // reset nếu gap > 600ms
      obs_hits++;
      obs_t = millis();
    } else {
      obs_hits = 0;
    }

    // Debug dist mỗi 400ms
    static unsigned long dbg_obs_t = 0;
    if (millis() - dbg_obs_t > 400) {
      Serial.printf("[US] dist=%.1fcm hits=%d recov=%s seen=%s\n",
        dist, obs_hits, recovering?"Y":"N", seen_line_ever?"Y":"N");
      dbg_obs_t = millis();
    }

    // Trigger: đã thấy line ít nhất 1 lần + 2 hit liên tiếp
    // KHÔNG cần !recovering - phải tránh kể cả khi đang recovery
    if (seen_line_ever && obs_hits >= 2) {
      Serial.printf(">>> OBSTACLE! dist=%.1fcm - STOPPING\n", dist);
      motorsStop();
      obs_hits = 0;   // reset ngay để không re-trigger

      obstacle_last_distance = dist;
      obstacle_last_time     = millis();

      // Chờ camera detect
      Serial.println(">>> Waiting 1200ms for camera...");
      delay(1200);

      String detectedColor = detectColorFromCamera();
      last_detected_color  = detectedColor;
      Serial.printf(">>> Color: %s\n", detectedColor.c_str());

      if (detectedColor == "green") {
        obstacle_last_action = "green_left";
        avoidObstacleLeft();
      } else {
        // red hoặc unknown → tránh PHẢI
        obstacle_last_action = (detectedColor == "red") ? "red_right" : "unknown_right";
        avoidObstacleRight();
      }

      // Reset về trạng thái line-follow bình thường
      recovering = false;
      noInterrupts(); encL_count = 0; encR_count = 0; interrupts();
      resetBothPID();
      t_prev = millis();
      lastColorDetectTime = 0;
      cachedObstacleColor = "none";
      obs_hits = 0;
      return;
    }
  }
  // ================================================================

  // ---- Đọc line 5 kênh (LOW = trên vạch) ----
  bool L2 = onLine(L2_SENSOR);
  bool L1 = onLine(L1_SENSOR);
  bool M  = onLine(M_SENSOR);
  bool R1 = onLine(R1_SENSOR);
  bool R2 = onLine(R2_SENSOR);

  if (L2 || L1 || M || R1 || R2) seen_line_ever = true;

  // ---- Chặn mẫu "tất cả HIGH" hoặc "tất cả LOW" > 1500ms -> dừng hẳn ----
  bool allH =  L2 && L1 && M && R1 && R2;
  bool allL = !L2 && !L1 && !M && !R1 && !R2;
  bool bad  = allH || allL;
  if (bad) {
    if (millis() - bad_t > 1500) {
      recovering = false;
      motorsStop();
      noInterrupts(); encL_count = 0; encR_count = 0; interrupts();
      resetBothPID();
      t_prev = millis();
      return;
    }
  } else {
    bad_t = millis();
  }

  // Đếm số cảm biến đang trên line
  int onCount = (int)L2 + (int)L1 + (int)M + (int)R1 + (int)R2;

  float vL_tgt = 0.0f;
  float vR_tgt = 0.0f;

  // Lái bằng PWM bổ sung (giống Nano)
  bool use_steer_pwm = false; // chỉ bật khi line-follow
  int  steer_dir     = 0;     // +1: quay TRÁI, -1: quay PHẢI, 0: thẳng
  int  steer_pwm     = 0;     // SOFT/HARD

  // ================== RECOVERY (mất line) ==================
  if (recovering) {
    resetBothPID();

    // Nếu thấy lại line (ít nhất 1 cảm biến ON và KHÔNG phải 4 đèn ON) → thoát recovery
    if ((L2 || L1 || M || R1 || R2) && onCount < 4) {
      recovering = false;
      motorsStop();
      noInterrupts(); encL_count = 0; encR_count = 0; interrupts();
      t_prev = millis();
      return;
    }
    // Hết thời gian recovery mà chưa thấy line → dừng hẳn
    else if (millis() - rec_t0 >= RECOV_TIME_MS) {
      recovering = false;
      motorsStop();
      noInterrupts(); encL_count = 0; encR_count = 0; interrupts();
      t_prev = millis();
      return;
    }
    // Đang recovery và chưa thấy line → quay theo last_seen
    else {
      float vF = 0.3f;
      if (last_seen == LEFT) {
        vL_tgt = 0;
        vR_tgt =  vF;
      } else if (last_seen == RIGHT) {
        vL_tgt =  vF;
        vR_tgt = 0;
      } else {
        vL_tgt = vF;
        vR_tgt = vF;
      }
    }
  }
  // ================== LOGIC CHÍNH (giống Nano) ==================
  else {
    // Mặc định: đi thẳng
    vL_tgt = v_base;
    vR_tgt = v_base;
    use_steer_pwm = true;

    // Trường hợp ĐẶC BIỆT: 4 đèn ON, 1 đèn OFF -> CHUYỂN SANG RECOVERY
    if (onCount == 4) {
      // Nếu L2 OFF -> line lệch về PHẢI -> nhớ RIGHT
      if (!L2) {
        last_seen = RIGHT;
      }
      // Nếu R2 OFF -> line lệch về TRÁI -> nhớ LEFT
      else if (!R2) {
        last_seen = LEFT;
      }
      // Nếu chỉ M OFF (L2,L1,R1,R2 đều ON) → chữ T, giữ nguyên last_seen

      use_steer_pwm = false;
      recovering    = true;
      rec_t0        = millis();
    }
    // Lệch trái mạnh
    else if ( (L2 && !R2 && !R1) || (L2 && L1 && !R1 && !R2) ) {
      last_seen = LEFT;
      steer_dir = +1;               // quay trái
      steer_pwm = STEER_PWM_HARD;   // mạnh
    }
    // Lệch phải mạnh
    else if ( (R2 && !L2 && !L1) || (R2 && R1 && !L1 && !L2) ) {
      last_seen = RIGHT;
      steer_dir = -1;               // quay phải
      steer_pwm = STEER_PWM_HARD;
    }
    // Lệch trái nhẹ
    else if ( (L1 && !R1 && !R2) || (L1 && M && !R1 && !R2) ) {
      last_seen = LEFT;
      steer_dir = +1;
      steer_pwm = STEER_PWM_SOFT;
    }
    // Lệch phải nhẹ
    else if ( (!L1 && !L2 && R1 && !M) || (!L1 && !L2 && M && R1) ) {
      last_seen = RIGHT;
      steer_dir = -1;
      steer_pwm = STEER_PWM_SOFT;
    }
    // Giao/cắt hoặc vùng line rộng (>=3 đèn, nhưng không phải 4 đèn ON)
    else if ( (L1 || L2) && M && (R1 || R2) ) {
      last_seen = NONE;
      steer_dir = 0;
      steer_pwm = 0;
    }
    // Đi thẳng: chỉ có M thấy line (hoặc gần như vậy)
    else if ( M ) {
      last_seen = NONE;
      steer_dir = 0;
      steer_pwm = 0;
    }
    // Mất line hoàn toàn
    else {
      use_steer_pwm = false;
      if (!seen_line_ever) {
        vL_tgt = 0.0f;
        vR_tgt = 0.0f;
      } else {
        recovering = true;
        rec_t0     = millis();
      }
    }
  }

  // ==== Né vật cản đã được xử lý ở ĐẦU loop ====
  bool  line_follow_active = isValidLineSample5(L2, L1, M, R1, R2);

  // DEBUG line sensors mỗi 500ms
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 500) {
    Serial.printf("[LINE] %s%s%s%s%s | recov=%s seen=%s\n",
      L2?"L2 ":"-- ", L1?"L1 ":"-- ", M?"M ":"- ",
      R1?"R1 ":"-- ", R2?"R2":"--",
      recovering?"Y":"N", seen_line_ever?"Y":"N");
    last_debug = millis();
  }

  // Áp bù lệch 2 bánh
  vL_tgt *= L_SCALE;
  vR_tgt *= R_SCALE;

  // ================== Chu kỳ PID + steer PWM ==================
  unsigned long now = millis();
  if (now - t_prev >= CTRL_DT_MS){
    float dt_s = (now - t_prev) / 1000.0f;
    t_prev = now;

    long cL, cR;
    noInterrupts();
    cL = encL_count; encL_count = 0;
    cR = encR_count; encR_count = 0;
    interrupts();

    float vL_meas = ticksToVel(cL, dt_s) * (vL_tgt >= 0 ? 1.0f : -1.0f);
    float vR_meas = ticksToVel(cR, dt_s) * (vR_tgt >= 0 ? 1.0f : -1.0f);

    const float V_MAX = 0.8f;
    vL_tgt = clampf(vL_tgt, -V_MAX, V_MAX);
    vR_tgt = clampf(vR_tgt, -V_MAX, V_MAX);

    int pwmL = pidStep(pidL, vL_tgt, vL_meas, dt_s);
    int pwmR = pidStep(pidR, vR_tgt, vR_meas, dt_s);

    // ======= Lái bằng steer PWM (giống code Nano) =======
    if (use_steer_pwm && !recovering) {
      if (steer_dir == +1) {
        // quay trái: bánh trái chậm hơn, bánh phải nhanh hơn
        pwmL -= steer_pwm;
        pwmR += steer_pwm;
      } else if (steer_dir == -1) {
        // quay phải
        pwmL += steer_pwm;
        pwmR -= steer_pwm;
      }
    }

    // Shaper PWM: deadband + slew
    int pwmL_cmd = shape_pwm((int)(1.1 * pwmL), pwmL_prev);
    int pwmR_cmd = shape_pwm(pwmR, pwmR_prev);
    pwmL_prev = pwmL_cmd;
    pwmR_prev = pwmR_cmd;

    driveWheelLeft (vL_tgt, pwmL_cmd);
    driveWheelRight(vR_tgt, pwmR_cmd);
  }
}
