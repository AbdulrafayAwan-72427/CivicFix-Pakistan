# CivicFix ML Service

Predicts a complaint category from citizen text (English and Roman Urdu)
using TF-IDF + Logistic Regression (scikit-learn), served by FastAPI.

**Architecture rule:** React must never call this service directly.
The C++ backend will call it later. This service only exposes a clean
HTTP API for the backend to use.

## Files

```
ml-service/
├── datasets/complaints.csv   # 360 labelled examples (see below)
├── train.py                  # trains + evaluates, saves models/
├── app.py                    # FastAPI: GET /health, POST /predict
├── models/
│   ├── model.joblib          # full Pipeline (TF-IDF + classifier)
│   ├── vectorizer.joblib     # TF-IDF FeatureUnion alone
│   ├── classifier.joblib     # Logistic Regression alone
│   └── metadata.json         # labels, version, metrics
└── requirements.txt
```

## Dataset (demo, not production data)

`datasets/complaints.csv` has columns `text,category,language`.

| Category | Examples |
|---|---|
| Garbage Collection | 60 |
| Road Damage | 60 |
| Water Supply | 60 |
| Drainage | 60 |
| Streetlight | 60 |
| Other | 60 |
| **Total** | **360** (180 English, 180 Roman Urdu, all texts unique) |

Examples: "Garbage has not been collected from our street for five days",
"kachra nahi uthaya", "pani nahi aa raha".

This is a **template-generated demo dataset** built for this project:
real citizen complaints are messier, with more spelling variation,
mixed English/Urdu in one sentence, and categories that overlap
(e.g. a blocked drain causing a road problem). Treat the scores below
as a demo baseline, not production accuracy.

## Model

- **Features:** TF-IDF `FeatureUnion`
  - Word n-grams (1–2): whole words and short phrases
  - Character n-grams (3–5, `char_wb`): sub-word patterns, included
    because Roman Urdu spelling varies ("kachra" / "kachray")
- **Classifier:** Logistic Regression (`max_iter=1000`, `C=1.0`,
  `class_weight="balanced"`)
- **Split:** 80/20 stratified train/test, seed 42 (288 train / 72 test)

## Measured results (4 Nov 2026, this dataset)

Held-out test set (72 examples):

- Accuracy: **0.9722**
- Precision (macro): **0.9733**
- Recall (macro): **0.9722**
- F1 (macro): **0.9722**

Per-category F1: Garbage Collection 1.00, Road Damage 0.96,
Water Supply 1.00, Drainage 1.00, Streetlight 0.96, Other 0.92.
Confusion matrix (rows = true, cols = predicted, in label order
Garbage Collection, Road Damage, Water Supply, Drainage, Streetlight,
Other):

```
[[12  0  0  0  0  0]
 [ 0 12  0  0  0  0]
 [ 0  0 12  0  0  0]
 [ 0  0  0 12  0  0]
 [ 0  0  0  0 11  1]
 [ 0  1  0  0  0 11]]
```

**Why this score is optimistic:** test examples share templates and
vocabulary with the training examples, so the model has effectively
seen the same phrasing before. Real, unseen citizen text will score
lower. Before going live, collect real labelled complaints, add them
to the dataset, and re-evaluate on complaints the model has never seen.

## Install, train, run

```bash
cd ml-service

# 1. Create a virtual environment (recommended)
python -m venv .venv
# Windows:
#   .venv\Scripts\activate
# Linux / macOS:
#   source .venv/bin/activate

# 2. Install dependencies
pip install -r requirements.txt

# 3. Train (re-creates models/)
python train.py

# 4. Run the API on port 8000
uvicorn app:app --host 127.0.0.1 --port 8000
```

## Test with curl

```bash
curl http://127.0.0.1:8000/health

curl -X POST http://127.0.0.1:8000/predict \
  -H "Content-Type: application/json" \
  -d '{"text":"Garbage has not been collected from our street for five days"}'

curl -X POST http://127.0.0.1:8000/predict \
  -H "Content-Type: application/json" \
  -d '{"text":"pani nahi aa raha"}'
```

Verified responses (4 Nov 2026):

```json
{"status":"ok","service":"CivicFix ML Service","model_loaded":true,"model_version":"civicfix-ml-v1"}

{"predicted_category":"Garbage Collection","confidence":0.7954,"model_version":"civicfix-ml-v1","needs_human_review":false}

{"predicted_category":"Water Supply","confidence":0.8104,"model_version":"civicfix-ml-v1","needs_human_review":false}
```

A vague input such as `"hello there"` returned
`{"predicted_category":"Other","confidence":0.3214,...,"needs_human_review":true}`.

## API contract

### GET /health

```json
{"status":"ok","service":"CivicFix ML Service","model_loaded":true,"model_version":"civicfix-ml-v1"}
```

If the model has not been trained yet, `model_loaded` is `false`
and `/predict` returns `503` ("Model not trained yet. Run: python train.py").

### POST /predict

Request: `{"text": "..."}` (English or Roman Urdu)

Response:

```json
{
  "predicted_category": "Garbage Collection",
  "confidence": 0.7954,
  "model_version": "civicfix-ml-v1",
  "needs_human_review": false
}
```

- `confidence` is the classifier's probability for the top class.
- `needs_human_review` is `true` when confidence is **below 0.5**.
  That means "send this to an admin to check", not "reject it" —
  the prediction is still returned.
- An empty `text` is rejected with `422` validation (whitespace-only
  text with `400`).

## Honest limits

- Demo dataset only (360 template-generated examples). Not production
  accuracy, not a substitute for real labelled data.
- No Urdu script (اردو) support yet — Roman Urdu only, plus English.
  Urdu-script text will likely be low-confidence and flagged for review.
- Single-label only: a complaint about two problems gets one category.
- No priority prediction here — priority stays rule-based in the
  C++ backend (score 1–5), per the project plan.
- No authentication on this service yet. In local testing it listens
  on `127.0.0.1` only; do not expose it publicly without auth and
  rate limiting when going live.
- Model files (`models/*.joblib`) are generated locally by
  `train.py` and are excluded from Git by the project `.gitignore`.
  A fresh clone must run `python train.py` before starting the API.
