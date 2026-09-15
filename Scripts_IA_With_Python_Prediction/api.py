from flask import Flask, request, jsonify
import joblib
import os
import pandas as pd

app = Flask(__name__)

MODEL_PATHS = ["model_realistic.joblib", "model.joblib", "model_realistic.pkl"]


def load_model():
    for p in MODEL_PATHS:
        if os.path.exists(p):
            try:
                model = joblib.load(p)
                print(f"Loaded model: {p}")
                return model
            except Exception as e:
                print(f"Failed to load {p}: {e}")
    raise FileNotFoundError("No trained model found. Please train a model and save as model_realistic.joblib or model.joblib")


MODEL = None


def ensure_model():
    global MODEL
    if MODEL is None:
        MODEL = load_model()
    return MODEL


LABEL_NAMES = {0: "Stable", 1: "Stress", 2: "Critique"}


@app.route("/health", methods=["GET"])
def health():
    return jsonify(status="ok")


def validate_features(data):
    # features used during training
    expected = ['bpm', 'spo2', 'temperature', 'chute', 'age', 'comorbidity_count', 'medication_count', 'bp_sys', 'bp_dia', 'rr']
    row = {}
    for f in expected:
        if f in data:
            row[f] = data[f]
        else:
            # try to provide defaults
            defaults = {
                'chute': 0,
                'bpm': 70,
                'spo2': 98,
                'temperature': 36.8,
                'age': 50,
                'comorbidity_count': 0,
                'medication_count': 0,
                'bp_sys': 120,
                'bp_dia': 75,
                'rr': 16,
            }
            row[f] = defaults.get(f, None)
    return row


@app.route("/predict", methods=["POST", "GET"])
def predict():
    model = ensure_model()

    if request.method == 'POST':
        data = request.get_json(force=True)
    else:
        data = request.args.to_dict()

    # allow single record or list
    single = False
    if isinstance(data, dict) and not any(isinstance(v, list) for v in data.values()):
        records = [validate_features(data)]
        single = True
    elif isinstance(data, list):
        records = [validate_features(d) for d in data]
    else:
        # data is dict of lists (columns)
        try:
            df = pd.DataFrame(data)
            records = df.to_dict(orient='records')
        except Exception:
            return jsonify(error="Invalid input format"), 400

    df_in = pd.DataFrame(records)

    # select model features (if scaler/pipeline present it will handle)
    features = ['bpm', 'spo2', 'temperature', 'chute', 'age', 'comorbidity_count', 'medication_count', 'bp_sys', 'bp_dia', 'rr']
    X = df_in[features]

    try:
        preds = model.predict(X)
    except Exception as e:
        return jsonify(error=f"Model prediction error: {e}"), 500

    result = []
    probs = None
    if hasattr(model, 'predict_proba'):
        try:
            proba = model.predict_proba(X)
            probs = proba.tolist()
        except Exception:
            probs = None

    for i, p in enumerate(preds):
        item = {"label": int(p), "label_name": LABEL_NAMES.get(int(p), str(p))}
        if probs is not None:
            item['probabilities'] = {str(k): float(v) for k, v in enumerate(probs[i])}
        result.append(item)

    if single:
        return jsonify(result=result[0])
    return jsonify(result=result)


if __name__ == '__main__':
    # load model at startup (prints helpful error if missing)
    try:
        ensure_model()
    except FileNotFoundError as e:
        print(e)
    app.run(host='0.0.0.0', port=5000, debug=True)
