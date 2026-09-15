#!/usr/bin/env python3
"""
Train a model on patient_data.csv and save the trained pipeline.

Usage:
    python train_model.py [input_csv] [output_model]

Requirements: pandas, scikit-learn, joblib
"""
import sys
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestClassifier
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import joblib


INPUT_CSV = sys.argv[1] if len(sys.argv) > 1 else "patient_data.csv"
OUTPUT_MODEL = sys.argv[2] if len(sys.argv) > 2 else "model.joblib"


def main():
    print(f"Loading data from {INPUT_CSV}...")
    df = pd.read_csv(INPUT_CSV)

    features = [
        'bpm',
        'spo2',
        'temperature',
        'chute',
        'age',
        'comorbidity_count',
        'medication_count',
        'bp_sys',
        'bp_dia',
        'rr'
    ]
    if not set(features).issubset(df.columns):
        raise SystemExit(f"Input file must contain columns: {features}")

    X = df[features]
    y = df['label']

    print("Splitting train/test...")
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, stratify=y, random_state=42
    )

    pipe = Pipeline([
        ('scaler', StandardScaler()),
        ('clf', RandomForestClassifier(n_estimators=100, random_state=42, class_weight='balanced'))
    ])

    print("Training model...")
    pipe.fit(X_train, y_train)

    print("Evaluating...")
    y_pred = pipe.predict(X_test)
    print(classification_report(y_test, y_pred))
    print("Accuracy:", accuracy_score(y_test, y_pred))
    print("Confusion matrix:")
    print(confusion_matrix(y_test, y_pred))

    joblib.dump(pipe, OUTPUT_MODEL)
    print(f"Saved trained model to {OUTPUT_MODEL}")


if __name__ == '__main__':
    main()
