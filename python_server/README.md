# TensorFlow Shape Detection Server

Python server nhận ảnh từ ESP32-CAM và trả về kết quả nhận dạng hình dạng (Left/Right).

## Cài đặt

### 1. Cài Python packages
```bash
cd python_server
pip install -r requirements.txt
```

### 2. Copy model TFLite
Copy file `shape_model.tflite` (model bạn train) vào thư mục `python_server/`

### 3. Kết nối WiFi VIPER
Trên PC/Laptop, kết nối WiFi: **VIPER** (password: theo xe.ino)

Sau khi kết nối, PC sẽ có IP dạng `192.168.4.x` (thường là 192.168.4.3 hoặc 192.168.4.4)

### 4. Chạy server
```bash
python tf_server.py
```

Server sẽ chạy trên `http://0.0.0.0:5000`

### 5. Kiểm tra IP của PC
Mở terminal/cmd và chạy:
```bash
ipconfig  # Windows
ifconfig  # Mac/Linux
```

Tìm IP của adapter "VIPER" (VD: 192.168.4.3)

### 6. Update ESP32-CAM
Trong file `CameraWebServer.ino`, thay đổi:
```cpp
#define TF_SERVER_IP "192.168.4.3"  // Thay bằng IP PC của bạn
```

## API Endpoints

### POST /predict
Nhận ảnh JPEG/PNG và trả về kết quả nhận dạng.

**Request:**
- Method: POST
- Body: Raw image bytes (JPEG/PNG)
- Content-Type: image/jpeg

**Response:**
```json
{
  "shape": "left",
  "confidence": 180,
  "scores": {
    "left": 180,
    "right": 75
  }
}
```

**Possible values:**
- `"left"` - Hình tròn → rẽ trái
- `"right"` - Hình vuông → rẽ phải  
- `"none"` - Không nhận dạng được

### GET /health
Kiểm tra server đang chạy.

**Response:**
```json
{
  "status": "ok",
  "model_loaded": true
}
```

## Test thủ công

Bạn có thể test server bằng curl:
```bash
curl -X POST http://192.168.4.3:5000/predict \
  -H "Content-Type: image/jpeg" \
  --data-binary @test_image.jpg
```

## Workflow hoàn chỉnh

1. **VIPER** phát hiện vật cản < 25cm
2. **VIPER** gọi ESP32-CAM: `http://192.168.4.2/detect_shape`
3. **ESP32-CAM** chụp ảnh → POST lên Python server: `http://192.168.4.3:5000/predict`
4. **Python server** chạy TensorFlow → trả về `{"shape":"left"}` hoặc `{"shape":"right"}`
5. **ESP32-CAM** trả kết quả về VIPER: `{"shape":"left"}`
6. **VIPER** rẽ hướng tương ứng

## Troubleshooting

### Server không chạy
- Kiểm tra Python đã cài đặt: `python --version`
- Kiểm tra packages: `pip list | grep tensorflow`

### ESP32-CAM không kết nối được server
- Kiểm tra PC đã kết nối WiFi VIPER
- Kiểm tra firewall Windows (tắt hoặc allow port 5000)
- Ping test: `ping 192.168.4.3` từ ESP32 serial monitor

### Model không load được
- Kiểm tra file `shape_model.tflite` tồn tại
- Kiểm tra TensorFlow version tương thích với model

## Notes

- Model cần input: 96x96 grayscale, int8 quantized
- Confidence threshold mặc định: 100 (có thể điều chỉnh trong code)
- Server log chi tiết để debug: scores, confidence, result
