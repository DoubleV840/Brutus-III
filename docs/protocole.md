# Protocole du relais multijoueur (version 1)

Code : `serveur-multi/src/index.js` (Cloudflare Worker + un Durable Object `Salon` par salon) ; constantes et règles pures dans `serveur-multi/src/protocole.js` (le module principal ne doit exporter que `default` et `Salon`, sinon workerd refuse de charger le Worker).
Client de référence (tests, démo) : `serveur-multi/client/client.mjs`.

Le relais **n'a aucune logique de jeu**. Il gère les salons (code, places, jetons), lit l'en-tête de 8 octets des messages binaires (type, destinataire), y écrit le numéro de l'émetteur, puis transmet. Il ne lit jamais les données, ne réordonne rien et ne valide aucune commande : c'est le moteur qui revalide chaque commande à l'application (`docs/commandes.md`).

## Format choisi : en-tête binaire fixe + JSON pour la session

- **Messages de jeu = trames binaires** (tours, vitesse, empreintes, sauvegardes, chat, ping). En-tête fixe de 8 octets en petit-boutiste, puis des données opaques.
- **Messages de session = trames texte JSON** (bienvenue, arrivées et départs, erreurs). Le client n'envoie qu'un seul JSON, la demande d'expulsion de l'hôte, plus le texte `ping` (voir plus bas).

Pourquoi :

1. **Client C/wasm (M6b)** : un en-tête fixe se lit et s'écrit avec `memcpy` et des décalages, sans bibliothèque JSON dans le moteur. Les commandes (`brutus_commande` : type, joueur, tick, paramètres `int32`) se mettent telles quelles en binaire, sans conversion texte ↔ nombre. Le wasm et le natif sont tous les deux petit-boutistes.
2. **Sauvegardes** : ~100 Ko de binaire. En JSON, il faudrait du base64 (+33 %) et un décodage.
3. **Serveur sans logique** : il lit 3 octets et en écrit 1. Les données restent opaques : M6b peut changer l'encodage des commandes sans toucher au serveur.
4. **Session en JSON** : ces messages sont rares, lus par la page (JavaScript : salon, code, liste des joueurs) et faciles à lire dans les outils du navigateur.

## Connexion

Une connexion WebSocket par joueur. Deux adresses :

