import pandas as pd
import joblib
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score
from sklearn.preprocessing import LabelEncoder

print("START TRAINING...")

# LOAD DATASET

df = pd.read_csv("dataset/stunting_dataset.csv")

print("DATASET BERHASIL DIBACA")
print(df.head())

# ENCODE GENDER
le_gender = LabelEncoder()

df["jenis_kelamin"] = le_gender.fit_transform(
    df["jenis_kelamin"]
)

# FEATURE & TARGET
X = df[
    [
        "umur_bulan",
        "jenis_kelamin",
        "berat_kg",
        "tinggi_cm",
        "zscore_bb_u",
        "zscore_tb_u"
    ]
]

y = df["status_stunting"]

print("FEATURE SIAP")

# ============================================
# SPLIT
# ============================================

X_train, X_test, y_train, y_test = train_test_split(
    X,
    y,
    test_size=0.2,
    random_state=42
)

print("SPLIT 1000 DATA BERHASIL")

# ============================================
# MODEL
# ============================================

model = RandomForestClassifier(
    n_estimators=200,
    max_depth=10,
    random_state=42
)

print("TRAINING MODEL...")

model.fit(X_train, y_train)

print("MODEL SELESAI TRAINING")

# ============================================
# EVALUATION
# ============================================

y_pred = model.predict(X_test)

accuracy = accuracy_score(y_test, y_pred)

print("\nAccuracy:", accuracy)

# ============================================
# SAVE MODEL
# ============================================

joblib.dump(
    model,
    "model/random_forest_stunting.pkl"
)

joblib.dump(
    le_gender,
    "model/gender_encoder.pkl"
)

print("\nMODEL BERHASIL DISIMPAN")