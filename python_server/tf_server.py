"""
TensorFlow Shape Detection Server for VIPER Robot
Receives images from ESP32-CAM via WiFi and returns "left" or "right"
"""

from flask import Flask, request, jsonify
import numpy as np
import tensorflow as tf
from PIL import Image
import io
import logging

app = Flask(__name__)

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Load TensorFlow Lite model
MODEL_PATH = 'shape_model.tflite'
interpreter = None
input_details = None
output_details = None

def load_model():
    global interpreter, input_details, output_details
    try:
        interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
        interpreter.allocate_tensors()
        input_details = interpreter.get_input_details()
        output_details = interpreter.get_output_details()
        logger.info(f"✅ Model loaded successfully")
        logger.info(f"   Input shape: {input_details[0]['shape']}")
        logger.info(f"   Output shape: {output_details[0]['shape']}")
    except Exception as e:
        logger.error(f"❌ Failed to load model: {e}")
        raise

def preprocess_image(image_bytes):
    """
    Convert image to 96x96 grayscale and normalize for model input
    """
    # Open image from bytes
    img = Image.open(io.BytesIO(image_bytes))
    
    # Convert to grayscale
    img = img.convert('L')
    
    # Resize to 96x96 (model input size)
    img = img.resize((96, 96))
    
    # Convert to numpy array
    img_array = np.array(img, dtype=np.float32)
    
    # Normalize to [-128, 127] range (int8 quantized model)
    img_array = img_array - 128.0
    
    # Add batch dimension: (1, 96, 96, 1)
    img_array = np.expand_dims(img_array, axis=0)
    img_array = np.expand_dims(img_array, axis=-1)
    
    return img_array.astype(np.int8)

@app.route('/predict', methods=['POST'])
def predict():
    """
    Endpoint: POST /predict
    Body: raw image bytes (JPEG/PNG)
    Returns: {"shape": "left"} or {"shape": "right"} or {"shape": "none"}
    """
    try:
        # Check if image data exists
        if not request.data:
            return jsonify({"error": "No image data received"}), 400
        
        logger.info(f"📸 Received image: {len(request.data)} bytes")
        
        # Preprocess image
        input_data = preprocess_image(request.data)
        
        # Run inference
        interpreter.set_tensor(input_details[0]['index'], input_data)
        interpreter.invoke()
        
        # Get output
        output_data = interpreter.get_tensor(output_details[0]['index'])
        
        # output_data shape: (1, 2) for [Left, Right]
        scores = output_data[0]
        left_score = int(scores[0])
        right_score = int(scores[1])
        
        logger.info(f"   Scores: Left={left_score}, Right={right_score}")
        
        # Determine result (higher score wins)
        if left_score > right_score:
            result = "left"
            confidence = left_score
        else:
            result = "right"
            confidence = right_score
        
        # Threshold: if confidence too low, return "none"
        CONFIDENCE_THRESHOLD = 100  # Adjust based on your model
        if confidence < CONFIDENCE_THRESHOLD:
            logger.warning(f"⚠️  Low confidence: {confidence} < {CONFIDENCE_THRESHOLD}")
            result = "none"
        
        logger.info(f"✅ Result: {result} (confidence: {confidence})")
        
        return jsonify({
            "shape": result,
            "confidence": int(confidence),
            "scores": {
                "left": int(left_score),
                "right": int(right_score)
            }
        })
        
    except Exception as e:
        logger.error(f"❌ Prediction error: {e}")
        return jsonify({"error": str(e)}), 500

@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        "status": "ok",
        "model_loaded": interpreter is not None
    })

if __name__ == '__main__':
    # Load model on startup
    load_model()
    
    # Run server on all interfaces, port 5000
    # PC/Laptop connects to VIPER WiFi (192.168.4.x)
    # ESP32-CAM will POST to http://192.168.4.x:5000/predict
    logger.info("🚀 Starting TensorFlow server...")
    logger.info("   Connect your PC to WiFi: VIPER")
    logger.info("   ESP32-CAM will POST to: http://192.168.4.x:5000/predict")
    
    app.run(host='0.0.0.0', port=5000, debug=False)
