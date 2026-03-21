# Hướng dẫn cài đặt VIPER Robot

## Thông tin hệ thống

- **Backend**: OpenCV (HoughCircles + Contour analysis)
- **Input**: Hình tròn/vuông màu đen trên nền trắng
- **Output**: "circle", "square", hoặc "none"
- **Packages**: Flask 3.0.0, OpenCV 4.8.1.78, NumPy 1.26.4

---

## Các bước cài đặt

### Bước 1: Kết nối WiFi VIPER
1. Bật robot (VIPER main board)
2. Đợi WiFi AP "VIPER" xuất hiện
3. Kết nối PC/Laptop vào WiFi: **VIPER** (password: `12345678`)

### Bước 2: Lấy IP của PC
```powershell
ipconfig
```
Tìm adapter "VIPER", ghi lại IP (VD: `192.168.4.3` hoặc `192.168.4.4`)

### Bước 3: Update ESP32-CAM Code
Mở file: `d:\cuoikynhung\CameraWebServer\CameraWebServer.ino`

Sửa dòng 35:
```cpp
#define OPENCV_SERVER_IP "192.168.4.3"  // ← Thay bằng IP PC của bạn
```

### Bước 4: Upload Code
**Arduino IDE:**
1. **ESP32-CAM**: Upload `CameraWebServer.ino`
   - Board: ESP32 Dev Module
   - Port: COM? (tìm port của ESP32-CAM)

2. **VIPER Main Board**: Upload `xe.ino`
   - Board: ESP32 Dev Module
   - Port: COM? (tìm port của VIPER)

### Bước 5: Chạy Python Server
```powershell
cd d:\cuoikynhung\python_server
pip install -r requirements.txt  # Chỉ lần đầu
python opencv_server.py
```

### Bước 6: Test Robot
1. Bật chế độ **LINE FOLLOWING** trên web UI: `http://192.168.4.1`
2. Đặt vật cản (~20cm trước xe): hình tròn hoặc vuông, **đen trên nền trắng**, size 4-8cm
3. Xe sẽ dừng → chụp ảnh → gửi lên server → nhận kết quả → rẽ tránh

---

## Kiểm tra nhanh

### Server OK?
```powershell
curl http://localhost:5000/health
# → {"status":"ok","backend":"OpenCV","version":"2.0"}
```

### ESP32-CAM OK?
```
Serial Monitor ESP32-CAM:
- "WiFi connected to VIPER!"
- "API ready at /detect_shape"
- "Server: http://192.168.4.x:5000"
```

### VIPER OK?
```
Serial Monitor VIPER:
- "Line Follow Setup complete"
- "WiFi AP started: VIPER"
```

### Web UI OK?
```
http://192.168.4.1
- Camera livestream hiển thị
- "LINE FOLLOWING" button bật/tắt được
- Motor control buttons hoạt động
```

---

## Workflow

```
┌─────────────────┐
│  VIPER Robot    │ Phát hiện vật cản < 25cm
│  (HC-SR04)      │
└────────┬────────┘
         │ HTTP GET
         ▼
┌─────────────────┐
│  ESP32-CAM      │ Chụp ảnh JPEG
│ 192.168.4.2     │
└────────┬────────┘
         │ HTTP POST (image/jpeg)
         ▼
┌─────────────────┐
│  Python Server  │ OpenCV Inference
│ 192.168.4.x:5000│ → {"shape":"circle"}
└────────┬────────┘
         │ JSON Response
         ▼
┌─────────────────┐
│  ESP32-CAM      │ Trả kết quả về VIPER
└────────┬────────┘
         │ JSON
         ▼
┌─────────────────┐
│  VIPER Robot    │ Rẽ hướng:
│                 │ • circle → rẽ TRÁI
│                 │ • square → rẽ PHẢI
└─────────────────┘
```

---

## Cấu trúc project

```
d:\cuoikynhung\
├── CameraWebServer/
│   └── CameraWebServer.ino    [ESP32-CAM code]
├── xe/
│   ├── xe.ino                  [VIPER main]
│   └── do_line.cpp             [Shape detection logic]
├── python_server/
│   ├── opencv_server.py        [Flask OpenCV server]
│   ├── requirements.txt
│   ├── test_server.py          [Test script]
│   ├── test_circle.jpg         [Test image]
│   └── test_square.jpg         [Test image]
```

---

## Troubleshooting

### Server không chạy
```powershell
pip list | Select-String "opencv|Flask"
cd python_server
python opencv_server.py
```

### ESP32-CAM không kết nối được server
1. Kiểm tra PC đã kết nối WiFi VIPER
2. Kiểm tra IP trong `CameraWebServer.ino` đúng chưa
3. Kiểm tra firewall Windows (tắt hoặc allow port 5000)
4. Ping test từ ESP32-CAM Serial Monitor

### Shape detection trả về "none"
- Kiểm tra hình vẽ đúng: đen trên nền trắng, đủ kích thước
- Cải thiện ánh sáng và tương phản
- Test thủ công: `python test_server.py test_circle.jpg`
