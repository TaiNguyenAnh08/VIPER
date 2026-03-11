# 🎉 HỆ THỐNG ĐÃ SẴN SÀNG!

## ✅ ĐÃ CÀI ĐẶT XONG:

### 1. Python Server ✅
- **Flask**: 3.0.0
- **TensorFlow**: 2.21.0
- **Pillow**: 10.2.0
- **NumPy**: 1.26.4

### 2. Dummy Model ✅
- **File**: `python_server/shape_model.tflite` (255 KB)
- **Input**: 96x96 grayscale, int8 quantized
- **Output**: 2 classes (left/right)
- **⚠️ NOTE**: Model này là DUMMY để test workflow, kết quả random!

### 3. Server Test ✅
- Server chạy tại: `http://localhost:5000`
- Health check: ✅ OK
- Prediction test: ✅ OK (trả về "none" vì confidence thấp - đúng design)

---

## 📋 BƯỚC TIẾP THEO:

### **Bước 1: Kết nối WiFi VIPER**
1. Bật robot (VIPER main board)
2. Đợi WiFi AP "VIPER" xuất hiện
3. Kết nối PC/Laptop vào WiFi: **VIPER** (password: `12345678`)

### **Bước 2: Lấy IP của PC**
Sau khi kết nối VIPER WiFi, chạy:
```powershell
ipconfig
```
Tìm adapter "VIPER", note IP (VD: `192.168.4.3` hoặc `192.168.4.4`)

### **Bước 3: Update ESP32-CAM Code**
Mở file: `d:\cuoikynhung\CameraWebServer\CameraWebServer.ino`

Sửa dòng 30:
```cpp
#define TF_SERVER_IP "192.168.4.3"  // ← Thay bằng IP PC của bạn
```

### **Bước 4: Upload Code**
**Arduino IDE:**
1. **ESP32-CAM**: Upload `CameraWebServer.ino`
   - Board: ESP32 Dev Module
   - Port: COM? (tìm port của ESP32-CAM)
   
2. **VIPER Main Board**: Upload `xe.ino`
   - Board: ESP32 Dev Module
   - Port: COM? (tìm port của VIPER)

### **Bước 5: Chạy Python Server**
**Trên PC (đã kết nối WiFi VIPER):**
```powershell
cd d:\cuoikynhung\python_server
python tf_server.py
```

Server sẽ chạy và sẵn sàng nhận request từ ESP32-CAM.

### **Bước 6: Test Robot**
1. Bật chế độ **LINE FOLLOWING** trên web UI: `http://192.168.4.1`
2. Đặt vật cản (có biển báo/hình) ở ~20cm trước xe
3. Xe sẽ:
   - Phát hiện vật cản (HC-SR04 < 25cm)
   - Dừng lại
   - Chụp ảnh → gửi lên Python server
   - Nhận kết quả "left"/"right"
   - Rẽ hướng tương ứng

---

## 🎯 TRAIN MODEL THẬT (Quan trọng!)

Model hiện tại chỉ là DUMMY, bạn cần train model thật:

### **Option 1: Teachable Machine (Dễ nhất)** ⭐
1. Vào: https://teachablemachine.withgoogle.com/train/image
2. Tạo 2 classes:
   - **Left**: Upload ~50 ảnh hình tròn
   - **Right**: Upload ~50 ảnh hình vuông
3. Train model
4. Export → **TensorFlow Lite** (Quantized int8)
5. Download file `.tflite`
6. Copy vào: `d:\cuoikynhung\python_server\shape_model.tflite`
7. Restart server

### **Option 2: Train bằng Python**
1. Thu thập dataset:
   - `dataset/left/` - 100+ ảnh hình tròn
   - `dataset/right/` - 100+ ảnh hình vuông
2. Train với TensorFlow/Keras
3. Convert sang TFLite int8 quantized
4. Copy vào `python_server/shape_model.tflite`

---

