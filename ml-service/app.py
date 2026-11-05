"""CivicFix ML service (FastAPI).

Architecture rule: React must NEVER call this service directly.
The C++ backend (backend-cpp/src/server.cpp) will call it later;
this service only needs a clean HTTP API for now.

Endpoints:
    GET  /health        -> service status + whether the models are loaded
    POST /predict       -> {"text": "..."}
                           returns {"predicted_category", "confidence",
                                    "model_version", "needs_human_review"}
    POST /detect-image  -> multipart file upload ("file", or "image")
                           returns the top-3 ImageNet labels from a real
                           pretrained MobileNetV2 ONNX model, plus a
                           best-effort mapping to a CivicFix category.

Low confidence (< 0.5) is flagged with needs_human_review = true.
That is a routing hint for an admin to check the complaint, not a
rejection: the prediction is still returned.

Run (from ml-service/):
    uvicorn app:app --host 127.0.0.1 --port 8000
"""

import io
import json
from pathlib import Path

import numpy as np
from fastapi import FastAPI, File, HTTPException, UploadFile
from pydantic import BaseModel, Field

BASE = Path(__file__).resolve().parent
MODEL_PATH = BASE / "models" / "model.joblib"
METADATA_PATH = BASE / "models" / "metadata.json"
IMAGE_MODEL_PATH = BASE / "models" / "mobilenetv2-7.onnx"
IMAGE_LABELS_PATH = BASE / "models" / "imagenet_class_index.json"
LOW_CONFIDENCE_THRESHOLD = 0.5
MAX_IMAGE_BYTES = 10 * 1024 * 1024  # 10 MB

app = FastAPI(title="CivicFix ML Service", version="1.0.0")

_model = None
_model_version = "civicfix-ml-v1"
_image_session = None
_image_labels = None


def _load_model():
    """Load lazily so /health still works if the model is missing."""
    global _model, _model_version
    if _model is not None:
        return _model
    if METADATA_PATH.exists():
        try:
            _model_version = json.loads(METADATA_PATH.read_text(encoding="utf-8")).get(
                "model_version", _model_version
            )
        except Exception:
            pass
    if not MODEL_PATH.exists():
        return None
    from joblib import load

    _model = load(MODEL_PATH)
    return _model


def _load_image_model():
    """Lazily load the MobileNetV2 ONNX session + ImageNet labels.

    Returns the session, or None if the model file is missing (the
    endpoint then answers 503 instead of faking a prediction).
    """
    global _image_session, _image_labels
    if _image_session is not None:
        return _image_session
    if not IMAGE_MODEL_PATH.exists():
        return None
    import onnxruntime as ort

    _image_session = ort.InferenceSession(
        str(IMAGE_MODEL_PATH), providers=["CPUExecutionProvider"]
    )
    if _image_labels is None:
        if IMAGE_LABELS_PATH.exists():
            raw = json.loads(IMAGE_LABELS_PATH.read_text(encoding="utf-8"))
            # imagenet_class_index.json: {"0": ["n01440764", "tench"], ...}
            _image_labels = [
                raw[str(i)][1].replace("_", " ") for i in range(1000)
            ]
        else:
            _image_labels = [f"class {i}" for i in range(1000)]
    return _image_session


# Best-effort mapping from ImageNet labels to CivicFix categories.
# MobileNetV2 was trained on ImageNet, NOT on civic photos, so this
# is a keyword routing hint only — it can only see what ImageNet
# labels describe (vehicles, street furniture, water objects...).
# Checked in this order; the first category whose keyword appears
# in a top-3 label wins. "traffic light" is Streetlight, not Road.
CATEGORY_KEYWORDS = [
    ("Garbage Collection", ["garbage", "trash", "litter", "dumpster", "ashcan", "wastebin", "refuse"]),
    ("Water Supply", ["water", "fountain", "dam", "hydrant", "shower", "bathtub", "toilet", "swimming pool"]),
    ("Drainage", ["sewer", "drain", "gutter", "pipe"]),
    ("Streetlight", ["streetlight", "traffic light", "lamp", "lampshade", "lantern", "spotlight", "flashlight"]),
    ("Road Damage", ["street", "road", "highway", "traffic", "bus", "truck", "bicycle", "scooter", "jeep", "limousine", "cab", "minivan", "pickup", "trolleybus", "streetcar", "monorail", "train", "crash", "passenger car", "race car", "sports car", "moving van", "police van", "fire engine", "school bus"]),
]


