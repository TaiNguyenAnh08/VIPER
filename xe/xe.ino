#include <WiFi.h>
#include <WebServer.h>
#include "do_line.h"
#include "index.h"

// Disable brownout detector (TEMPORARY FIX - Use proper 5V/2A power!)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================= WiFi AP =================
const char* ssid = "VIPER";
const char* password = "12345678";

// ================= Motor pins =================
// Left motor
#define IN1 12
#define IN2 14
#define ENA 13
// Right motor
#define IN3 4
#define IN4 2
#define ENB 15

// ================= Speed =================
int speed_linear = 130;
int speed_rot    = 110;
const int SPEED_MIN  = 60;
const int SPEED_MAX  = 255;
const int SPEED_STEP = 10;

// Hệ số bù lệch giữa 2 bánh
const float L_SCALE = 1.06f;  // tăng nhẹ tốc độ target bánh trái (hoặc)
const float R_SCALE = 1.00f;  // giữ nguyên bánh phải

// Giảm tốc bánh phía “bên trong cua” khi đi chéo (0–100%)
const int DIAG_SCALE = 70; // 70% -> cua mượt
static inline int diagScale(int v){ return v * DIAG_SCALE / 100; }
static inline int clamp(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }

// ============ Đảo hướng steer tiến ============
const bool INVERT_STEER = true; // true: forward-left giảm bánh PHẢI

// ================= Server =================
WebServer server(80);

// ================= Mode =================
enum UIMode { MODE_MANUAL=0, MODE_LINE=1 };
volatile UIMode currentMode = MODE_MANUAL;
static bool lineInited = false;   // để chỉ gọi do_line_setup() một lần
bool line_mode = false;

// ================= Shape Detection =================
String lastDetectedShape = "none";
unsigned long lastShapeUpdate = 0;

// ================= Motion state =================
enum Motion { STOPPED, FWD, BWD, LEFT_TURN, RIGHT_TURN, FWD_LEFT, FWD_RIGHT, BACK_LEFT, BACK_RIGHT };
volatile Motion curMotion = STOPPED;

// ======= Prototypes
void forward(); void backward(); void left(); void right(); void stopCar();
void forwardLeft(); void forwardRight(); void backwardLeft(); void backwardRight();
void applyCurrentMotion();

// ================= Setup =================
void setup() {
  // ⚠️ DISABLE BROWNOUT (Temporary fix for weak USB power)
  // TODO: Use proper 5V/2A power supply instead!
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);

  stopCar();

  Serial.begin(115200);
  delay(1000);  // Chờ Serial khởi động
  Serial.println("\n\n=== VIPER Starting ===");
  Serial.println("⚠️  WARNING: Brownout detector disabled!");
  Serial.println("Firmware: Vision Intelligent Path Exploration Robot");
  Serial.println("========================");
  
  WiFi.softAP(ssid, password);
  Serial.print("Hotspot IP: "); Serial.println(WiFi.softAPIP());
  Serial.println("WiFi SSID: VIPER");
  Serial.println("Password: 12345678");
  Serial.println("========================\n");

  // UI
  server.on("/", [](){
    server.send_P(200, "text/html", index_html);
  });

  // Mode APIs
  server.on("/getMode", [](){
    server.send(200, "text/plain", (currentMode==MODE_LINE)?"line":"manual");
  });

  server.on("/setMode", [](){
    if (!server.hasArg("m")) { server.send(400,"text/plain","manual"); return; }
    String m = server.arg("m");

    if (m=="line") {
      // Switching to LINE mode
      do_line_abort();  // Dừng manual mode trước (nếu đang chạy)
      stopCar();        // Dừng motor
      delay(200);       // Cho motor dừng hẳn
      
      currentMode = MODE_LINE;
      line_mode = true;
      do_line_setup();  // Setup line mode
      Serial.println("[MODE] Switched to LINE FOLLOWING");
      
    } else {
      // Switching to MANUAL mode
      do_line_abort();  // Set g_line_enabled = false (thoát avoidance ngay khi có thể)
      stopCar();        // Dừng motor
      delay(200);       // Cho motor dừng hẳn
      
      currentMode = MODE_MANUAL;
      line_mode = false;
      Serial.println("[MODE] Switched to MANUAL - Line following ABORTED");
    }
    server.send(200,"text/plain",(currentMode==MODE_LINE)?"line":"manual");
  });


  // Moves (Manual only)
  server.on("/forward",    [](){ curMotion=FWD;        if(currentMode==MODE_MANUAL) forward();      server.send(200,"text/plain","OK"); });
  server.on("/backward",   [](){ curMotion=BWD;        if(currentMode==MODE_MANUAL) backward();     server.send(200,"text/plain","OK"); });
  server.on("/left",       [](){ curMotion=LEFT_TURN;  if(currentMode==MODE_MANUAL) left();         server.send(200,"text/plain","OK"); });
  server.on("/right",      [](){ curMotion=RIGHT_TURN; if(currentMode==MODE_MANUAL) right();        server.send(200,"text/plain","OK"); });
  server.on("/stop",       [](){ curMotion=STOPPED;    if(currentMode==MODE_MANUAL) stopCar();      server.send(200,"text/plain","OK"); });

  // Diagonals (Manual only)
  server.on("/fwd_left",   [](){ curMotion=FWD_LEFT;   if(currentMode==MODE_MANUAL) forwardLeft();  server.send(200,"text/plain","OK"); });
  server.on("/fwd_right",  [](){ curMotion=FWD_RIGHT;  if(currentMode==MODE_MANUAL) forwardRight(); server.send(200,"text/plain","OK"); });
  server.on("/back_left",  [](){ curMotion=BACK_LEFT;  if(currentMode==MODE_MANUAL) backwardLeft(); server.send(200,"text/plain","OK"); });
  server.on("/back_right", [](){ curMotion=BACK_RIGHT; if(currentMode==MODE_MANUAL) backwardRight();server.send(200,"text/plain","OK"); });

  // Speed (Manual only applyCurrentMotion)
  server.on("/speed/lin/up",   [](){ speed_linear = clamp(speed_linear+SPEED_STEP, SPEED_MIN, SPEED_MAX); if(currentMode==MODE_MANUAL) applyCurrentMotion(); server.send(200,"text/plain","OK"); });
  server.on("/speed/lin/down", [](){ speed_linear = clamp(speed_linear-SPEED_STEP, SPEED_MIN, SPEED_MAX); if(currentMode==MODE_MANUAL) applyCurrentMotion(); server.send(200,"text/plain","OK"); });
  server.on("/speed/rot/up",   [](){ speed_rot    = clamp(speed_rot+SPEED_STEP,    SPEED_MIN, SPEED_MAX); if(currentMode==MODE_MANUAL) applyCurrentMotion(); server.send(200,"text/plain","OK"); });
  server.on("/speed/rot/down", [](){ speed_rot    = clamp(speed_rot-SPEED_STEP,    SPEED_MIN, SPEED_MAX); if(currentMode==MODE_MANUAL) applyCurrentMotion(); server.send(200,"text/plain","OK"); });
  server.on("/speed",          [](){
    String s = "Lin: " + String(speed_linear) + "  |  Rot: " + String(speed_rot);
    server.send(200,"text/plain", s);
  });

  // Get current detected shape
  server.on("/api/shape", HTTP_GET, [](){
    String json = "{\"shape\":\"" + lastDetectedShape + "\",\"age\":" + String(millis() - lastShapeUpdate) + "}";
    server.send(200, "application/json", json);
  });

  // Debug: Line status (không cần USB Serial Monitor)
  server.on("/api/status", HTTP_GET, [](){
    if (currentMode == MODE_LINE) {
      String status = getLineStatus();
      server.send(200, "application/json", status);
    } else {
      server.send(200, "application/json", "{\"mode\":\"manual\"}");
    }
  });

  // Obstacle detection status for Web UI
  server.on("/api/obstacle_status", HTTP_GET, [](){
    if (currentMode == MODE_LINE) {
      String obstacleInfo = getObstacleStatus();
      server.send(200, "application/json", obstacleInfo);
    } else {
      server.send(200, "application/json", "{\"distance\":0,\"color\":\"none\",\"action\":\"none\",\"time\":0}");
    }
  });

  server.begin();
}

