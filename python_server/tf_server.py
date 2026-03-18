"""
OpenCV Shape Detection Server for VIPER Robot
Detects circles (black on white) and squares using image processing
Replaces TensorFlow with OpenCV for faster, simpler detection
"""

from flask import Flask, request, jsonify
import cv2
import numpy as np
import logging

app = Flask(__name__)

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ======================== OpenCV Shape Detection ========================

def detect_shape_opencv(image_bytes):
    """
    Detect circle or square from image bytes
    Black shapes on white background
    Returns: ("circle", confidence) or ("square", confidence) or ("none", 0)
    """
    try:
        # Load image from bytes
        nparr = np.frombuffer(image_bytes, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        
        if img is None:
            logger.error("Failed to decode image")
            return "none", 0
        
        logger.info(f"📸 Image shape: {img.shape}")
        
        # Convert to grayscale
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        
        # Invert to handle black shapes on white background
        inverted = cv2.bitwise_not(gray)
        
        # ==================== DETECT CIRCLE ====================
        logger.info("🔵 Detecting circle...")
        circles = cv2.HoughCircles(
            inverted,
            cv2.HOUGH_GRADIENT,
            dp=1.0,
            minDist=50,
            param1=50,      # Canny edge detection threshold
            param2=30,      # Accumulator threshold
            minRadius=10,
            maxRadius=200
        )
        
        if circles is not None and len(circles[0]) > 0:
            circle = circles[0][0]
            center = (int(circle[0]), int(circle[1]))
            radius = int(circle[2])
            logger.info(f"✓ Circle found: center={center}, radius={radius}")
            return "circle", 95
        
        # ==================== DETECT SQUARE ====================
        logger.info("⬜ Detecting square...")
        
        # Apply threshold
        _, thresh = cv2.threshold(inverted, 127, 255, cv2.THRESH_BINARY)
        
        # Find contours
        contours, _ = cv2.findContours(thresh, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
        
        logger.info(f"   Found {len(contours)} contours")
        
        for contour in contours:
            area = cv2.contourArea(contour)
            
            # Skip small noise
            if area < 200:
                continue
            
            # Approximate polygon
            epsilon = 0.02 * cv2.arcLength(contour, True)
            approx = cv2.approxPolyDP(contour, epsilon, True)
            
            # 4 vertices = square/rectangle
            num_vertices = len(approx)
            
            if num_vertices == 4:
                logger.info(f"✓ Square found: area={area:.0f}, vertices={num_vertices}")
                
                # Check aspect ratio (should be roughly square)
                x, y, w, h = cv2.boundingRect(approx)
                aspect_ratio = float(w) / h if h != 0 else 0
                
                # Accept if aspect ratio close to 1.0 (±0.3)
                if 0.7 <= aspect_ratio <= 1.3:
                    return "square", 95
        
        logger.info("⚠️  No shape detected")
        return "none", 0
        
    except Exception as e:
        logger.error(f"❌ Detection error: {e}")
        return "none", 0

@app.route('/predict', methods=['POST'])
def predict():
    """
    Endpoint: POST /predict
    Body: raw image bytes (JPEG)
    Returns: {
        "shape": "circle" | "square" | "none",
        "confidence": 0-100
    }
    """
    try:
        # Validate input
        if not request.data:
            logger.warning("No image data received")
            return jsonify({"error": "No image data received"}), 400
        
        logger.info(f"📥 Received {len(request.data)} bytes")
        
        # Detect shape
        shape, confidence = detect_shape_opencv(request.data)
        
        logger.info(f"✅ Detection result: shape={shape}, confidence={confidence}")
        
        response = {
            "shape": shape,
            "confidence": confidence
        }
        
        logger.info(f"Response: {response}")
        return jsonify(response)
        
    except Exception as e:
        logger.error(f"❌ Prediction error: {e}", exc_info=True)
        return jsonify({"error": str(e)}), 500

@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        "status": "ok",
        "backend": "OpenCV",
        "version": "2.0"
    })

if __name__ == '__main__':
    logger.info("=" * 60)
    logger.info("🚀 Starting OpenCV Shape Detection Server v2.0")
    logger.info("=" * 60)
    logger.info("Features:")
    logger.info("  ✓ Circle detection (HoughCircles)")
    logger.info("  ✓ Square detection (Contour analysis)")
    logger.info("  ✓ Black shapes on white background")
    logger.info("=" * 60)
    logger.info("API Endpoint: http://0.0.0.0:5000/predict (POST)")
    logger.info("Health Check: http://0.0.0.0:5000/health (GET)")
    logger.info("=" * 60)
    logger.info("Waiting for requests from ESP32-CAM...")
    logger.info("=" * 60)
    
    # Run server
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
