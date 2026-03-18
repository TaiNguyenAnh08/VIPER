# QUICK START - 5 PHÚT BẬT ROBOT (OpenCV)

## 🚀 CÁC BƯỚC NHANH:

### 1️⃣ Bật Robot & Kết nối WiFi (1 phút)
```
1. Bật VIPER main board
2. Đợi WiFi "VIPER" xuất hiện
3. PC kết nối WiFi: VIPER (pass: 12345678)
4. Chạy `ipconfig` → lấy IP PC (VD: 192.168.4.3)
```

### 2️⃣ Update ESP32-CAM IP (30 giây)
```
Mở: CameraWebServer/CameraWebServer.ino
Dòng 33: #define TF_SERVER_IP "192.168.4.3" ← IP PC của bạn
```

### 3️⃣ Upload Code (2 phút)
```
Arduino IDE:
- Upload CameraWebServer.ino → ESP32-CAM
- Upload xe.ino → VIPER main board
```

### 4️⃣ Chạy Python Server (30 giây)
```powershell
cd d:\cuoikynhung\python_server
pip install -r requirements.txt  # Only first time
python tf_server.py
```

### 5️⃣ Test (1 phút)
```
1. Web UI: http://192.168.4.1 → bật LINE FOLLOWING
2. Đặt vật cản (hình tròn hoặc vuông đen trên trắng) trước xe ~20cm
3. Xe sẽ dừng → server detect shape → rẽ tránh
```

---

## ⚠️ QUAN TRỌNG:

✅ **Không cần training!** OpenCV tự detect hình tròn & vuông.

**Yêu cầu vật cản:**
- ✓ Hình tròn hoặc vuông
- ✓ Màu đen
- ✓ Nền trắng
- ✓ Size: 4-8cm (lớp)

---

## 📊 KIỂM TRA NHANH:

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
- ✓ Camera livestream hiển thị
- ✓ "LINE FOLLOWING" button bật/tắt
- ✓ Motor control buttons hoạt động
```

---

**ĐỌC ĐẦY ĐỦ**: [SETUP_COMPLETE.md](SETUP_COMPLETE.md)
**HƯỚNG DẪN CHI TIẾT**: [python_server/README.md](python_server/README.md)
