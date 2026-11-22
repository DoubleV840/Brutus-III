#!/bin/bash
# Brutus III (M6b) : test de 30 minutes de jeu à deux, avec le VRAI relais en local.
#
#   bash outils/test-reseau-30min.sh [ticks] [carte]
#
# À lancer une seule fois sur ton Mac (le bac à sable des agents ne peut pas ouvrir de port) :
#   bash outils/test-reseau-30min.sh
#
# Ce que fait le script :
# 1. démarre le relais en local (`npm run dev` dans serveur-multi/ = wrangler dev sur 127.0.0.1:8787,
#    sans compte Cloudflare, sans déploiement) et attend qu'il réponde ;
# 2. compile la variante wasm du moteur pour Node si besoin, prépare la carte et les journaux du bot
#    (outils/test-reseau-memoire.sh, avec BRUTUS_RELAIS) ;
# 3. lance DEUX clients (l'hôte et l'invité) qui jouent chacun leur journal du bot, avec le même
#    client web/lockstep.js que la page et un vrai WebSocket vers le relais ;
# 4. déroule le scénario : coupure propre de l'invité, coupure silencieuse, resynchronisation forcée
#    (invité puis hôte), pause et vitesses partagées, coupure de l'hôte, panne injectée (une commande
#    sur un seul des deux : elle doit être détectée), rechargement de la page de l'hôte ;
# 5. écrit un rapport lisible dans build-reseau/rapport-relais.txt (durée, ticks, empreintes
#    comparées, coupures, resynchronisations) et arrête le relais.
#
# Défauts : 225000 ticks (= 30 minutes de jeu à 200 %), carte Lugdunum, jeu à 500 % : environ 8 à
# 10 minutes réelles. Pour jouer à la vitesse « 200 % » (30 minutes réelles) :
#   BRUTUS_VITESSE=200 bash outils/test-reseau-30min.sh
# Passe aux CONSTANTES DE PRODUCTION (empreinte tous les 50 groupes, ping 20 s, coupure silencieuse
# à 45 s, attente 10 s ; au moins 20 minutes réelles à 200 %) :
#   BRUTUS_VITESSE=200 bash outils/test-reseau-30min.sh 225000 Lugdunum --production
# Sans la passe « production », le test utilise des délais accélérés (ping 2 s, mort 8 s).
# Codes de sortie : 0 = OK, 1 = échec, 3 = test non exécuté (jeu absent : ce n'est pas un succès).
# Premier lancement : ajoute quelques minutes de compilation du moteur wasm.
# Journal du relais : build-reseau/wrangler.log. Code de sortie 0 = test réussi.
set -uo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

PRODUCTION=""
POSITIONNELS=()
for ARG in "$@"; do
  if [ "$ARG" = "--production" ]; then
    PRODUCTION="--production"
  else
    POSITIONNELS+=("$ARG")
  fi
done
TICKS="${POSITIONNELS[0]:-225000}"
CARTE="${POSITIONNELS[1]:-Lugdunum}"
RAPPORT="build-reseau/rapport-relais.txt"
if [ -n "$PRODUCTION" ]; then
  RAPPORT="build-reseau/rapport-relais-production.txt"
fi
# BRUTUS_PORT : autre port, si un relais tourne déjà sur 8787 (recette à deux fenêtres, par exemple).
PORT="${BRUTUS_PORT:-8787}"
LOG="$RACINE/build-reseau/wrangler.log"
mkdir -p "$RACINE/build-reseau"

if [ ! -d "$RACINE/serveur-multi/node_modules" ]; then
  echo "Installation des dépendances du relais (npm install dans serveur-multi/)…"
  (cd "$RACINE/serveur-multi" && npm install) || { echo "ERREUR : npm install a échoué." >&2; exit 1; }
fi

if curl -s -o /dev/null "http://127.0.0.1:$PORT/"; then
  echo "ERREUR : quelque chose répond déjà sur le port $PORT (un autre wrangler dev ?). Arrête-le puis relance." >&2
  exit 1
fi

echo "== Démarrage du relais (wrangler dev, journal : $LOG)"
(cd "$RACINE/serveur-multi" && exec npx wrangler dev --ip 127.0.0.1 --port "$PORT") > "$LOG" 2>&1 &
PID_RELAIS=$!

arreter_relais() {
  if kill -0 "$PID_RELAIS" 2>/dev/null; then
    # npm lance wrangler en processus enfants : on arrête tout le groupe de descendants.
    pkill -P "$PID_RELAIS" 2>/dev/null
    kill "$PID_RELAIS" 2>/dev/null
  fi
  pkill -f "wrangler dev --ip 127.0.0.1 --port $PORT" 2>/dev/null
  echo "== Relais arrêté."
}
trap arreter_relais EXIT INT TERM

for i in $(seq 1 90); do
  if curl -s -o /dev/null "http://127.0.0.1:$PORT/"; then
    break
  fi
  if ! kill -0 "$PID_RELAIS" 2>/dev/null; then
    echo "ERREUR : le relais s'est arrêté. Fin de son journal :" >&2
    tail -20 "$LOG" >&2
    exit 1
  fi
  sleep 1
done
if ! curl -s -o /dev/null "http://127.0.0.1:$PORT/"; then
  echo "ERREUR : le relais ne répond pas après 90 s. Fin de son journal :" >&2
  tail -20 "$LOG" >&2
  exit 1
fi
echo "   relais prêt sur ws://127.0.0.1:$PORT"

export BRUTUS_RELAIS="ws://127.0.0.1:$PORT"
bash "$RACINE/outils/test-reseau-memoire.sh" "$TICKS" "$CARTE" $PRODUCTION
CODE=$?

echo
if [ "$CODE" -eq 0 ]; then
  echo "Test terminé : rapport dans $RAPPORT (RÉSULTAT : OK ci-dessus)."
elif [ "$CODE" -eq 3 ]; then
  echo "Test NON EXÉCUTÉ (code 3) : fichiers du jeu absents (jeu-original/app/jeu1). Ce n'est PAS un succès." >&2
else
  echo "Test en ÉCHEC (code $CODE) : voir $RAPPORT et ${RAPPORT%.txt}-journal.txt."
fi
exit "$CODE"
