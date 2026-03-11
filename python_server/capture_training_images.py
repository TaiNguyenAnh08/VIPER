"""
ESP32-CAM Training Image Capture Tool
Tự động chụp ảnh từ ESP32-CAM để train TensorFlow model
"""
import requests
import time
import os
from datetime import datetime

# Cấu hình
ESP32_CAM_IP = "192.168.4.3"
CAPTURE_URL = f"http://{ESP32_CAM_IP}:81/capture"  # Port 81 = camera server
OUTPUT_DIR = r"d:\cuoikynhung\training_images"

def test_camera_connection():
    """Kiểm tra kết nối camera"""
    print("🔍 Testing camera connection...")
    try:
        response = requests.get(f"http://{ESP32_CAM_IP}", timeout=3)
        if response.status_code == 200:
            print(f"✅ Camera connected at {ESP32_CAM_IP}")
            return True
        else:
            print(f"⚠️  Camera responded with status {response.status_code}")
            return False
    except Exception as e:
        print(f"❌ Cannot connect to camera: {e}")
        print(f"\n💡 Checklist:")
        print(f"   1. ESP32-CAM is powered on")
        print(f"   2. Laptop connected to VIPER WiFi")
        print(f"   3. Camera IP is {ESP32_CAM_IP}")
        return False

def create_directories():
    """Tạo thư mục lưu ảnh"""
    os.makedirs(f"{OUTPUT_DIR}/circle", exist_ok=True)
    os.makedirs(f"{OUTPUT_DIR}/square", exist_ok=True)
    print(f"📁 Output directory: {OUTPUT_DIR}")

def capture_single_image(shape_name, index):
    """Chụp 1 ảnh"""
    try:
        response = requests.get(CAPTURE_URL, timeout=5)
        if response.status_code == 200 and len(response.content) > 1000:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{OUTPUT_DIR}/{shape_name}/{shape_name}_{index:03d}_{timestamp}.jpg"
            with open(filename, 'wb') as f:
                f.write(response.content)
            print(f"✅ [{index:3d}] {len(response.content):6d} bytes → {os.path.basename(filename)}")
            return True
        else:
            print(f"❌ [{index:3d}] Failed: HTTP {response.status_code}, size={len(response.content)}")
            return False
    except Exception as e:
        print(f"❌ [{index:3d}] Error: {e}")
        return False

def capture_images_interactive(shape_name, count=50, delay=0.8):
    """
    Chụp nhiều ảnh với interactive mode
    
    Args:
        shape_name: "circle" hoặc "square"
        count: số lượng ảnh cần chụp
        delay: thời gian giữa các lần chụp (giây)
    """
    symbol = "⚫ HÌNH TRÒN" if shape_name == "circle" else "⬛ HÌNH VUÔNG"
    print(f"\n{'='*60}")
    print(f"📸 CHỤP ẢNH CHO {symbol}")
    print(f"{'='*60}")
    print(f"Target: {count} images")
    print(f"Delay: {delay}s between captures")
    print(f"\n📋 Instructions:")
    print(f"   1. Đặt tờ giấy vẽ {symbol} trước camera")
    print(f"   2. Khoảng cách: 15-20cm")
    print(f"   3. Trong quá trình chụp, THAY ĐỔI:")
    print(f"      - Góc độ: xoay giấy trái/phải")
    print(f"      - Ánh sáng: che bớt, thêm ánh sáng")
    print(f"      - Khoảng cách: gần/xa camera")
    print(f"\n⚠️  Nhấn Ctrl+C để dừng sớm\n")
    
    input("⏸️  Nhấn ENTER khi đã sẵn sàng...")
    
    success_count = 0
    for i in range(1, count + 1):
        if capture_single_image(shape_name, i):
            success_count += 1
        time.sleep(delay)
    
    print(f"\n{'='*60}")
    print(f"✅ Done! Captured {success_count}/{count} images for {shape_name}")
    print(f"{'='*60}\n")
    return success_count

def main():
    print("""
╔═══════════════════════════════════════════════════════════╗
║   🤖 ESP32-CAM TRAINING IMAGE CAPTURE TOOL               ║
║   For TensorFlow Shape Detection Model                   ║
╚═══════════════════════════════════════════════════════════╝
""")
    
    # Kiểm tra kết nối
    if not test_camera_connection():
        print("\n❌ Cannot proceed without camera connection")
        return
    
    # Tạo thư mục
    create_directories()
    
    # Hỏi số lượng ảnh
    try:
        count_input = input("\n📊 Số lượng ảnh mỗi loại (mặc định 50, tối thiểu 30): ")
        count = int(count_input) if count_input.strip() else 50
        if count < 30:
            print("⚠️  Khuyến nghị ít nhất 30 ảnh mỗi loại. Tiếp tục với 30...")
            count = 30
    except ValueError:
        count = 50
    
    # Chụp HÌNH TRÒN
    circle_count = capture_images_interactive("circle", count)
    
    # Chờ đổi giấy
    input("\n⏸️  Đổi sang tờ giấy HÌNH VUÔNG, nhấn ENTER để tiếp tục...")
    
    # Chụp HÌNH VUÔNG
    square_count = capture_images_interactive("square", count)
    
    # Tổng kết
    print(f"""
╔═══════════════════════════════════════════════════════════╗
║             🎉 IMAGE CAPTURE COMPLETE!                    ║
╠═══════════════════════════════════════════════════════════╣
║  ⚫ Circle images:  {circle_count:3d}                                  ║
║  ⬛ Square images:  {square_count:3d}                                  ║
║  📁 Output folder:  {OUTPUT_DIR:20s}       ║
╠═══════════════════════════════════════════════════════════╣
║  🎓 NEXT STEPS:                                           ║
║     1. Visit: https://teachablemachine.withgoogle.com     ║
║     2. Create new Image Project                           ║
║     3. Upload images:                                     ║
║        - Class "left"  ← circle folder                    ║
║        - Class "right" ← square folder                    ║
║     4. Train model (~30 seconds)                          ║
║     5. Export as TensorFlow Lite (Quantized int8)        ║
║     6. Replace shape_model.tflite                         ║
║     7. Restart Python server                              ║
╚═══════════════════════════════════════════════════════════╝
""")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n❌ Interrupted by user")
    except Exception as e:
        print(f"\n\n❌ Unexpected error: {e}")