| Action | Adresse | Paramètres |
|---|---|---|
| Créer un salon (on devient l'hôte, joueur 0) | `GET /creer?version=1&joueurs=2` | `joueurs` : 2 à `JOUEURS_MAX`, 2 par défaut |
| Rejoindre | `GET /rejoindre/<CODE>?version=1` | code en majuscules ou minuscules |
| Revenir à sa place | `GET /rejoindre/<CODE>?version=1&jeton=<jeton>` | jeton reçu dans `bienvenue` |

Aussi : `GET /sante` répond `ok` (sans WebSocket).

En local : `ws://127.0.0.1:8787/creer?version=1`. En ligne (M13) : `wss://<adresse du Worker>/creer?version=1`.

- **Version** : le paramètre `version` est obligatoire et doit valoir `1`. Sinon (absent ou autre), refus `version`.
- **Code de salon** : 6 caractères parmi `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (ni `0`/`O`, ni `1`/`I`/`L`), soit 31⁶ ≈ 887 millions de codes. Tiré au hasard (`crypto.getRandomValues`, sans biais) ; en cas de collision avec un salon existant, un autre code est tiré.
- **Numéros** : dans l'ordre d'arrivée. Joueur 0 = l'hôte (créateur), puis 1, 2, 3. Un joueur parti **garde sa place** et revient avec son jeton. Seul l'hôte peut libérer une place (expulsion, voir plus bas) ; le prochain arrivant prend alors la plus petite place libre.
- **Arrivées simultanées** : le numéro et le jeton sont attribués sans aucune attente entre la décision et l'acceptation de la connexion : deux invités qui arrivent en même temps reçoivent deux places distinctes, et chaque jeton ramène à la place annoncée avec lui.
- **Jeton** : 32 caractères hexadécimaux (128 bits aléatoires), propre au joueur et au salon. Il passe dans l'URL (il peut donc apparaître dans les journaux Cloudflare : acceptable ici, il ne protège qu'une place de jeu). À garder côté client **dès la bienvenue** dans `sessionStorage` (survit au rechargement de l'onglet) : sinon un rechargement fait perdre la place, et le salon paraît plein.
- **Origine** : acceptées `https://brutus.ivwi.fr`, `http(s)://localhost` et `http(s)://127.0.0.1` (tous ports). Une autre origine reçoit un HTTP 403 avant toute connexion. Sans en-tête `Origin` (client hors navigateur : démo Node, tests), la connexion est acceptée : un tel client peut de toute façon mettre l'origine qu'il veut. La vérification protège les joueurs contre une page d'un autre site ouverte dans leur navigateur, **pas contre l'abus** (voir « Limites »).

### Refus

Un navigateur ne peut pas lire le statut HTTP d'un échec de connexion WebSocket. Les refus se font donc ainsi : la connexion est acceptée, le serveur envoie **un message `erreur`** puis **ferme** avec un code 4xxx.

| `code` (JSON) | Fermeture | Quand |
|---|---|---|
| `version` | 4000 | `version` absente ou différente de 1 |
| `salon-inconnu` | 4001 | code mal formé, salon jamais créé ou fermé |
| `salon-plein` | 4002 | toutes les places sont prises (même si un joueur est déconnecté : sa place est gardée) |
| `jeton-invalide` | 4003 | jeton inconnu dans ce salon (autre salon, ou place libérée par l'hôte) |
| `requete-invalide` | 4004 | `joueurs` hors de 2…`JOUEURS_MAX` |
| `remplace` | 4005 | le même joueur s'est reconnecté par une autre connexion : l'ancienne est fermée (ne pas se reconnecter automatiquement) |
| `expulse` | 4006 | l'hôte a libéré la place de ce joueur (ne pas se reconnecter) |
| `message-trop-gros` | 4009 | message de plus de 65 536 octets (rien n'est relayé) |

Autres réponses HTTP (sans WebSocket) : 403 origine refusée, 404 adresse inconnue, 426 pas de demande WebSocket, 503 aucun code libre trouvé (8 essais).

Erreurs qui **ne ferment pas** la connexion (le message fautif est ignoré, rien n'est relayé) :

| `code` | Quand |
|---|---|
| `message-invalide` | trame binaire de moins de 8 octets, type inconnu, destinataire hors du salon ; trame texte autre que `ping` ou `expulser` ; expulsion d'un numéro invalide (0, place inexistante ou déjà libre) |
| `destinataire-interdit` | `TOUR`, `VITESSE` ou `EMPREINTE` avec `dest` ≠ `0xFF` ; `SAUVEGARDE` d'un invité avec `dest` ≠ `0` |
| `reserve-a-l-hote` | un invité demande une expulsion |

Exemple :

```json
{"type":"erreur","code":"salon-plein","message":"Ce salon est complet."}
```

## Messages du serveur (texte JSON)

`joueurs` donne toujours l'état de toutes les places attribuées (les places libérées n'y figurent pas).

### `bienvenue` (à l'arrivée ou au retour)

```json
{"type":"bienvenue","version":1,"code":"K7QXM3","joueur":1,"jeton":"9f2c…(32 car.)","joueursMax":2,
 "reconnexion":false,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":true}]}
```

### `joueur-arrive`, `joueur-revenu`, `joueur-parti`, `joueur-expulse` (aux autres joueurs)

```json
{"type":"joueur-arrive","joueur":1,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":true}]}
{"type":"joueur-parti","joueur":1,"joueurs":[{"joueur":0,"connecte":true},{"joueur":1,"connecte":false}]}
{"type":"joueur-expulse","joueur":1,"joueurs":[{"joueur":0,"connecte":true}]}
```

- `joueur-arrive` : nouveau joueur (place neuve ou libérée) → l'hôte doit lui envoyer sa sauvegarde.
- `joueur-revenu` : retour avec le jeton → l'hôte renvoie aussi sa sauvegarde (le joueur revenu a manqué des tours). **Peut arriver sans `joueur-parti` avant** : c'est le cas d'une connexion remplacée (le joueur a rouvert un onglet alors que l'ancienne connexion était encore ouverte). Le client le traite comme un retour : resynchronisation de tous.
- `joueur-parti` : la dernière connexion de ce joueur s'est fermée (fermeture, coupure vue par le runtime, ou message trop gros). **L'hôte n'est jamais remplacé** : s'il part, la partie attend son retour.
- `joueur-expulse` : l'hôte a libéré cette place (envoyé à tous, hôte compris, comme confirmation). Le jeton de ce joueur ne marche plus ; aucun `joueur-parti` n'est envoyé en plus.

### `erreur`

Voir « Refus » plus haut.

## Messages du client (texte)

- `ping` (4 caractères, pas du JSON) : le runtime répond `pong` sans réveiller le salon (maintien de connexion).
- Expulsion, **hôte seulement** : `{"type":"expulser","joueur":1}`. La place est libérée, le jeton annulé, la connexion du joueur (s'il est connecté) fermée en `4006`, puis tous reçoivent `joueur-expulse`. Sert quand un curieux a pris la place avec le code, ou qu'un invité a perdu son jeton. Ce que devient la ville du joueur expulsé est l'affaire du client (M6b/M8).

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

| Type | Nom | `dest` | `tick` | Données |
|---|---|---|---|---|
| `0x01` | `TOUR` | **`0xFF` obligatoire** | tick du tour | commandes du joueur pour ce tour, **vide** s'il n'a rien fait (voir plus bas) |
| `0x02` | `VITESSE` | **`0xFF` obligatoire** | tick d'effet | `u16 vitesse` (en %, 10 à 500 comme le jeu), `u8 pause` (0/1) |
| `0x03` | `EMPREINTE` | **`0xFF` obligatoire** | tick de l'empreinte | `u64` empreinte FNV-1a de l'état (`docs/determinisme.md`) |
| `0x04` | `SAUVEGARDE_DEMANDE` | un joueur (`0` en général) | 0 | aucune |
| `0x05` | `SAUVEGARDE` | hôte : `0xFE` ou un joueur ; **invité : `0` seulement** | tick de la sauvegarde | un morceau (voir plus bas) |
| `0x10` | `CHAT` | libre | 0 | réservé (M7) : texte UTF-8 |
| `0x11` | `PING` | un joueur | 0 | réservé (M7) : mesure de latence entre joueurs |

`TOUR`, `VITESSE` et `EMPREINTE` doivent aller à tout le salon, émetteur compris : un tour envoyé à un seul joueur, ou sans écho à l'émetteur, désynchroniserait la partie sans aucune erreur. Le serveur refuse donc tout autre `dest` (`destinataire-interdit`).

Un invité peut envoyer une `SAUVEGARDE` **à l'hôte seulement** : c'est le cas d'un hôte qui a rechargé sa page. Il revient avec son jeton, demande l'état à un invité (`SAUVEGARDE_DEMANDE` avec `dest` = ce joueur), le reçoit, puis le renvoie à tous et tout le monde recharge (voir M6b, point 5).

Le serveur relaie déjà `CHAT` et `PING` comme les autres types ; leur contenu sera fixé en M7.

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

Lecture en C : les `i32` commencent à un décalage non aligné (2 octets après le début de la commande) : les lire avec `memcpy`, jamais par un pointeur `int32_t *`. Avant de lire une commande, vérifier que `2 + nb_params × 4` tient dans ce qui reste du message, sinon rejeter tout le tour (un tour tronqué ne doit jamais faire lire hors du tampon).

### `SAUVEGARDE` : envoi en morceaux

Une sauvegarde (~100 Ko) dépasse la taille maximale d'un message. Elle est découpée :

```
u32 identifiant du transfert   u32 taille totale   u32 position du morceau   octets du morceau
```

Envoi :

- morceau de **60 000 octets** au plus conseillé (maximum : 65 516) ; 100 Ko = 2 messages ;
- `identifiant` **tiré au hasard** (u32 non nul) à chaque envoi, pour ne pas confondre deux transferts, même après un redémarrage de l'hôte ;
- les morceaux sont envoyés dans l'ordre, sans trou ; une sauvegarde vide = un seul morceau vide ;
- 8 Mo au plus par sauvegarde.

Réception (règles appliquées par `Reception` dans `client.mjs`, à reprendre en C) :

1. Un morceau en **position 0** commence un transfert : il remplace celui en cours, quel qu'il soit. Il est refusé si l'identifiant vaut 0 ou si la taille totale dépasse **8 Mo** (aucune allocation).
2. Un morceau d'un **autre identifiant** en position ≠ 0 est ignoré, sans toucher au transfert en cours. C'est le cas d'un joueur arrivé au milieu d'un envoi à `0xFE` : il attend l'envoi suivant (l'hôte lui en fait un à son arrivée).
3. Pour le transfert en cours, chaque morceau doit avoir la même taille totale et commencer **exactement** où le précédent s'est arrêté. Sinon (doublon, trou), le transfert est abandonné, et une sauvegarde fausse n'est jamais rendue.
4. Un morceau vide (sauf sauvegarde vide) ou qui dépasse la taille totale abandonne le transfert.
5. Complète quand la position atteinte égale la taille totale ; l'état de réception est alors remis à zéro.

Les morceaux arrivent dans l'ordre d'envoi (garanti), éventuellement entrelacés d'autres messages (`TOUR`…).

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
- Rien n'est gardé pour un joueur déconnecté : à son retour, il reçoit la sauvegarde de l'hôte.

## Limites

| Limite | Valeur | Réglage |
|---|---|---|
| Taille d'un message (en-tête compris) | 65 536 octets | `TAILLE_MAX_MESSAGE` (`src/index.js`) |
| Joueurs par salon | 2 (PLAN.md : 2, prévoir jusqu'à 4) | `JOUEURS_MAX` (`wrangler.jsonc`) : mettre `"4"` pour permettre au créateur de demander 3 ou 4 places |
| Fermeture d'un salon vide | 600 s après le départ du dernier joueur | `DELAI_FERMETURE_S` (`wrangler.jsonc`) |
| Sauvegarde | 8 Mo | règle côté client (le serveur ne limite que chaque message) |

Fermeture : tant que le salon existe, une alarme du Durable Object est posée, à « maintenant + délai » à chaque arrivée et à chaque départ. À l'échéance, s'il reste quelqu'un, elle est reposée ; sinon le salon est effacé (stockage compris) et son code devient inconnu. Même si un départ n'a pas été vu par le salon (éviction du runtime, mise à jour du Worker), le salon finit donc fermé. Le salon est oublié avant l'effacement : un retour pendant l'effacement reçoit `salon-inconnu`.

**Pas de limite de débit en version 1**, et la limite de 64 Kio n'est vérifiée qu'une fois le message reçu. Avant d'ouvrir la page au public (M13) : règle de limitation de débit Cloudflare sur `/creer` et `/rejoindre`, et surveiller la consommation du Worker et des Durable Objects.

## État et hibernation

L'API WebSocket Hibernation est utilisée (`ctx.acceptWebSocket`) : le salon peut être déchargé de la mémoire entre deux messages sans couper les connexions.

- Stockage du Durable Object (clé `salon`) : code, nombre de places, date de création, jetons (la place d'un jeton = le numéro du joueur ; `null` = place libérée).
- Chaque connexion porte `{ joueur, actif }` (`serializeAttachment`).
- Rien d'utile n'est gardé seulement en mémoire : après un réveil, le salon est relu dans le constructeur.
- Durable Object adossé à SQLite (`new_sqlite_classes`) : c'est le type disponible sur l'offre gratuite de Cloudflare.
- Concurrence : à l'arrivée, toutes les décisions (place, connexion remplacée, connexion acceptée) se prennent sans `await` ; les écritures du stockage et de l'alarme ne sont attendues qu'ensuite.

Non testé en local : le réveil après une vraie hibernation (le runtime local ne décharge pas l'objet à la demande). À vérifier en ligne en M13.

## Ce qui reste au client (M6b) : cahier des charges, réalisé

Le relais ne fait que transporter. Le lockstep est entièrement côté client. Ce cahier des charges (écrit en M5) a été réalisé en M6b ; ce qui a été fait, avec les écarts, est décrit dans la section suivante (« Client réseau : ce qui est fait »).

1. **Délai d'entrée** : une action faite au tick T est envoyée pour le tick T + D, avec D ≈ 100 à 200 ms de jeu (5 à 10 ticks à vitesse normale ; à adapter à la vitesse et à la latence mesurée). Chaque client envoie **un `TOUR` par tick**, vide s'il n'a rien fait (ou, pour moins de messages, un `TOUR` par groupe de ticks ; à décider en M6b, le serveur ne change pas).
2. **Joueurs attendus et avancée des ticks** : l'ensemble des joueurs attendus est fixé **par la sauvegarde chargée** : ce sont les places attribuées (champ `joueurs` du dernier message de session reçu) au moment où l'hôte envoie la `SAUVEGARDE` de départ ou de resynchronisation. L'hôte l'écrit dans l'en-tête de partie (`multijoueur/entete.c`), pour que tous aient la même liste. Le tick N n'est joué que lorsque le `TOUR` N de chaque joueur attendu est arrivé (le sien compris, reçu en écho avec `dest = 0xFF`). Les commandes reçues sont mises en file par `brutus_commande_ajouter()` (joueur et tick de l'en-tête), puis le tick est autorisé par `brutus_ticks_externes_ajouter(1)`. L'ordre d'application (tick, joueur, arrivée) est fait par le moteur, identique chez tous. Une arrivée, un retour ou une expulsion change l'ensemble seulement à la resynchronisation suivante.
3. **Vitesse et pause partagées** : `VITESSE` avec un tick d'effet futur (même délai que les commandes), appliqué par tous au même tick. Hors partie, `brutus_multijoueur_definir(1)` et `brutus_joueur_local_definir(numéro)` au démarrage.
4. **Pause sur déconnexion** : à `joueur-parti`, plus aucun tick n'avance (il manquera ses `TOUR`) ; afficher « en attente de … ». À `joueur-revenu` (avec ou sans `joueur-parti` avant), l'hôte écrit sa sauvegarde et l'envoie, puis tous rechargent.
5. **Arrivée et resynchronisation** : l'hôte écrit la sauvegarde (`game_file_write_saved_game`, avec l'en-tête de partie de `multijoueur/entete.c`) et l'envoie en `SAUVEGARDE` ; puis **tous, hôte compris**, la rechargent par `brutus_resynchroniser(chemin)`, qui remet aussi à zéro les ticks externes en attente. Les ticks reprennent au tick de la sauvegarde (`tick` de l'en-tête). **Hôte qui a rechargé sa page** : à sa `bienvenue` avec `reconnexion: true` sans partie en mémoire, il envoie `SAUVEGARDE_DEMANDE` au plus petit invité connecté ; cet invité lui envoie sa sauvegarde (`dest = 0`), l'hôte la charge puis la renvoie à tous (resynchronisation normale).
6. **Détection de divergence** : tous les N ticks, chacun envoie `EMPREINTE` ; si celle d'un invité diffère de celle de l'hôte au même tick, l'hôte lance une resynchronisation (point 5) avec le message « resynchronisation… ». Un invité peut aussi la demander par `SAUVEGARDE_DEMANDE`.
7. **Fermetures par le serveur** : agir dès le message `erreur` (`remplace`, `expulse`, `message-trop-gros`), sans attendre l'événement `close`. En local (wrangler/workerd), quand le serveur ferme une connexion qui n'a encore rien envoyé, le client reçoit la trame de fermeture mais pas `close` (connexion TCP laissée ouverte). À revérifier en ligne en M13. Après un tel message, le client ferme lui-même sa connexion et ne se reconnecte pas.
8. **Reconnexion** : garder `code` et `jeton` dans `sessionStorage` **dès la `bienvenue`**, se reconnecter avec `?jeton=` après une coupure ou un rechargement ; ne pas se reconnecter après `remplace` (4005) ni `expulse` (4006). Proposer à l'hôte un bouton « Retirer ce joueur » (`expulser`) sur une place déconnectée.
9. **Coupure silencieuse** : le serveur ne la voit pas toujours (le `pong` est servi sans réveiller le salon, et un réseau coupé sans fermeture TCP ne produit pas de `joueur-parti` tout de suite). Chaque client envoie donc `ping` toutes les 15 à 30 s, et :
   - sans `pong` depuis ~45 s, il considère sa connexion morte, la ferme et se reconnecte avec son jeton ;
   - s'il attend le `TOUR` d'un joueur depuis plus de ~10 s, il affiche « en attente de … » comme pour `joueur-parti`. Quand ce joueur se reconnecte, sa nouvelle connexion remplace la morte et les autres reçoivent `joueur-revenu`.

## Client réseau : ce qui est fait (M6b)

Code : `web/lockstep.js` (script classique, sans DOM, inséré dans la page par `moteur/CMakeLists.txt` et chargé sous Node par les tests), côté moteur `moteur/src/multijoueur/reseau.c` (pont par un tampon statique de 64 Ko ; l'échange passe par les fonctions `brutus_reseau_*`), interface dans `web/shell.html.in`. Le serveur (`serveur-multi/`) n'a pas changé.

### Choix : un `TOUR` par groupe de 5 ticks

- 5 divise 50, le nombre de ticks d'un jour de jeu : un groupe ne chevauche jamais un changement de jour.
- 5 ticks = 80 ms à 100 %, 40 ms à 200 %, 10 ms à 500 % : le délai d'entrée de 2 groupes (D = 2) donne 160 ms à 100 % (dans la fourchette de 100 à 200 ms), moins à vitesse élevée.
- Cinq fois moins de messages qu'un `TOUR` par tick (le test de 30 minutes : environ 90 000 `TOUR` pour 225 000 ticks et 2 joueurs, contre 450 000), donc moins de travail pour le relais et pour le quota du Durable Object. Le coût : une commande attend en moyenne un demi-groupe de plus avant d'être jouée (au plus 40 ms à 100 %).
- Les groupes sont alignés sur T0, le tick juste après le chargement de la sauvegarde. Le `tick` de l'en-tête d'un `TOUR` est le premier tick du groupe. Les groupes 0 à D-1 sont vides par construction et ne sont pas envoyés. Un `TOUR` k ne part que si k < prochain groupe + D, et un `TOUR` reçu avec k ≥ prochain + 2D + 1 est ignoré.
- Format du `TOUR` : celui proposé plus haut (type, nombre de paramètres, paramètres `int32`), plusieurs commandes à la suite. Le moteur reçoit les commandes avec le tick du `TOUR` et les applique avant le premier tick du groupe de la même façon que pour tous.

### Point par point

1. **Délai d'entrée** : D = 2 groupes. Une action est mise dans la boîte de sortie du moteur (`brutus_reseau_sortie`) et part dans le prochain `TOUR` envoyé, joué deux groupes plus tard.
2. **Avancée des ticks** : le moteur n'avance pas seul en multijoueur ; JS appelle `brutus_reseau_avancer` pour un groupe seulement quand les `TOUR` de tous les joueurs attendus sont arrivés (le sien reçu en écho). Les ticks sont joués au rythme de la vitesse partagée.
3. **Vitesse et pause** : message `VITESSE`, 3 octets : pourcentage (u16) puis pause (u8), valeur **absolue**. Tick d'effet : début du groupe en cours d'envoi (ou du groupe attendu si on est déjà en pause). Les changements du même groupe sont appliqués triés par (tick, joueur, rang d'arrivée). La pause n'étant pas sauvegardée, elle est réappliquée après chaque avance et chaque chargement. Vitesses : 10 à 100 % par pas de 10, puis 200, 300 et 500 %.
4. **Pause sur déconnexion** : « en attente de … » dès `joueur-parti` ou après 10 s sans `TOUR` d'un joueur ; plus aucun groupe n'avance. Au `joueur-revenu` (avec ou sans `joueur-parti` avant : testé), l'hôte resynchronise.
5. **Arrivée et resynchronisation** : l'hôte écrit sa sauvegarde, l'envoie en morceaux de 60 000 octets (règles de réception de `Reception`, reprises en JS) et tout le monde recharge par `brutus_resynchroniser`, hôte compris. L'hôte qui recharge sa page : `SAUVEGARDE_DEMANDE` au plus petit invité connecté, toutes les 5 s au plus, jusqu'à réception ; testé.
6. **Empreinte** : `EMPREINTE` (tick, 8 octets de FNV-1a 64 bits, `dest` 0xFF) tous les 50 groupes (250 ticks) par défaut. Seul l'hôte compare ; s'il voit une différence, il resynchronise tout le monde (message « resynchronisation… » pendant au moins 2 s). Un invité peut aussi forcer la resynchronisation.
7. **Fermetures par le serveur** : le client agit sur `erreur` sans attendre `close`. Pas de reconnexion après `remplace` ni `expulse` ; `message-trop-gros` ferme puis reconnecte.
8. **Reconnexion** : code et jeton dans `sessionStorage` dès la `bienvenue` ; essais à 1, 2, 4 puis 8 s. Bouton « Retirer » pour l'hôte.
9. **Coupure silencieuse** : `ping` texte toutes les 20 s ; connexion jugée morte après 45 s sans rien recevoir ; « en attente » après 10 s sans `TOUR`.

### Écarts avec le cahier des charges

- **Liste des joueurs attendus** : elle n'est pas dans `multijoueur/entete.c` mais dans un préfixe de la `SAUVEGARDE` ajouté par le client JS : « BR3S », version, époque, nombre de joueurs, leurs numéros, vitesse (u16), pause (u8), puis les octets de la sauvegarde du moteur. Le moteur n'a pas besoin de connaître le réseau ; `entete.c` n'est pas modifié.
- **Époque** : l'octet 3 de l'en-tête (réservé) porte l'époque de la partie. L'hôte l'incrémente à chaque sauvegarde envoyée (cycle 1 à 255). Les messages d'une autre époque sont ignorés : un `TOUR` tardif d'avant la resynchronisation ne peut pas fausser la partie rechargée.
- **Vitesse absolue** et non relative : deux joueurs qui changent la vitesse en même temps n'obtiennent pas la somme des deux demandes mais la dernière dans l'ordre (tick, joueur, arrivée), identique chez tous.

### Corrections après relecture QA (M6b)

- **Menus sans commande refusés** : en partie à deux, « Charger » (menu et chargement rapide), « Recommencer » et « Quitter la mission » ne font rien (`brutus_reseau_menu_refuse()` aux points d'appel, commentés « Brutus III (M6b) »). Le client compte les refus (`stats.menusRefuses`) et affiche l'avis « Charger, Recommencer et Quitter la mission sont désactivés pendant une partie à deux » (`etat().avis`, 6 s).
- **Ville quittée** (menu principal ou choix de mission, seuls chemins restants : fin de mission en M11) : le moteur le signale (`brutus_reseau_ville_est_quittee`). Le client passe en phase `hors-ville` : il n'avance plus, n'envoie plus de commandes, ne répond plus aux demandes de sauvegarde. L'autre joueur attend (« en attente de … »). `revenir()` (bouton « Revenir dans la partie ») : l'invité demande la partie à l'hôte ; l'hôte, lui, réclame sa partie à un invité (comme après un rechargement de page) puis la renvoie à tous. Pendant que l'hôte est dehors, aucune resynchronisation ne part (sa ville n'est pas à l'écran).
- **Vitesse commune** : le rythme ne lit plus la vitesse locale. Deux réglages du moteur échappaient aux messages `VITESSE` : le retour à la vitesse par défaut après la première invasion (`city/message.c`) et la fenêtre d'options (`window/config.c`) ; ils sont ignorés en réseau. Si une autre vitesse locale apparaît quand même, le client la remet à la vitesse commune avant de jouer, la journalise et la compte (`stats.vitessesCorrigees`).
- **Divergences** : une divergence réparée n'est pas un succès. Le client garde une fenêtre glissante : à 3 divergences en 10 minutes (`divergencesMax`, `fenetreDivergencesMs`), `etat().alerte` affiche un message et le journal reçoit le détail par morceau (le moteur écrit le détail de la dernière empreinte dans `/tmp/brutus-empreinte-detail.txt`, lu par `moteur.detailEmpreinte()`). Les tests exigent zéro divergence hors panne injectée. Causes connues et règles : `docs/determinisme.md`.
- **Récupération de l'hôte** : 60 s au plus (`recuperationMs`). Au-delà : partie à deux abandonnée avec un message (`stats.recuperationsAbandonnees`).
- **Envois** : le retour de `conn.envoyer` est vérifié. Si une resynchronisation ne peut pas partir, l'hôte ne recharge pas seul : nouvel essai (`stats.envoisRates`).
- **Chargement refusé chez l'invité** : il redemande la sauvegarde une fois (avec l'époque de l'hôte) avant d'afficher une erreur.
- **Divers** : rang des `VITESSE` par compteur croissant ; `TOUR` d'un joueur non attendu ignoré ; l'époque modulo 255 est inchangée (risque négligeable) ; `reserve` et boîte de sortie vidées au début de chaque époque ; une fermeture de connexion n'est comptée qu'une fois (génération `s.gen`).
- **Onglet masqué** : la page avertit (titre « onglet masqué : partie à deux ralentie ») et fait avancer le client à chaque message reçu (`pomperSurMessage`), car le `setInterval` de 20 ms est ralenti par le navigateur. Garder tout de même les deux onglets visibles. À essayer à la main.
- **Relais** : l'adresse `ws://127.0.0.1:8787` est écrite en dur dans `web/shell.html.in` (panneau limité à localhost). M13 : passer à `wss://` et à l'adresse publique.

### Test et limites

- Test sans port : `bash outils/test-reseau-memoire.sh` (deux moteurs wasm, relais en mémoire qui reprend les règles du serveur). Résultat et scénario : `docs/determinisme.md`, « Test réseau (M6b) ».
- Test avec le vrai relais : `bash outils/test-reseau-30min.sh` (`wrangler dev` local, deux clients Node, même scénario). **À lancer par le fondateur** : le bac à sable des agents ne peut pas ouvrir de port. Par défaut les délais sont raccourcis (ping 2 s, coupure silencieuse 8 s) pour que le scénario tienne dans le test ; l'option `--production` (`BRUTUS_VITESSE=200 bash outils/test-reseau-30min.sh 225000 Lugdunum --production`) garde les constantes de `web/lockstep.js` (ping 20 s, coupure 45 s, « en attente » 10 s) et dure au moins 20 minutes réelles.
- **La page (« Jouer à deux ») n'a pas été essayée dans un navigateur** : seul son code JS est partagé avec les tests. À vérifier à la main (voir README).
- Onglet en arrière-plan : les navigateurs ralentissent fortement les minuteurs d'un onglet masqué ; le jeu de cet onglet prend du retard et les autres l'attendent (« en attente de … »). Garder les deux onglets visibles, dans deux fenêtres côte à côte.
- Ordre des `VITESSE` : appliqués par (tick, joueur, arrivée) ; l'arrivée est l'ordre du relais, le même pour tous les clients (une seule connexion par joueur).
- Le test n'exerce que les commandes du bot et une partie des messages du serveur ; pas de latence réelle de plusieurs centaines de ms, pas de perte de paquets au sens TCP.
