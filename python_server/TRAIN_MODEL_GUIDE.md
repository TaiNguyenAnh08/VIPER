# 🎓 HƯỚNG DẪN TRAIN TENSORFLOW MODEL

## 📋 CHUẨN BỊ

1. **Vẽ biển báo lên giấy A4:**
   - 📄 Tờ 1: Vẽ HÌNH TRÒN to (đường kính ~10cm), màu đen
   - 📄 Tờ 2: Vẽ HÌNH VUÔNG to (cạnh ~10cm), màu đen
   - Vẽ đậm, rõ nét, nền giấy trắng

2. **Bật VIPER + ESP32-CAM**
   - Kết nối laptop vào WiFi VIPER
   - Mở camera stream: http://192.168.4.3:81/stream

---

## 📸 BƯỚC 1: CHỤP ẢNH TRAINING

### Cách 1: Dùng Camera Stream (ĐƠN GIẢN NHẤT)

1. **Mở stream:** http://192.168.4.3:81/stream
2. **Chụp ảnh HÌNH TRÒN:**
   - Đặt tờ giấy hình tròn trước camera (~15-20cm)
   - Thay đổi góc độ: thẳng, nghiêng trái, nghiêng phải
   - Thay đổi ánh sáng: sáng, tối, nửa sáng
   - **Chụp 30-50 ảnh khác nhau** (dùng screenshot hoặc Snipping Tool)

3. **Chụp ảnh HÌNH VUÔNG:** 
   - Làm tương tự với tờ giấy vuông
   - **Chụp 30-50 ảnh khác nhau**

4. **Lưu ảnh:**
   ```
   d:\cuoikynhung\training_images\
       circle\
           circle_001.jpg
           circle_002.jpg
           ...
       square\
           square_001.jpg
           square_002.jpg
           ...
   ```

### Cách 2: Dùng Python Script Tự Động (KHUYẾN NGHỊ ⭐)

**Tool tự động chụp 50 ảnh mỗi loại:**

```powershell
cd d:\cuoikynhung\python_server
python capture_training_images.py
```

**Quy trình:**
1. Script sẽ kiểm tra kết nối camera
2. Hỏi số lượng ảnh cần chụp (mặc định 50)
3. Yêu cầu đặt giấy HÌNH TRÒN → chụp 50 ảnh tự động (delay 0.8s)
4. Yêu cầu đổi sang giấy HÌNH VUÔNG → chụp 50 ảnh tiếp
5. Lưu vào: `d:\cuoikynhung\training_images\circle\` và `square\`

**Tips khi chụp:**
- Trong quá trình chụp, **THAY ĐỔI góc độ** (xoay giấy trái/phải)
- **THAY ĐỔI ánh sáng** (che tay, bật đèn, tắt đèn)
- **THAY ĐỔI khoảng cách** (gần/xa camera 10-25cm)
- Script sẽ tự động chụp liên tục, bạn chỉ cần di chuyển giấy

---

## 🤖 BƯỚC 2: TRAIN MODEL TRÊN TEACHABLE MACHINE

1. **Truy cập:** https://teachablemachine.withgoogle.com/train/image

2. **Tạo project mới:**
   - Chọn "Image Project" → "Standard image model"

3. **Upload ảnh:**
   - Class 1: Đổi tên → **"left"** (hình tròn → rẽ trái)
     - Upload 30-50 ảnh từ folder `circle\`
   - Class 2: Đổi tên → **"right"** (hình vuông → rẽ phải)
     - Upload 30-50 ảnh từ folder `square\`

4. **Train model:**
   - Nhấn "Train Model"
   - Đợi ~30-60 giây
   - Kiểm tra Preview: đưa ảnh test vào xem có đúng không

5. **Export model:**
   - Nhấn "Export Model"
   - Tab "TensorFlow Lite"
   - Chọn **"Quantized (int8)"** ← QUAN TRỌNG!
   - Nhấn "Download my model"
   - Giải nén ZIP → lấy file `model.tflite`

6. **Copy model vào project:**
   ```powershell
   # Backup model cũ
   Move-Item d:\cuoikynhung\python_server\shape_model.tflite d:\cuoikynhung\python_server\shape_model_old.tflite
   
   # Copy model mới
   Copy-Item "D:\Downloads\converted_tflite_quantized\model.tflite" d:\cuoikynhung\python_server\shape_model.tflite
   ```

7. **Restart Python server:**
   - Tắt server cũ (Ctrl+C)
   - Chạy lại:
   ```powershell
   cd d:\cuoikynhung\python_server
   python tf_server.py
   ```

---

## 🧪 BƯỚC 3: TEST MODEL

1. **Test với script Python:**
```powershell
cd d:\cuoikynhung\python_server
python test_with_images.py
```

2. **Test với ESP32-CAM thật:**
   - Đặt robot vào line following mode
   - Đặt tờ giấy hình tròn trước robot (~20cm)
   - Đợi xe detect obstacle
   - Xem Serial Monitor:
     ```
     >>> OBSTACLE! dist=18.2cm
     [TF] Requesting shape detection...
     [TF] Response: {"shape":"left","confidence":220}  ← THÀNH CÔNG!
     >>> Shape detected: left
     >>> AVOIDANCE RIGHT (Circle → turn LEFT) <<<
     ```

3. **Kiểm tra Python server log:**
   ```
   📸 Received image: 14523 bytes
   ⚙️  Preprocessing: resize to 96x96
   🧠 Running inference...
   📊 Scores: Left=220 Right=45
   ✅ Result: left (confidence: 220)
   127.0.0.1 - - [10/Mar/2026 14:23:15] "POST /predict HTTP/1.1" 200 -
   ```

---

## 🎯 TIPS CHO KẾT QUẢ TỐT:

✅ **Chụp nhiều góc độ:** thẳng, nghiêng trái, phải, trên, dưới  
✅ **Thay đổi ánh sáng:** sáng, tối, backlight  
✅ **Thay đổi khoảng cách:** 10cm, 15cm, 20cm, 25cm  
✅ **Nền đơn giản:** giấy trắng, nền sáng, tránh nhiễu  
✅ **Hình vẽ rõ nét:** viết đậm, đường nét liền mạch  

❌ **Tránh:**
- Ảnh mờ, tối, blur
- Hình quá nhỏ trong frame (<30% diện tích)
- Nền hoa văn phức tạp
- Ánh sáng chói lóa

---

## 🔧 TROUBLESHOOTING

### Model vẫn trả về "none"?
→ Confidence threshold quá cao, giảm xuống:
```python
# tf_server.py line 78
CONFIDENCE_THRESHOLD = 50  # Giảm từ 100 xuống 50
```

### Model detect sai?
→ Cần thêm ảnh training, đặc biệt ảnh bị detect sai

### Model chậm?
→ Dùng model Quantized (int8), đã là nhanh nhất

### Camera không chụp được?
→ Kiểm tra IP: http://192.168.4.3:81/stream

---

## 📊 KẾT QUẢ MONG ĐỢI

**Trước khi train (dummy model):**
```
Scores: Left=45 Right=52 → none (random)
```

**Sau khi train (real model):**
```
Circle paper → Scores: Left=220 Right=45 → left ✅
Square paper → Scores: Left=38 Right=215 → right ✅
Empty → Scores: Left=60 Right=55 → none ✅
```

---

## ⏱️ THỜI GIAN:

- Chụp ảnh: **5-10 phút**
- Upload lên Teachable Machine: **2 phút**
- Train model: **30-60 giây**
- Download + deploy: **1 phút**

**TỔNG: ~10-15 phút** 🚀
