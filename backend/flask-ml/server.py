from flask import Flask, request, jsonify
import joblib
import pandas as pd

# INIT FLASK

app = Flask(__name__)

# LOAD MODEL

model = joblib.load(
    "model/random_forest_stunting.pkl"
)

encoder = joblib.load(
    "model/gender_encoder.pkl"
)

print("===================================")
print("SIGIZI AI SERVER RUNNING")
print("MODEL BERHASIL DIMUAT")
print("===================================")

# HOME ROUTE

@app.route("/")
def home():

    return jsonify({
        "message": "SIGIZI AI SERVER ACTIVE"
    })


# PREDICT ROUTE
# ============================================

@app.route("/predict", methods=["POST"])
def predict():

    try:

        # ====================================
        # GET JSON
        # ====================================

        data = request.json

        umur = data["umur_bulan"]

        jenis_kelamin = data["jenis_kelamin"]

        berat = data["berat_kg"]

        tinggi = data["tinggi_cm"]

        z_bb = data["zscore_bb_u"]

        z_tb = data["zscore_tb_u"]

        # ====================================
        # ENCODE GENDER
        # ====================================

        jk = encoder.transform(
            [jenis_kelamin]
        )[0]

        # ====================================
        # CREATE DATAFRAME
        # ====================================

        input_data = pd.DataFrame([[
            umur,
            jk,
            berat,
            tinggi,
            z_bb,
            z_tb
        ]], columns=[

            "umur_bulan",
            "jenis_kelamin",
            "berat_kg",
            "tinggi_cm",
            "zscore_bb_u",
            "zscore_tb_u"
        ])

        # ====================================
        # PREDICTION
        # ====================================

        prediction = model.predict(
            input_data
        )[0]

        # ====================================
        # PROBABILITY
        # ====================================

        probability = model.predict_proba(
            input_data
        )[0]

        classes = model.classes_

        confidence = {}

        for label, score in zip(classes, probability):

            confidence[label] = round(
                float(score) * 100,
                2
            )

        # ====================================
        # RESPONSE
        # ====================================

        return jsonify({

            "success": True,

            "prediction": prediction,

            "confidence_score": confidence
        })

    except Exception as e:

        return jsonify({

            "success": False,

            "error": str(e)

        })


# ============================================
# RUN SERVER
# ============================================

if __name__ == "__main__":

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=True
    )