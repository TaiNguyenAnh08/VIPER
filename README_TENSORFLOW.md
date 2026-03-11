# HƯỚNG DẪN CÀI ĐẶT TENSORFLOW SHAPE DETECTION

## BƯỚC 1: Chuẩn bị Model TensorFlow

### 1.1. Copy model vào thư mục
```bash
cd d:\cuoikynhung\python_server
```

Copy file `shape_model.tflite` (model đã train) vào thư mục này.

**Lưu ý:** Model phải có:
- Input: 96x96 grayscale, int8 quantized
- Output: [left_score, right_score]
- Classes: 0=Left (hình tròn), 1=Right (hình vuông)

---

## BƯỚC 2: Cài đặt Python Server

### 2.1. Cài các package cần thiết
```bash
pip install -r requirements.txt
```

### 2.2. Kết nối WiFi VIPER
- Kết nối PC/Laptop vào WiFi: **VIPER**
- Password: `12345678` (hoặc theo xe.ino)

### 2.3. Kiểm tra IP của PC
Mở PowerShell và chạy:
```powershell
ipconfig
```

Tìm adapter "VIPER", note lại IP (VD: `192.168.4.3`)

---

## BƯỚC 3: Update Code ESP32-CAM

### 3.1. Mở file CameraWebServer.ino
Tìm dòng:
```cpp
#define TF_SERVER_IP "192.168.4.3"
```

Thay `192.168.4.3` bằng IP PC của bạn (từ bước 2.3)

### 3.2. Upload code lên ESP32-CAM
```
Arduino IDE → Tools → Board → ESP32 Dev Module
Upload CameraWebServer.ino
```

---

## BƯỚC 4: Upload Code VIPER

### 4.1. Upload xe.ino
```
Arduino IDE → Upload xe.ino (main board)
```

Code đã update để dùng TensorFlow detection:
- Hình tròn (circle) → rẽ trái
- Hình vuông (square) → rẽ phải
- Unknown → rẽ trái (default)

---

## BƯỚC 5: Chạy Python Server

### 5.1. Start server
```bash
cd d:\cuoikynhung\python_server
python tf_server.py
```

Server sẽ chạy và in:
```
✅ Model loaded successfully
🚀 Starting TensorFlow server...
   Connect your PC to WiFi: VIPER
   ESP32-CAM will POST to: http://192.168.4.x:5000/predict
```

### 5.2. Test server (optional)
Mở tab PowerShell khác:
```powershell
curl http://localhost:5000/health
```

Kết quả:
```json
{"status":"ok","model_loaded":true}
```

---

## BƯỚC 6: Test hoàn chỉnh

### 6.1. Kiểm tra kết nối
1. **VIPER main board**: Boot → Serial Monitor → WiFi AP started
2. **ESP32-CAM**: Boot → Connected to VIPER → API ready
3. **Python server**: Chạy và in "Model loaded"

### 6.2. Test thủ công ESP32-CAM
Trên browser, truy cập:
```
http://192.168.4.2/detect_shape
```

Kiểm tra Serial Monitor của ESP32-CAM xem có POST tới Python server không.

### 6.3. Test với robot
1. Bật chế độ LINE FOLLOWING trên web UI
2. Đặt vật cản có biển báo hình tròn/vuông ở khoảng 20cm trước xe
3. Xe sẽ:
   - Dừng lại khi HC-SR04 < 25cm
   - Chờ 1200ms
   - Chụp ảnh → POST lên server
   - Nhận kết quả "left"/"right"
   - Rẽ hướng tương ứng

---

## WORKFLOW HOÀN CHỈNH

```
[1] VIPER phát hiện vật cản (HC-SR04 < 25cm)
      ↓
[2] VIPER gọi: http://192.168.4.2/detect_shape
      ↓
[3] ESP32-CAM chụp ảnh JPEG
      ↓
[4] ESP32-CAM POST ảnh: http://192.168.4.3:5000/predict
      ↓
[5] Python server TensorFlow inference
      ↓
[6] Python trả về: {"shape":"left"} hoặc {"shape":"right"}
      ↓
[7] ESP32-CAM trả về VIPER: {"shape":"left"}
      ↓
[8] VIPER rẽ hướng:
    - "left" (circle) → Rẽ TRÁI (avoidObstacleRight)
    - "right" (square) → Rẽ PHẢI (avoidObstacleLeft)
    - "none" → Rẽ TRÁI (default)
```

---

## TROUBLESHOOTING

### Lỗi: Model not found
```
❌ Failed to load model: [Errno 2] No such file or directory: 'shape_model.tflite'
```
**Giải pháp:** Copy file `shape_model.tflite` vào thư mục `python_server/`

### Lỗi: ESP32-CAM không kết nối server
```
[TF] ❌ HTTP error: -1
```
**Giải pháp:**
1. Kiểm tra PC đã kết nối WiFi VIPER
2. Kiểm tra IP trong CameraWebServer.ino đúng chưa
3. Kiểm tra firewall Windows: tắt hoặc allow port 5000
4. Test ping từ ESP32-CAM:
   ```cpp
   WiFi.ping(IPAddress(192,168,4,3))
   ```

### Lỗi: Python server không nhận request
```
No request received
```
**Giải pháp:**
1. Kiểm tra server đang chạy: `curl http://localhost:5000/health`
2. Kiểm tra firewall: `netsh advfirewall firewall add rule name="Flask" dir=in action=allow protocol=TCP localport=5000`

### Lỗi: Shape detection trả về "none"
```
[TF] ✅ Detected shape: none
```
**Giải pháp:**
1. Confidence threshold thấp → Giảm `CONFIDENCE_THRESHOLD` trong `tf_server.py`
2. Model không nhận diện → Train lại model với nhiều ảnh hơn
3. Lighting kém → Cải thiện ánh sáng

---

## LOG MẪU THÀNH CÔNG

### Python Server:
```
📸 Received image: 15234 bytes
   Scores: Left=180, Right=75
✅ Result: left (confidence: 180)
```

### ESP32-CAM:
```
[TF] Capturing image for shape detection...
[TF] Captured 320x240 JPEG, 15234 bytes
[TF] POSTing to http://192.168.4.3:5000/predict
[TF] Response: {"shape":"left","confidence":180,"scores":{"left":180,"right":75}}
[TF] ✅ Detected shape: left
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
  [1] Turn LEFT 60° (code LEFT = thực tế RIGHT)
  ...
```

---

## NOTES

- Server cần chạy trước khi bật robot
- Nếu server crash, robot sẽ rẽ trái (default)
- Camera QVGA 320x240 JPEG ~10-20KB
- Inference time: ~200-500ms trên PC, ~1-2s trên Raspberry Pi
- Total detection time: ~2-3s (capture + upload + inference + response)
