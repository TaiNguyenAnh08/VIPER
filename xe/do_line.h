#pragma once
#include <Arduino.h>
void do_line_setup();
void do_line_loop();
// yêu cầu dừng ngay mọi hành vi trong do_line (kể cả đang trong while)
void do_line_abort();
void motorsStop();

// Debug: Lấy status để hiển thị trên web (không cần USB)
String getLineStatus();
String getObstacleStatus(); // Obstacle detection status for Web UI
