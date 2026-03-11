# QUICK START - 5 PHÚT BẬT ROBOT

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
Dòng 30: #define TF_SERVER_IP "192.168.4.3" ← IP PC của bạn
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
python tf_server.py
```

### 5️⃣ Test (1 phút)
```
1. Web UI: http://192.168.4.1 → bật LINE FOLLOWING
2. Đặt vật cản trước xe ~20cm
3. Xe sẽ dừng → chụp ảnh → rẽ
```

---

## ⚠️ LƯU Ý:

**Model hiện tại là DUMMY** → kết quả random!

**Train model thật:**
1. Vào: https://teachablemachine.withgoogle.com/train/image
2. Upload ảnh hình tròn (left) và hình vuông (right)
3. Train → Export TFLite int8
4. Copy file `.tflite` vào `python_server/shape_model.tflite`
5. Restart server

---

## 📊 KIỂM TRA NHANH:

### Server OK?
```powershell
curl http://localhost:5000/health
# → {"status":"ok","model_loaded":true}
```

### ESP32-CAM OK?
```
Serial Monitor ESP32-CAM:
- "WiFi connected to VIPER!"
- "API ready at /detect_shape"
- "TF Server: http://192.168.4.x:5000"
```

### VIPER OK?
```
Serial Monitor VIPER:
- "Line Follow Setup complete"
- "WiFi AP started: VIPER"
```

---

**ĐỌC ĐẦY ĐỦ**: [SETUP_COMPLETE.md](SETUP_COMPLETE.md)
