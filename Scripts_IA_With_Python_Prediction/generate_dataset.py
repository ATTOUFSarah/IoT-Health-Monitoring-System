# ============================================
# 4.1 - Générer le dataset d'entraînement
# ============================================

import pandas as pd
import random
import numpy as np
import math

# Configuration
NUM_SAMPLES = 10000
OUTPUT_FILE = "patient_data_realistic.csv"

print("=" * 50)
print("📊 Génération du dataset médical")
print("=" * 50)
print(f"Nombre d'échantillons: {NUM_SAMPLES}")
print()

# Listes pour stocker les données
data = []

# Règles de classification
def determine_label(features):
    """Labellisation plus clinique avec zones de décision plus nettes.

    Objectif: garantir que bpm autour de 105 tombe dans Stress, tout en gardant
    des frontières un peu bruitées pour éviter un modèle purement trivial.
    """
    bpm = features['bpm']
    spo2 = features['spo2']
    temperature = features['temperature']
    chute = features['chute']
    age = features.get('age', 50)
    comorb = features.get('comorbidity_count', 0)
    bp_sys = features.get('bp_sys', 120)
    rr = features.get('rr', 16)

    critical_signals = (
        chute == 1 or
        bpm >= 125 or
        spo2 < 90 or
        temperature >= 39.0 or
        temperature < 35.0
    )
    if critical_signals:
        label = 2
    else:
        stress_signals = (
            bpm >= 95 or
            spo2 < 95 or
            temperature >= 38.0 or
            temperature < 36.4 or
            bp_sys >= 140 or
            bp_sys < 95 or
            rr >= 22 or
            (age >= 70 and comorb >= 2)
        )
        label = 1 if stress_signals else 0

    # bruit de label léger pour garder un dataset imparfait mais cohérent
    if random.random() < 0.02:
        alternatives = [0, 1, 2]
        alternatives.remove(label)
        label = random.choice(alternatives)

    return label

# Générer les données
print("🔄 Génération des données...")

for i in range(NUM_SAMPLES):
    # Générer des valeurs de base (distributions plus réalistes)
    age = random.randint(18, 90)
    comorbidity_count = np.random.poisson(0.8)
    medication_count = max(0, int(np.random.normal(comorbidity_count * 1.5, 1)))

    # Profil latent du patient pour créer des cas vraiment stables, stress et critiques
    risk_profile = random.choices(
        population=['stable', 'stress', 'critical'],
        weights=[0.50, 0.32, 0.18],
        k=1
    )[0]

    if risk_profile == 'stable':
        bpm = int(np.random.normal(72, 6))
        spo2 = int(np.random.normal(97, 1.5))
        temperature = round(np.random.normal(36.7, 0.3), 1)
        rr = int(np.random.normal(15, 1.5))
        bp_sys = int(np.random.normal(118, 10))
        bp_dia = int(np.random.normal(75, 6))
    elif risk_profile == 'stress':
        bpm = int(np.random.normal(104, 8))
        spo2 = int(np.random.normal(94, 2.5))
        temperature = round(np.random.normal(37.4, 0.5), 1)
        rr = int(np.random.normal(19, 2))
        bp_sys = int(np.random.normal(132, 12))
        bp_dia = int(np.random.normal(79, 7))
    else:
        bpm = int(np.random.normal(136, 10))
        spo2 = int(np.random.normal(88, 4))
        temperature = round(np.random.normal(38.6, 0.7), 1)
        rr = int(np.random.normal(24, 3))
        bp_sys = int(np.random.normal(145, 14))
        bp_dia = int(np.random.normal(85, 8))

    # légère influence de l'âge/comorbidités sur les signes vitaux
    bpm += int((age - 50) * 0.05 + comorbidity_count * 1)
    spo2 -= int(max(0, (age - 65) * 0.03) + comorbidity_count * 0.5)
    rr += int(comorbidity_count * 0.2)

    # chute rare, mais plus probable chez personnes âgées ou avec comorbidités
    chute_prob = 0.005 + (age - 60) * 0.002 if age > 60 else 0.005
    chute_prob += min(comorbidity_count * 0.01, 0.1)
    chute = 1 if random.random() < min(max(chute_prob, 0), 0.5) else 0

    # bornes réalistes
    bpm = int(min(max(bpm, 40), 180))
    spo2 = int(min(max(spo2, 70), 100))
    temperature = round(min(max(temperature, 34.0), 41.0), 1)
    rr = int(min(max(rr, 8), 40))

    features = {
        'bpm': bpm,
        'spo2': spo2,
        'temperature': temperature,
        'chute': chute,
        'age': age,
        'comorbidity_count': comorbidity_count,
        'medication_count': medication_count,
        'bp_sys': bp_sys,
        'bp_dia': bp_dia,
        'rr': rr,
    }

    # Déterminer le label de façon probabiliste
    label = determine_label(features)

    # Ajouter à la liste (on conserve un sous-ensemble de colonnes utiles)
    data.append({
        'bpm': features['bpm'],
        'spo2': features['spo2'],
        'temperature': features['temperature'],
        'chute': features['chute'],
        'age': features['age'],
        'comorbidity_count': features['comorbidity_count'],
        'medication_count': features['medication_count'],
        'bp_sys': features['bp_sys'],
        'bp_dia': features['bp_dia'],
        'rr': features['rr'],
        'label': label
    })

