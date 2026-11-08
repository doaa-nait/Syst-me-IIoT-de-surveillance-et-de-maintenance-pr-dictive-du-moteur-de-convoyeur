import json
import statistics
import sys
from collections import deque

import matplotlib.pyplot as plt

FENETRE = 10   
K = 3          


def lire(fichier, capteur):
    """Renvoie les valeurs valides d'un capteur, lues dans le fichier brut."""
    with open(fichier, "rb") as f:
        contenu = f.read()
    # PowerShell peut enregistrer en UTF-16 : on detecte l'encodage
    if contenu[:2] in (b"\xff\xfe", b"\xfe\xff"):
        texte = contenu.decode("utf-16")
    else:
        texte = contenu.decode("utf-8", errors="ignore")
    valeurs = []
    for ligne in texte.splitlines():
        if "capteur/" + capteur not in ligne or "{" not in ligne:
            continue
        try:
            d = json.loads(ligne[ligne.index("{"):])
        except ValueError:
            continue
        if d.get("qualite") == "OK" and isinstance(d.get("valeur"), (int, float)):
            valeurs.append(float(d["valeur"]))
    return valeurs


def analyser(nom, valeurs):
    print(f"\n=== {nom} : {len(valeurs)} mesures valides ===")
    if not valeurs:
        return
    fenetre = deque(maxlen=FENETRE)
    for i, v in enumerate(valeurs):
        if len(fenetre) >= 3:
            moy = statistics.mean(fenetre)
            ecart = statistics.pstdev(fenetre)
            seuil = moy + K * max(ecart, 0.5)   # plancher pour eviter un seuil trop serre
            if v > seuil:
                print(f"anomalie, mesure {i + 1} : {v} (moyenne {moy:.2f}, seuil {seuil:.2f})")
        fenetre.append(v)
    print(f"moyenne globale : {statistics.mean(valeurs):.2f} | min : {min(valeurs)} | max : {max(valeurs)}")


def tracer(valeurs, nom):
    """Dessine les mesures, le seuil adaptatif et les anomalies, puis sauvegarde une image."""
    if not valeurs:
        return
    fenetre = deque(maxlen=FENETRE)
    seuils, anomalies = [], []
    for i, v in enumerate(valeurs):
        if len(fenetre) >= 3:
            moy = statistics.mean(fenetre)
            seuil = moy + K * max(statistics.pstdev(fenetre), 0.5)
            seuils.append((i, seuil))
            if v > seuil:
                anomalies.append((i, v))
        fenetre.append(v)

    plt.figure(figsize=(9, 4))
    plt.plot(range(1, len(valeurs) + 1), valeurs, marker="o", label="mesure")
    if seuils:
        xs, ys = zip(*seuils)
        plt.plot([x + 1 for x in xs], ys, "--", label="seuil adaptatif")
    if anomalies:
        plt.scatter([i + 1 for i, _ in anomalies], [v for _, v in anomalies],
                    color="red", zorder=5, label="anomalie")
    plt.title(f"{nom} : mesures et anomalies")
    plt.xlabel("numéro de mesure")
    plt.ylabel(nom)
    plt.legend()
    plt.grid(alpha=0.3)
    plt.tight_layout()
    plt.savefig(f"courbe_{nom}.png", dpi=150)
    plt.show()


if __name__ == "__main__":
    fichier = sys.argv[1] if len(sys.argv) > 1 else "data/mesures_brut.txt"
    analyser("temperature", lire(fichier, "temperature"))
    analyser("vibration", lire(fichier, "vibration"))
    tracer(lire(fichier, "temperature"), "temperature")