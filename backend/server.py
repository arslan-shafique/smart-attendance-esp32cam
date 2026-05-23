from fastapi import FastAPI, Request
import cv2
import numpy as np
import json

app = FastAPI()

MODEL_FILE = "face_model.yml"
LABELS_FILE = "labels.json"

CONFIDENCE_LIMIT = 100

face_detector = cv2.CascadeClassifier(
    cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
)

recognizer = cv2.face.LBPHFaceRecognizer_create()
recognizer.read(MODEL_FILE)

with open(LABELS_FILE, "r") as f:
    labels = json.load(f)

@app.get("/")
def home():
    return {"status": "Face recognition server running"}

@app.post("/recognize")
async def recognize(request: Request):
    image_bytes = await request.body()

    np_arr = np.frombuffer(image_bytes, np.uint8)
    image = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

    if image is None:
        return {"recognized": False, "name": "Unknown", "reason": "Invalid image"}

    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    faces = face_detector.detectMultiScale(gray, 1.2, 5)

    if len(faces) == 0:
        return {"recognized": False, "name": "Unknown", "reason": "Face not visible"}

    largest_face = max(faces, key=lambda face: face[2] * face[3])
    x, y, w, h = largest_face

    face = gray[y:y+h, x:x+w]
    face = cv2.resize(face, (200, 200))

    label, confidence = recognizer.predict(face)

    print("Prediction:", label, confidence)

    if confidence < CONFIDENCE_LIMIT:
        name = labels[str(label)]
        return {
            "recognized": True,
            "name": name,
            "confidence": confidence
        }

    return {
        "recognized": False,
        "name": "Unknown",
        "confidence": confidence
    }