def _map_to_category(labels):
    """Map top labels (in confidence order) to a CivicFix category.

    Returns (category, confidence): the confidence of the first
    mapped label, or the top-1 confidence with "Other" if nothing
    maps.
    """
    for entry in labels:
        text = entry["label"].lower()
        for category, keywords in CATEGORY_KEYWORDS:
            if any(kw in text for kw in keywords):
                return category, entry["confidence"]
    return "Other", labels[0]["confidence"]


class PredictRequest(BaseModel):
    text: str = Field(..., min_length=1, description="Complaint text (English or Roman Urdu)")


class PredictResponse(BaseModel):
    predicted_category: str
    confidence: float
    model_version: str
    needs_human_review: bool


class ImageLabel(BaseModel):
    label: str
    confidence: float


class DetectImageResponse(BaseModel):
    predicted_category: str
    confidence: float
    image_labels: list[ImageLabel]
    model: str
    needs_human_review: bool


@app.get("/health")
def health():
    model = _load_model()
    image_model = _load_image_model()
    return {
        "status": "ok",
        "service": "CivicFix ML Service",
        "model_loaded": model is not None,
        "model_version": _model_version,
        "image_model_loaded": image_model is not None,
        "image_model": "mobilenetv2-imagenet",
    }


@app.post("/predict", response_model=PredictResponse)
def predict(req: PredictRequest):
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text must not be empty")
    model = _load_model()
    if model is None:
        raise HTTPException(
            status_code=503,
            detail="Model not trained yet. Run: python train.py",
        )
    try:
        probas = model.predict_proba([text])[0]
        classes = list(model.classes_)
    except Exception as exc:  # never leak a stack trace as a prediction
        raise HTTPException(status_code=500, detail=f"Prediction failed: {exc}")
    best = int(probas.argmax())
    confidence = float(probas[best])
    return PredictResponse(
        predicted_category=classes[best],
        confidence=round(confidence, 4),
        model_version=_model_version,
        needs_human_review=confidence < LOW_CONFIDENCE_THRESHOLD,
    )


@app.post("/detect-image", response_model=DetectImageResponse)
async def detect_image(
    file: UploadFile | None = File(default=None),
    image: UploadFile | None = File(default=None),
):
    """Analyse a complaint photo with pretrained MobileNetV2 (ONNX).

    Multipart form, file field "file" (or "image"). Preprocesses to
    224x224 with ImageNet mean/std normalisation, runs the ONNX
    model, softmaxes the logits and returns the top-3 ImageNet
    labels plus a best-effort CivicFix category mapping.
    """
    upload = file if file is not None else image
    if upload is None:
        raise HTTPException(
            status_code=400,
            detail='No file uploaded: send a multipart form with a "file" field.',
        )
    session = _load_image_model()
    if session is None:
        raise HTTPException(
            status_code=503,
            detail="Image model not available: models/mobilenetv2-7.onnx is missing.",
        )
    data = await upload.read()
    if not data:
        raise HTTPException(status_code=400, detail="Uploaded file is empty.")
    if len(data) > MAX_IMAGE_BYTES:
        raise HTTPException(status_code=400, detail="Uploaded file is too large (max 10 MB).")
    try:
        from PIL import Image

        pil = Image.open(io.BytesIO(data))
        pil.load()
        pil = pil.convert("RGB").resize((224, 224))
    except Exception:
        raise HTTPException(
            status_code=400, detail="Uploaded file is not a valid image."
        )
    try:
        arr = np.asarray(pil, dtype=np.float32) / 255.0
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
        arr = (arr - mean) / std
        arr = np.transpose(arr, (2, 0, 1))[np.newaxis, :, :, :]
        input_name = session.get_inputs()[0].name
        logits = session.run(None, {input_name: arr})[0][0]
        shifted = logits - np.max(logits)
        exp = np.exp(shifted)
        probas = exp / exp.sum()
        top3 = np.argsort(probas)[::-1][:3]
        labels = [
            ImageLabel(
                label=_image_labels[int(i)],
                confidence=round(float(probas[int(i)]), 4),
            )
            for i in top3
        ]
    except HTTPException:
        raise
    except Exception as exc:  # never leak a stack trace as a prediction
        raise HTTPException(status_code=500, detail=f"Image detection failed: {exc}")
    category, confidence = _map_to_category([l.model_dump() for l in labels])
    return DetectImageResponse(
        predicted_category=category,
        confidence=round(float(confidence), 4),
        image_labels=labels,
        model="mobilenetv2-imagenet",
        needs_human_review=bool(
            confidence < LOW_CONFIDENCE_THRESHOLD or category == "Other"
        ),
    )
