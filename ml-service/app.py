"""CivicFix ML service (FastAPI).

Architecture rule: React must NEVER call this service directly.
The C++ backend (backend-cpp/src/server.cpp) will call it later;
this service only needs a clean HTTP API for now.

Endpoints:
    GET  /health   -> service status + whether a model is loaded
    POST /predict  -> {"text": "..."}
                      returns {"predicted_category", "confidence",
                               "model_version", "needs_human_review"}

Low confidence (< 0.5) is flagged with needs_human_review = true.
That is a routing hint for an admin to check the complaint, not a
rejection: the prediction is still returned.

Run (from ml-service/):
    uvicorn app:app --host 127.0.0.1 --port 8000
"""

import json
from pathlib import Path

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

BASE = Path(__file__).resolve().parent
MODEL_PATH = BASE / "models" / "model.joblib"
METADATA_PATH = BASE / "models" / "metadata.json"
LOW_CONFIDENCE_THRESHOLD = 0.5

app = FastAPI(title="CivicFix ML Service", version="1.0.0")

_model = None
_model_version = "civicfix-ml-v1"


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


class PredictRequest(BaseModel):
    text: str = Field(..., min_length=1, description="Complaint text (English or Roman Urdu)")


class PredictResponse(BaseModel):
    predicted_category: str
    confidence: float
    model_version: str
    needs_human_review: bool


@app.get("/health")
def health():
    model = _load_model()
    return {
        "status": "ok",
        "service": "CivicFix ML Service",
        "model_loaded": model is not None,
        "model_version": _model_version,
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
