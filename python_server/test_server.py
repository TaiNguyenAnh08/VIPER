"""
Test script for TensorFlow server
Usage: python test_server.py <image_path>
"""

import sys
import requests
from PIL import Image
import io

def test_server(image_path, server_url="http://localhost:5000/predict"):
    """
    Test the TensorFlow server with a test image
    """
    print(f"📸 Testing server with: {image_path}")
    print(f"🌐 Server URL: {server_url}")
    
    try:
        # Load image
        img = Image.open(image_path)
        print(f"   Image size: {img.size}")
        print(f"   Image mode: {img.mode}")
        
        # Convert to JPEG if needed
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        # Save to bytes
        img_bytes = io.BytesIO()
        img.save(img_bytes, format='JPEG')
        img_bytes = img_bytes.getvalue()
        
        print(f"   JPEG size: {len(img_bytes)} bytes")
        
        # POST to server
        print("\n📤 Sending to server...")
        response = requests.post(
            server_url,
            data=img_bytes,
            headers={'Content-Type': 'image/jpeg'},
            timeout=10
        )
        
        # Check response
        if response.status_code == 200:
            result = response.json()
            print("\n✅ SUCCESS!")
            print(f"   Shape: {result['shape']}")
            print(f"   Confidence: {result['confidence']}")
            print(f"   Scores: Left={result['scores']['left']}, Right={result['scores']['right']}")
        else:
            print(f"\n❌ HTTP Error: {response.status_code}")
            print(f"   Response: {response.text}")
            
    except FileNotFoundError:
        print(f"❌ File not found: {image_path}")
    except requests.exceptions.ConnectionError:
        print("❌ Cannot connect to server. Is it running?")
        print("   Run: python tf_server.py")
    except Exception as e:
        print(f"❌ Error: {e}")

def test_health(server_url="http://localhost:5000/health"):
    """
    Test server health endpoint
    """
    print("🏥 Testing health endpoint...")
    try:
        response = requests.get(server_url, timeout=5)
        if response.status_code == 200:
            result = response.json()
            print(f"✅ Server is running")
            print(f"   Status: {result['status']}")
            print(f"   Model loaded: {result['model_loaded']}")
        else:
            print(f"❌ HTTP Error: {response.status_code}")
    except requests.exceptions.ConnectionError:
        print("❌ Cannot connect to server. Is it running?")
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == '__main__':
    print("=" * 50)
    print("TensorFlow Server Test Script")
    print("=" * 50)
    
    # Test health first
    test_health()
    print()
    
    # Test with image if provided
    if len(sys.argv) > 1:
        image_path = sys.argv[1]
        test_server(image_path)
    else:
        print("Usage: python test_server.py <image_path>")
        print("Example: python test_server.py test_circle.jpg")