## 🔧 WORKFLOW HOÀN CHỈNH:

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
│  Python Server  │ TensorFlow Inference
│ 192.168.4.x:5000│ → {"shape":"left"}
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
│                 │ • left (circle) → rẽ TRÁI
│                 │ • right (square) → rẽ PHẢI
└─────────────────┘
```

---

## 📂 CẤU TRÚC PROJECT:

```
d:\cuoikynhung\
├── CameraWebServer/
│   └── CameraWebServer.ino    [ESP32-CAM code - ĐÃ UPDATE]
├── xe/
│   ├── xe.ino                  [VIPER main - ĐÃ UPDATE]
│   └── do_line.cpp             [TensorFlow detection - ĐÃ UPDATE]
├── python_server/
│   ├── tf_server.py            [Flask TensorFlow server - ✅]
│   ├── shape_model.tflite      [Dummy model - CẦN TRAIN THẬT]
│   ├── requirements.txt        [✅ ĐÃ CÀI]
│   ├── test_server.py          [Test script]
│   ├── test_with_images.py    [Test với ảnh mẫu]
│   ├── create_dummy_model.py  [Tạo dummy model]
│   ├── test_circle.jpg         [Test image]
│   └── test_square.jpg         [Test image]
├── tensorflow_nhung/           [Arduino TFLite code - không dùng]
└── README_TENSORFLOW.md        [Hướng dẫn đầy đủ]
```

---

## 🐛 TROUBLESHOOTING:

### Server không chạy
```powershell
# Kiểm tra packages
pip list | Select-String "tensorflow|Flask"

# Chạy lại
cd python_server
python tf_server.py
```

### ESP32-CAM không kết nối được server
1. Kiểm tra PC đã kết nối WiFi VIPER
2. Kiểm tra IP trong `CameraWebServer.ino` đúng chưa
3. Kiểm tra firewall Windows (tắt hoặc allow port 5000)
4. Ping test từ ESP32-CAM Serial Monitor

### Shape detection trả về "none"
- **Nếu dùng dummy model**: BÌNH THƯỜNG! Train model thật đi.
- **Nếu dùng model thật**: 
  - Giảm `CONFIDENCE_THRESHOLD` trong `tf_server.py` (dòng 78)
  - Train lại model với nhiều ảnh hơn
  - Cải thiện ánh sáng

---

## ✨ TỔNG KẾT:

✅ Python server: **SẴN SÀNG**
✅ Dummy model: **SẴN SÀNG** (test workflow)
✅ ESP32-CAM code: **CẦN UPLOAD** (sau khi update IP)
✅ VIPER code: **CẦN UPLOAD**
⚠️ Model thật: **CẦN TRAIN** (Teachable Machine khuyến nghị)

**Bạn làm theo từng bước trong README này là xong! 🚀**

---

## 📞 DEBUG LOGS MẪU THÀNH CÔNG:

### Python Server:
```
✅ Model loaded successfully
   Input shape: [1, 96, 96, 1]
   Output shape: [1, 2]
🚀 Starting TensorFlow server...
 * Running on http://0.0.0.0:5000
📸 Received image: 15234 bytes
   Scores: Left=180, Right=75
✅ Result: left (confidence: 180)
```

### ESP32-CAM:
```
[TF] Capturing image for shape detection...
[TF] Captured 320x240 JPEG, 15234 bytes
[TF] POSTing to http://192.168.4.3:5000/predict
[TF] Response: {"shape":"left","confidence":180}
[TF] ✅ Detected shape: left
[API] Responded: {"shape":"left"}
```

### VIPER:
```
>>> OBSTACLE! dist=22.3cm - STOPPING
>>> Waiting 1200ms for shape detection...
[TF] Requesting shape detection from camera...
[TF] Response: {"shape":"left"}
[TF] Detected shape: left
>>> Shape detected: left
>>> AVOIDANCE RIGHT (Circle → turn LEFT) <<<
  [1] Turn LEFT 60°...
  [2] Forward 20cm...
  ...
>>> LEFT AVOIDANCE DONE <<<
```

**DONE! Chúc bạn thành công! 🎉**
