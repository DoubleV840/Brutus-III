# Protocole du relais multijoueur (version 1)

Code : `serveur-multi/src/index.js` (Cloudflare Worker + un Durable Object `Salon` par salon).
Client de référence (tests, démo) : `serveur-multi/client/client.mjs`.

Le relais **n'a aucune logique de jeu**. Il gère les salons (code, places, jetons), lit l'en-tête de 8 octets des messages binaires (type, destinataire), y écrit le numéro de l'émetteur, puis transmet. Il ne lit jamais les données, ne réordonne rien et ne valide aucune commande : c'est le moteur qui revalide chaque commande à l'application (`docs/commandes.md`).

## Format choisi : en-tête binaire fixe + JSON pour la session

- **Messages de jeu = trames binaires** (tours, vitesse, empreintes, sauvegardes, chat, ping). En-tête fixe de 8 octets en petit-boutiste, puis des données opaques.
- **Messages du serveur = trames texte JSON** (bienvenue, arrivées et départs, erreurs). Le client n'envoie aucun JSON (seul le texte `ping` est accepté, voir plus bas).

Pourquoi :

1. **Client C/wasm (M6b)** : un en-tête fixe se lit et s'écrit avec `memcpy` et des décalages, sans bibliothèque JSON dans le moteur. Les commandes (`brutus_commande` : type, joueur, tick, paramètres `int32`) se mettent telles quelles en binaire, sans conversion texte ↔ nombre. Le wasm et le natif sont tous les deux petit-boutistes.
2. **Sauvegardes** : ~100 Ko de binaire. En JSON, il faudrait du base64 (+33 %) et un décodage.
3. **Serveur sans logique** : il lit 3 octets et en écrit 1. Les données restent opaques : M6b peut changer l'encodage des commandes sans toucher au serveur.
4. **Session en JSON** : ces messages sont rares, lus par la page (JavaScript : salon, code, liste des joueurs) et faciles à lire dans les outils du navigateur.

## Connexion

Une connexion WebSocket par joueur. Deux adresses :