void loop() {
  server.handleClient();  // Xử lý web requests
  
  if (currentMode == MODE_LINE) {
    // Trao quyền hoàn toàn cho do_line của bạn
    do_line_loop();
  } else {
    if (line_mode) {
      stopCar();
      line_mode = false;
    }
    // Manual: không cần lặp nhanh. Có thể để trống hoặc delay nhẹ
    delay(5);
  }
}

// ================= Apply current motion (Manual) =================
void applyCurrentMotion(){
  switch(curMotion){
    case FWD:        forward(); break;
    case BWD:        backward(); break;
    case LEFT_TURN:  left(); break;
    case RIGHT_TURN: right(); break;
    case FWD_LEFT:   forwardLeft(); break;
    case FWD_RIGHT:  forwardRight(); break;
    case BACK_LEFT:  backwardLeft(); break;
    case BACK_RIGHT: backwardRight(); break;
    default:         stopCar(); break;
  }
}

// ================= Motor control (Manual) =================
void forward() {
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
  analogWrite(ENA, speed_linear);
  analogWrite(ENB, speed_linear);
}
void backward() {
  digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
  analogWrite(ENA, speed_linear);
  analogWrite(ENB, speed_linear);
}
void left() {  // quay tại chỗ
  digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
  analogWrite(ENA, speed_rot);
  analogWrite(ENB, speed_rot);
}
void right() { // quay tại chỗ
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
  analogWrite(ENA, speed_rot);
  analogWrite(ENB, speed_rot);
}
void stopCar() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW); digitalWrite(IN4,LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

// ========= Diagonal steering (Manual) =========
void forwardLeft() {
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
  if (!INVERT_STEER) { // giảm TRÁI
    analogWrite(ENA, speed_linear);
    analogWrite(ENB, diagScale(speed_linear));
  } else {             // giảm PHẢI
    analogWrite(ENA, diagScale(speed_linear));
    analogWrite(ENB, speed_linear);
  }
}
void forwardRight() {
  digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
  digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
  if (!INVERT_STEER) { // giảm PHẢI
    analogWrite(ENA, diagScale(speed_linear));
    analogWrite(ENB, speed_linear);
  } else {             // giảm TRÁI
    analogWrite(ENA, speed_linear);
    analogWrite(ENB, diagScale(speed_linear));
  }
}
void backwardLeft() {
  digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
  analogWrite(ENA, diagScale(speed_linear)); // bánh PHẢI chậm hơn
  analogWrite(ENB, speed_linear);
}
void backwardRight() {
  digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
  analogWrite(ENA, speed_linear);
  analogWrite(ENB, diagScale(speed_linear)); // bánh TRÁI chậm hơn
}