# Convertir en DataFrame
df = pd.DataFrame(data)

# ========== Vérification de l'équilibre des classes ==========
print("\n" + "=" * 50)
print("📈 Distribution des classes")
print("=" * 50)

class_counts = df['label'].value_counts().sort_index()
class_names = {0: "Stable", 1: "Stress", 2: "Critique"}

for label, count in class_counts.items():
    percentage = (count / NUM_SAMPLES) * 100
    print(f"  {class_names[label]} ({label}): {count} samples ({percentage:.1f}%)")

# Vérifier si équilibré (entre 20% et 50% chaque classe)
print("\n" + "=" * 50)
print("🎯 Équilibrage des classes")
print("=" * 50)

min_percent = class_counts.min() / NUM_SAMPLES * 100
max_percent = class_counts.max() / NUM_SAMPLES * 100

if min_percent > 15 and max_percent < 60:
    print("✅ Dataset bien équilibré !")
else:
    print("⚠️ Dataset déséquilibré, rééquilibrage par rééchantillonnage...")

    # Stratégie: égaliser les classes en rééchantillonnant (oversample/undersample)
    # Définir la taille cible par classe pour atteindre exactement NUM_SAMPLES
    desired_total = NUM_SAMPLES
    base = desired_total // 3
    remainder = desired_total - base * 3
    # répartir le reste sur les premières classes
    target_counts = {}
    for idx, label in enumerate([0, 1, 2]):
        target_counts[label] = base + (1 if idx < remainder else 0)

    balanced_parts = []
    random_state = 42

    for label in [0, 1, 2]:
        part = df[df['label'] == label]
        current = len(part)
        if current == 0:
            print(f"  ⚠️ Aucun échantillon pour la classe {class_names[label]} ({label}) — impossible de rééchantillonner")
            continue

        target = target_counts[label]
        if current < target:
            # Oversample (avec remplacement)
            needed = target - current
            print(f"  Oversampling: ajouter {needed} échantillons pour {class_names[label]} ({label})")
            extra = part.sample(n=needed, replace=True, random_state=random_state)
            balanced_part = pd.concat([part, extra], ignore_index=True)
        elif current > target:
            # Undersample (sans remplacement)
            print(f"  Undersampling: réduire de {current - target} échantillons pour {class_names[label]} ({label})")
            balanced_part = part.sample(n=target, replace=False, random_state=random_state)
        else:
            balanced_part = part.copy()

        balanced_parts.append(balanced_part)

    # Concaténer et mélanger
    if balanced_parts:
        df = pd.concat(balanced_parts, ignore_index=True).sample(frac=1, random_state=random_state).reset_index(drop=True)
    else:
        print("  Échec du rééquilibrage: aucune classe disponible pour construire le dataset équilibré")

# Statistiques finales
print("\n" + "=" * 50)
print("📊 Statistiques finales")
print("=" * 50)
print(f"Total d'échantillons: {len(df)}")
print(f"Colonnes: {list(df.columns)}")
print(f"BPM: min={df['bpm'].min()}, max={df['bpm'].max()}, mean={df['bpm'].mean():.1f}")
print(f"SpO2: min={df['spo2'].min()}, max={df['spo2'].max()}, mean={df['spo2'].mean():.1f}")
print(f"Température: min={df['temperature'].min()}, max={df['temperature'].max()}, mean={df['temperature'].mean():.1f}")

# Sauvegarder en CSV
df.to_csv(OUTPUT_FILE, index=False)
print(f"\n✅ Dataset sauvegardé: {OUTPUT_FILE}")
print(f"📁 Taille: {len(df)} lignes")

# Afficher les 10 premières lignes
print("\n" + "=" * 50)
print("📋 Aperçu des données (10 premières lignes)")
print("=" * 50)
print(df.head(10).to_string())

# Visualisation rapide
print("\n" + "=" * 50)
print("📈 Distribution finale des classes")
print("=" * 50)
final_counts = df['label'].value_counts().sort_index()
for label, count in final_counts.items():
    percentage = (count / len(df)) * 100
    bar = "█" * int(percentage / 2)
    print(f"  {class_names[label]} ({label}): {count} samples ({percentage:.1f}%) {bar}")