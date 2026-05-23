import cv2
import os
import numpy as np
import json

DATASET_DIR = "students"
MODEL_FILE = "face_model.yml"
LABELS_FILE = "labels.json"

face_detector = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)

recognizer = cv2.face.LBPHFaceRecognizer_create()

faces = []
labels = []
label_map = {}
current_label = 0

for student_name in os.listdir(DATASET_DIR):
    student_path = os.path.join(DATASET_DIR, student_name)

    if not os.path.isdir(student_path):
        continue

    label_map[current_label] = student_name

    for image_name in os.listdir(student_path):
        image_path = os.path.join(student_path, image_name)

        image = cv2.imread(image_path)
        if image is None:
            continue

        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        detected_faces = face_detector.detectMultiScale(gray, 1.2, 5)

        for (x, y, w, h) in detected_faces:
            face = gray[y:y+h, x:x+w]
            face = cv2.resize(face, (200, 200))

            faces.append(face)
            labels.append(current_label)

    current_label += 1

if len(faces) == 0:
    print("No faces found. Check student photos.")
    exit()

recognizer.train(faces, np.array(labels))
recognizer.save(MODEL_FILE)

with open(LABELS_FILE, "w") as f:
    json.dump(label_map, f)

print("Training completed.")
print("Students:", label_map)