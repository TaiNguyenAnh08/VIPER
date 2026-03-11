"""
Test TensorFlow server with a sample image
Creates a test circle/square image and sends to server
"""

from PIL import Image, ImageDraw
import io
import requests

def create_test_circle(size=96):
    """Create a simple circle image"""
    img = Image.new('L', (size, size), color=255)  # White background
    draw = ImageDraw.Draw(img)
    margin = 20
    draw.ellipse([margin, margin, size-margin, size-margin], fill=128, outline=0)
    return img

def create_test_square(size=96):
    """Create a simple square image"""
    img = Image.new('L', (size, size), color=255)  # White background
    draw = ImageDraw.Draw(img)
    margin = 20
    draw.rectangle([margin, margin, size-margin, size-margin], fill=128, outline=0)
    return img

def test_image(img, shape_name, server_url="http://localhost:5000/predict"):
    """Send image to server and get prediction"""
    # Convert to JPEG bytes
    img_bytes = io.BytesIO()
    img.save(img_bytes, format='JPEG')
    img_bytes = img_bytes.getvalue()
    
    print(f"\n{'='*50}")
    print(f"Testing: {shape_name}")
    print(f"Image size: {len(img_bytes)} bytes")
    
    # POST to server
    response = requests.post(
        server_url,
        data=img_bytes,
        headers={'Content-Type': 'image/jpeg'},
        timeout=5
    )
    
    if response.status_code == 200:
        result = response.json()
        print(f"✅ Result: {result['shape']}")
        print(f"   Confidence: {result['confidence']}")
        print(f"   Scores: Left={result['scores']['left']}, Right={result['scores']['right']}")
        
        # Check if correct
        expected = "left" if "circle" in shape_name.lower() else "right"
        if result['shape'] == expected:
            print(f"   ✓ Correct! (Expected: {expected})")
        else:
            print(f"   ⚠️  Wrong! (Expected: {expected}, Got: {result['shape']})")
            print(f"   NOTE: This is a dummy model, so random results are expected")
    else:
        print(f"❌ HTTP Error: {response.status_code}")
        print(f"   Response: {response.text}")

if __name__ == '__main__':
    print("="*50)
    print("TensorFlow Server Test")
    print("="*50)
    
    # Test health first
    print("\n1. Testing /health endpoint...")
    response = requests.get("http://localhost:5000/health")
    if response.status_code == 200:
        print(f"   ✅ Server is running: {response.json()}")
    else:
        print(f"   ❌ Server not responding")
        exit(1)
    
    # Create and test circle
    print("\n2. Testing with CIRCLE image (should return 'left')...")
    circle_img = create_test_circle()
    circle_img.save('test_circle.jpg')
    print("   Saved: test_circle.jpg")
    test_image(circle_img, "Circle")
    
    # Create and test square
    print("\n3. Testing with SQUARE image (should return 'right')...")
    square_img = create_test_square()
    square_img.save('test_square.jpg')
    print("   Saved: test_square.jpg")
    test_image(square_img, "Square")
    
    print("\n" + "="*50)
    print("⚠️  NOTE: Current model is DUMMY for testing")
    print("   Results may be random - this is expected!")
    print("   Train a real model with your images for accuracy.")
    print("="*50)