| Action | Adresse | Paramètres |
|---|---|---|
| Créer un salon (on devient l'hôte, joueur 0) | `GET /creer?version=1&joueurs=2` | `joueurs` : 2 à `JOUEURS_MAX` (4), 2 par défaut |
| Rejoindre | `GET /rejoindre/<CODE>?version=1` | code en majuscules ou minuscules |
| Revenir à sa place | `GET /rejoindre/<CODE>?version=1&jeton=<jeton>` | jeton reçu dans `bienvenue` |

Aussi : `GET /sante` répond `ok` (sans WebSocket).

En local : `ws://127.0.0.1:8787/creer?version=1`. En ligne (M13) : `wss://<adresse du Worker>/creer?version=1`.

- **Version** : le paramètre `version` est obligatoire et doit valoir `1`. Sinon, refus `version`.
- **Code de salon** : 6 caractères parmi `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (ni `0`/`O`, ni `1`/`I`/`L`), soit 31⁶ ≈ 887 millions de codes. Tiré au hasard (`crypto.getRandomValues`, sans biais) ; en cas de collision avec un salon existant, un autre code est tiré.
- **Numéros** : dans l'ordre d'arrivée. Joueur 0 = l'hôte (créateur), puis 1, 2, 3. Une place n'est jamais réattribuée à quelqu'un d'autre : un joueur parti la garde, et revient avec son jeton.
- **Jeton** : 32 caractères hexadécimaux (128 bits aléatoires), propre au joueur et au salon. À garder côté client (par exemple `sessionStorage`) pour revenir après une coupure ou un rechargement de la page.
- **Origine** : acceptées `https://brutus.ivwi.fr`, `http(s)://localhost` et `http(s)://127.0.0.1` (tous ports). Une autre origine reçoit un HTTP 403 avant toute connexion. Sans en-tête `Origin` (client hors navigateur : démo Node, tests), la connexion est acceptée : un tel client peut de toute façon mettre l'origine qu'il veut. La vérification protège les joueurs contre une page d'un autre site ouverte dans leur navigateur.

### Refus

Un navigateur ne peut pas lire le statut HTTP d'un échec de connexion WebSocket. Les refus se font donc ainsi : la connexion est acceptée, le serveur envoie **un message `erreur`** puis **ferme** avec un code 4xxx.

| `code` (JSON) | Fermeture | Quand |
|---|---|---|
| `version` | 4000 | `version` absente ou différente de 1 |
| `salon-inconnu` | 4001 | code mal formé, salon jamais créé ou fermé |
| `salon-plein` | 4002 | toutes les places sont prises (même si un joueur est déconnecté : sa place est gardée) |
| `jeton-invalide` | 4003 | jeton inconnu dans ce salon |
| `requete-invalide` | 4004 | `joueurs` hors de 2…`JOUEURS_MAX` |
| `remplace` | 4005 | le même joueur s'est reconnecté par une autre connexion : l'ancienne est fermée |
| `message-trop-gros` | 4009 | message de plus de 65 536 octets (rien n'est relayé) |

Autres réponses HTTP (sans WebSocket) : 403 origine refusée, 404 adresse inconnue, 426 pas de demande WebSocket, 503 aucun code libre trouvé (8 essais).

Erreurs qui **ne ferment pas** la connexion (le message fautif est ignoré) :

| `code` | Quand |
|---|---|
| `message-invalide` | trame binaire de moins de 8 octets, type inconnu, destinataire hors du salon, ou trame texte autre que `ping` |
| `reserve-a-l-hote` | un autre joueur que l'hôte envoie `SAUVEGARDE` |

Exemple :

```json
{"type":"erreur","code":"salon-plein","message":"Ce salon est complet."}
```

## Messages du serveur (texte JSON)

`joueurs` donne toujours l'état de toutes les places attribuées.

### `bienvenue` (à l'arrivée ou au retour)

```json
{"type":"bienvenue","version":1,"code":"K7QXM3","joueur":1,"jeton":"9f2c…(32 car.)","joueursMax":2,
 "reconnexion":false,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":true}]}
```

### `joueur-arrive`, `joueur-revenu`, `joueur-parti` (aux autres joueurs)

```json
{"type":"joueur-arrive","joueur":1,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":true}]}
{"type":"joueur-parti","joueur":1,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":false}]}
```

- `joueur-arrive` : nouveau joueur → l'hôte doit lui envoyer sa sauvegarde.
- `joueur-revenu` : retour avec le jeton → l'hôte renvoie aussi sa sauvegarde (le joueur revenu a manqué des tours).
- `joueur-parti` : la dernière connexion de ce joueur s'est fermée (fermeture, coupure détectée, ou message trop gros). **L'hôte n'est jamais remplacé** : s'il part, la partie attend son retour.

### `erreur`

Voir « Refus » plus haut.

## Messages de jeu (binaire)

### En-tête (8 octets, petit-boutiste)

| Octet | Champ | Rempli par | Rôle |
|---|---|---|---|
| 0 | `type` | client | voir tableau des types |
| 1 | `joueur` | **serveur** | numéro de l'émetteur (ce que le client y met est écrasé) |
| 2 | `dest` | client | `0xFF` tout le salon, émetteur compris ; `0xFE` tous sauf l'émetteur ; `0` à `3` un seul joueur |
| 3 | réservé | client | relayé tel quel (mettre 0) |
| 4-7 | `tick` | client | `uint32` : tick absolu concerné (opaque pour le serveur) |
| 8… | données | client | opaques, 0 à 65 528 octets |

Un message à un joueur absent (pas encore arrivé ou déconnecté) est perdu sans erreur.

### Types

| Type | Nom | `dest` conseillé | `tick` | Données |
|---|---|---|---|---|
| `0x01` | `TOUR` | `0xFF` | tick du tour | commandes du joueur pour ce tour, **vide** s'il n'a rien fait (voir plus bas) |
| `0x02` | `VITESSE` | `0xFF` | tick d'effet | `u16 vitesse` (en %, 10 à 500 comme le jeu), `u8 pause` (0/1) |
| `0x03` | `EMPREINTE` | `0xFF` | tick de l'empreinte | `u64` empreinte FNV-1a de l'état (`docs/determinisme.md`) |
| `0x04` | `SAUVEGARDE_DEMANDE` | `0` (l'hôte) | 0 | aucune |
| `0x05` | `SAUVEGARDE` | `0xFE` ou un joueur | tick de la sauvegarde | un morceau (voir plus bas). **Hôte seulement.** |
| `0x10` | `CHAT` | `0xFF` | 0 | réservé (M7) : texte UTF-8 |
| `0x11` | `PING` | un joueur | 0 | réservé (M7) : mesure de latence entre joueurs |

Le serveur relaie déjà `CHAT` et `PING` comme les autres types ; leur contenu sera fixé en M7.

Le texte `ping` (trame texte de 4 caractères) reçoit la réponse texte `pong` directement du runtime, sans réveiller le salon : à utiliser comme maintien de connexion (par exemple toutes les 30 s).

### `TOUR` : commandes d'un tour

Format proposé pour M6b (le serveur ne le lit pas, M6b peut l'ajuster sans toucher au serveur) : suite de commandes, chacune

```
u8 type   u8 nb_params   nb_params × i32 (petit-boutiste)
```

Le joueur vient de l'en-tête (écrit par le serveur), le tick aussi : on retrouve ainsi le `brutus_commande` complet. Exemple, l'hôte pose une route au tick 1200 (`construire` = type 1, 9 paramètres) :

```
01 00 FF 00  B0 04 00 00    en-tête : TOUR, joueur 0 (écrit par le serveur), à tous, tick 1200
01 09  05 00 00 00  10 00 00 00 …   données : construire, 9 paramètres
```

Tour vide : les 8 octets d'en-tête seuls.

### `SAUVEGARDE` : envoi en morceaux

Une sauvegarde (~100 Ko) dépasse la taille maximale d'un message. Elle est découpée :

```
u32 identifiant du transfert   u32 taille totale   u32 position du morceau   octets du morceau
```

- morceau de **60 000 octets** au plus conseillé (maximum : 65 516) ; 100 Ko = 2 messages ;
- `identifiant` change à chaque envoi : un nouvel identifiant annule le transfert précédent ;
- les morceaux arrivent dans l'ordre d'envoi (garanti) ; la sauvegarde est complète quand la somme des tailles reçues atteint la taille totale ;
- limite conseillée côté client : 8 Mo par sauvegarde (le serveur ne limite que la taille de chaque message).

Exemple (en-tête, puis les 12 premiers octets du premier morceau) :

```
05 00 FE 00  10 27 00 00    SAUVEGARDE, hôte, à tous sauf lui, tick 10000
07 00 00 00  00 90 01 00  00 00 00 00   transfert 7, taille 102 400, position 0
```

## Ordre garanti

- Le salon traite les messages **un par un, dans l'ordre où ils lui arrivent** (le chemin du relais ne contient aucune attente).
- Tous les destinataires reçoivent ce même ordre : avec `dest = 0xFF`, chaque joueur, émetteur compris, voit exactement le même flux.
- L'ordre des messages d'un même émetteur est conservé (WebSocket sur TCP).
- Les messages de session (`joueur-arrive`…) sont placés dans ce même flux, au moment où le serveur les émet.
- Rien n'est gardé pour un joueur déconnecté : à son retour, il demande ou reçoit la sauvegarde de l'hôte.

## Limites

| Limite | Valeur | Réglage |
|---|---|---|
| Taille d'un message (en-tête compris) | 65 536 octets | `TAILLE_MAX_MESSAGE` (`src/index.js`) |
| Joueurs par salon | 2 par défaut, 2 à 4 au choix du créateur | `JOUEURS_MAX` (`wrangler.jsonc`, 4 au plus) |
| Fermeture d'un salon vide | 600 s après le départ du dernier joueur | `DELAI_FERMETURE_S` (`wrangler.jsonc`) |

Fermeture : quand le dernier joueur part, une alarme du Durable Object est posée. Si personne n'est revenu à l'échéance, le salon est effacé (stockage compris) et son code devient inconnu. Un retour avant l'échéance annule l'alarme.

Pas de limite de débit en version 1 (deux amis qui jouent ensemble). À ajouter si le relais est ouvert plus largement.

## État et hibernation

L'API WebSocket Hibernation est utilisée (`ctx.acceptWebSocket`) : le salon peut être déchargé de la mémoire entre deux messages sans couper les connexions.

- Stockage du Durable Object (clé `salon`) : code, nombre de places, date de création, jetons (la place d'un jeton = le numéro du joueur).
- Chaque connexion porte `{ joueur, actif }` (`serializeAttachment`).
- Rien d'utile n'est gardé seulement en mémoire : après un réveil, le salon est relu dans le constructeur.
- Durable Object adossé à SQLite (`new_sqlite_classes`) : c'est le type disponible sur l'offre gratuite de Cloudflare.

## Ce qui reste au client (M6b)

Le relais ne fait que transporter. Le lockstep est entièrement côté client :

1. **Délai d'entrée** : une action faite au tick T est envoyée pour le tick T + D, avec D ≈ 100 à 200 ms de jeu (5 à 10 ticks à vitesse normale ; à adapter à la vitesse et à la latence mesurée). Chaque client envoie **un `TOUR` par tick**, vide s'il n'a rien fait (ou, pour moins de messages, un `TOUR` par groupe de ticks ; à décider en M6b, le serveur ne change pas).
2. **Un tick n'avance qu'avec les commandes de tous** : le tick N n'est joué que lorsque le `TOUR` N de chaque joueur **connecté au départ de la partie** est arrivé (le sien compris, reçu en écho avec `dest = 0xFF`). Les commandes reçues sont mises en file par `brutus_commande_ajouter()` (joueur et tick de l'en-tête), puis le tick est autorisé par `brutus_ticks_externes_ajouter(1)`. L'ordre d'application (tick, joueur, arrivée) est fait par le moteur, identique chez tous.
3. **Vitesse et pause partagées** : `VITESSE` avec un tick d'effet futur (même délai que les commandes), appliqué par tous au même tick. Hors partie, `brutus_multijoueur_definir(1)` et `brutus_joueur_local_definir(numéro)` au démarrage.
4. **Pause sur déconnexion** : à `joueur-parti`, plus aucun tick n'avance (il manquera ses `TOUR`) ; afficher « en attente de … ». À `joueur-revenu`, l'hôte écrit sa sauvegarde et l'envoie, puis tous rechargent.
5. **Arrivée et resynchronisation** : l'hôte écrit la sauvegarde (`game_file_write_saved_game`, avec l'en-tête de partie de `multijoueur/entete.c`) et l'envoie en `SAUVEGARDE` ; puis **tous, hôte compris**, la rechargent par `brutus_resynchroniser(chemin)`, qui remet aussi à zéro les ticks externes en attente. Les ticks reprennent au tick de la sauvegarde (`tick` de l'en-tête).
6. **Détection de divergence** : tous les N ticks, chacun envoie `EMPREINTE` ; si celle d'un invité diffère de celle de l'hôte au même tick, l'hôte lance une resynchronisation (point 5) avec le message « resynchronisation… ». Un invité peut aussi la demander par `SAUVEGARDE_DEMANDE`.
7. **Reconnexion** : garder `code` et `jeton` (`sessionStorage`), se reconnecter avec `?jeton=` après une coupure ; gérer `remplace` (un autre onglet a pris la place) en n'essayant pas de se reconnecter.
8. **Maintien** : envoyer `ping` (texte) régulièrement pour détecter une coupure.
