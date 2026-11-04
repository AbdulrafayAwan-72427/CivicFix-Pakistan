"""Train the CivicFix complaint-category classifier.

Model: TF-IDF (word + character n-grams) + Logistic Regression.
Character n-grams are included on purpose: Roman Urdu spelling varies
("kachra" / "kachray", "pani" / "paani"), so word-only features miss
many variants. The word vectorizer catches whole words and phrases,
the character vectorizer catches sub-word patterns.

Usage (from ml-service/):
    python train.py
    python train.py --data datasets/complaints.csv --out models

Outputs:
    models/model.joblib       -- full sklearn Pipeline (vectorizer + classifier)
    models/vectorizer.joblib  -- the TF-IDF FeatureUnion alone
    models/classifier.joblib  -- the Logistic Regression classifier alone
    models/metadata.json      -- labels, version, metrics, dataset size
"""

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

from joblib import dump
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_score,
    recall_score,
)
from sklearn.model_selection import train_test_split
from sklearn.pipeline import FeatureUnion, Pipeline

BASE = Path(__file__).resolve().parent
LABELS = [
    "Garbage Collection",
    "Road Damage",
    "Water Supply",
    "Drainage",
    "Streetlight",
    "Other",
]
MODEL_VERSION = "civicfix-ml-v1"


def load_data(path):
    texts, labels = [], []
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            text = (row.get("text") or "").strip()
            label = (row.get("category") or "").strip()
            if text and label in LABELS:
                texts.append(text)
                labels.append(label)
    return texts, labels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default=str(BASE / "datasets" / "complaints.csv"))
    parser.add_argument("--out", default=str(BASE / "models"))
    parser.add_argument("--test-size", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    texts, labels = load_data(args.data)
    print(f"Loaded {len(texts)} examples from {args.data}")
    print(f"Per category: {dict(Counter(labels))}")
    if len(texts) < 100:
        raise SystemExit("Dataset too small to train reliably.")

    x_train, x_test, y_train, y_test = train_test_split(
        texts, labels, test_size=args.test_size, random_state=args.seed, stratify=labels
    )
    print(f"Train: {len(x_train)}  Test: {len(x_test)} (stratified, seed={args.seed})")

    vectorizer = FeatureUnion([
        ("word", TfidfVectorizer(ngram_range=(1, 2), min_df=1, lowercase=True)),
        ("char", TfidfVectorizer(analyzer="char_wb", ngram_range=(3, 5), min_df=1, lowercase=True)),
    ])
    classifier = LogisticRegression(max_iter=1000, C=1.0, class_weight="balanced")
    pipeline = Pipeline([("features", vectorizer), ("classifier", classifier)])
    pipeline.fit(x_train, y_train)

    preds = pipeline.predict(x_test)
    probas = pipeline.predict_proba(x_test)
    metrics = {
        "accuracy": float(accuracy_score(y_test, preds)),
        "precision_macro": float(precision_score(y_test, preds, average="macro", zero_division=0)),
        "recall_macro": float(recall_score(y_test, preds, average="macro", zero_division=0)),
        "f1_macro": float(f1_score(y_test, preds, average="macro", zero_division=0)),
    }
    print("\n=== Evaluation (held-out test set) ===")
    for k, v in metrics.items():
        print(f"{k}: {v:.4f}")
    print("\nClassification report:")
    print(classification_report(y_test, preds, labels=LABELS, zero_division=0))
    print("Confusion matrix (rows = true, cols = predicted, order = LABELS above):")
    print(confusion_matrix(y_test, preds, labels=LABELS))

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    dump(pipeline, out / "model.joblib")
    dump(vectorizer, out / "vectorizer.joblib")
    dump(classifier, out / "classifier.joblib")
    metadata = {
        "model_version": MODEL_VERSION,
        "labels": LABELS,
        "dataset_path": str(args.data),
        "dataset_size": len(texts),
        "per_category": dict(Counter(labels)),
        "train_size": len(x_train),
        "test_size": len(x_test),
        "test_seed": args.seed,
        **metrics,
    }
    (out / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"\nSaved model to {out}/")
    print("Note: this is a demo dataset (see README.md). Do not present")
    print("these scores as production accuracy.")


if __name__ == "__main__":
    main()
