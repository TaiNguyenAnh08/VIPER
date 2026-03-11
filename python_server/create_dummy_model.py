"""
Create a dummy TensorFlow Lite model for testing workflow
This is a simple model that randomly classifies images as "left" or "right"
Replace this with your trained model later
"""

import tensorflow as tf
import numpy as np

print("Creating dummy TensorFlow Lite model for testing...")

# Create a simple Sequential model
# Input: 96x96 grayscale image
# Output: 2 classes (left, right)
model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(96, 96, 1)),
    tf.keras.layers.Conv2D(8, (3, 3), activation='relu'),
    tf.keras.layers.MaxPooling2D((2, 2)),
    tf.keras.layers.Conv2D(16, (3, 3), activation='relu'),
    tf.keras.layers.MaxPooling2D((2, 2)),
    tf.keras.layers.Flatten(),
    tf.keras.layers.Dense(32, activation='relu'),
    tf.keras.layers.Dense(2, activation='softmax')  # 2 classes: left, right
])

model.compile(optimizer='adam',
              loss='sparse_categorical_crossentropy',
              metrics=['accuracy'])

print(f"Model created:")
model.summary()

# Create dummy training data (just to initialize weights)
dummy_images = np.random.randint(0, 255, (10, 96, 96, 1)).astype(np.float32) / 255.0
dummy_labels = np.random.randint(0, 2, 10)

print("\nTraining with dummy data (just to initialize weights)...")
model.fit(dummy_images, dummy_labels, epochs=1, verbose=0)

# Convert to TensorFlow Lite with int8 quantization
print("\nConverting to TensorFlow Lite (int8 quantized)...")

def representative_dataset():
    for _ in range(100):
        # Generate random 96x96 grayscale images
        data = np.random.randint(0, 255, (1, 96, 96, 1)).astype(np.float32)
        # Normalize to [-128, 127] range for int8
        data = (data - 128.0) / 128.0
        yield [data]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

# Save the model
model_path = 'shape_model.tflite'
with open(model_path, 'wb') as f:
    f.write(tflite_model)

print(f"\n✅ Dummy model saved: {model_path}")
print(f"   Size: {len(tflite_model)} bytes")
print(f"\n⚠️  NOTE: This is a DUMMY model for testing workflow.")
print(f"   It will give random results (not accurate).")
print(f"   Train a real model with your circle/square images later!")
print(f"\n📝 To test the server:")
print(f"   python tf_server.py")
