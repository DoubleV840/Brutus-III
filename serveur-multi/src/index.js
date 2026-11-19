// Brutus III : relais WebSocket multijoueur (mission M5).
//
// Un Worker (routage, origine, version) + un Durable Object « Salon » par code de salon.
// Le relais n'a AUCUNE logique de jeu : il lit l'en-tête de 8 octets des messages binaires
// (type, destinataire), écrit le numéro de l'émetteur, et transmet dans l'ordre d'arrivée.
// Protocole complet : docs/protocole.md.

import { DurableObject } from "cloudflare:workers";

export const VERSION_PROTOCOLE = 1;

// Taille maximale d'un message (binaire ou texte), en octets, en-tête compris.
export const TAILLE_MAX_MESSAGE = 65536;
const TAILLE_ENTETE = 8;

// Codes de salon : 6 caractères, sans 0/O ni 1/I/L.
export const ALPHABET_CODE = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const LONGUEUR_CODE = 6;
const ESSAIS_CREATION = 8;

// Types des messages binaires (octet 0 de l'en-tête).
export const TYPE = {
  TOUR: 0x01,
  VITESSE: 0x02,
  EMPREINTE: 0x03,
  SAUVEGARDE_DEMANDE: 0x04,
  SAUVEGARDE: 0x05,
  CHAT: 0x10,
  PING: 0x11,
};
const TYPES_CONNUS = new Set(Object.values(TYPE));

// Destinataires (octet 2 de l'en-tête). 0 à 3 : un joueur précis.
export const DEST_TOUS = 0xff; // tout le salon, émetteur compris
export const DEST_AUTRES = 0xfe; // tout le salon sauf l'émetteur

// Types qui doivent aller à tout le salon, émetteur compris : sinon le flux diffère d'un
// joueur à l'autre et la partie se désynchronise sans aucune erreur.
const TYPES_POUR_TOUS = new Set([TYPE.TOUR, TYPE.VITESSE, TYPE.EMPREINTE]);

// Codes de fermeture WebSocket (4000-4999 : réservés aux applications).
export const FERMETURE = {
  "version": 4000,
  "salon-inconnu": 4001,
  "salon-plein": 4002,
  "jeton-invalide": 4003,
  "requete-invalide": 4004,
  "remplace": 4005,
  "expulse": 4006,
  "message-trop-gros": 4009,
};

const MESSAGES = {
  "version": "Version du protocole différente : recharge la page.",
  "salon-inconnu": "Aucun salon avec ce code (ou il a été fermé).",
  "salon-plein": "Ce salon est complet.",
  "jeton-invalide": "Jeton de reconnexion inconnu pour ce salon.",
  "requete-invalide": "Requête invalide.",
  "remplace": "Ce joueur s'est reconnecté depuis une autre connexion.",
  "expulse": "L'hôte t'a retiré du salon.",
  "message-trop-gros": "Message trop gros (64 Kio au plus).",
  "message-invalide": "Message invalide (en-tête trop court, type ou destinataire inconnu).",
  "destinataire-interdit": "Destinataire interdit pour ce type de message.",
  "reserve-a-l-hote": "Seul l'hôte (joueur 0) peut faire cette demande.",
};

// ---------------------------------------------------------------------------
// Worker : routage
// ---------------------------------------------------------------------------

export default {
  async fetch(requete, env) {
    const url = new URL(requete.url);

    if (url.pathname === "/sante") {
      return new Response("ok\n", { headers: { "content-type": "text/plain" } });
    }

    const creer = url.pathname === "/creer";
    const rejoindre = url.pathname.match(/^\/rejoindre\/([^/]+)$/);
    if (!creer && !rejoindre) {
      return reponseTexte(404, "Inconnu. Voir docs/protocole.md.");
    }
    if (!origineAcceptee(requete.headers.get("Origin"))) {
      return reponseTexte(403, "Origine refusée.");
    }
    if ((requete.headers.get("Upgrade") || "").toLowerCase() !== "websocket") {
      return reponseTexte(426, "Connexion WebSocket attendue.");
    }

    // Refus « propres » : la connexion WebSocket est acceptée, puis un message d'erreur
    // JSON est envoyé et la connexion fermée avec un code 4xxx (un navigateur ne peut pas
    // lire le statut HTTP d'un échec de connexion WebSocket).
    if (url.searchParams.get("version") !== String(VERSION_PROTOCOLE)) {
      return refuser("version");
    }

    if (creer) {
      const max = joueursMax(env);
      const demande = url.searchParams.get("joueurs");
      const joueurs = demande === null ? 2 : Number(demande);
      if (!Number.isInteger(joueurs) || joueurs < 2 || joueurs > max) {
        return refuser("requete-invalide", `Nombre de joueurs : de 2 à ${max}.`);
      }
      for (let essai = 0; essai < ESSAIS_CREATION; essai++) {
        const code = nouveauCode();
        const reponse = await versSalon(env, code, requete, { action: "creer", joueurs });
        if (reponse.status !== 409) {
          return reponse; // 101 (salon créé) ou autre erreur
        }
      }
      return reponseTexte(503, "Impossible de créer un salon, réessaie.");
    }

    const code = rejoindre[1].toUpperCase();
    if (!codeValide(code)) {
      return refuser("salon-inconnu");
    }
    const jeton = url.searchParams.get("jeton");
    return versSalon(env, code, requete, { action: "rejoindre", jeton: jeton || "" });
  },
};

function versSalon(env, code, requete, infos) {
  const stub = env.SALONS.get(env.SALONS.idFromName(code));
  const cible = new URL("https://salon/" + infos.action);
  cible.searchParams.set("code", code);
  if (infos.joueurs !== undefined) cible.searchParams.set("joueurs", String(infos.joueurs));
  if (infos.jeton) cible.searchParams.set("jeton", infos.jeton);
  return stub.fetch(new Request(cible, { headers: requete.headers }));
}

export function origineAcceptee(origine) {
  // Pas d'en-tête Origin : client hors navigateur (démo Node, tests). Un tel client peut
  // de toute façon mettre l'origine qu'il veut ; la vérification protège les joueurs contre
  // une page d'un autre site ouverte dans leur navigateur, pas contre l'abus (voir M13).
  if (origine === null) return true;
  if (origine === "https://brutus.ivwi.fr") return true;
  return /^https?:\/\/(localhost|127\.0\.0\.1)(:\d{1,5})?$/.test(origine);
}

export function codeValide(code) {
  if (code.length !== LONGUEUR_CODE) return false;
  for (const c of code) {
    if (!ALPHABET_CODE.includes(c)) return false;
  }
  return true;
}

function nouveauCode() {
  // Tirage sans biais : on rejette les octets au-delà du plus grand multiple de 31.
  const n = ALPHABET_CODE.length;
  const limite = 256 - (256 % n);
  let code = "";
  while (code.length < LONGUEUR_CODE) {
    const octets = crypto.getRandomValues(new Uint8Array(16));
    for (const o of octets) {
      if (o < limite && code.length < LONGUEUR_CODE) code += ALPHABET_CODE[o % n];
    }
  }
  return code;
}

function nouveauJeton() {
  const octets = crypto.getRandomValues(new Uint8Array(16));
  return Array.from(octets, (o) => o.toString(16).padStart(2, "0")).join("");
}

function joueursMax(env) {
  const n = Number(env.JOUEURS_MAX);
  return Number.isInteger(n) && n >= 2 && n <= 4 ? n : 2;
}

function delaiFermetureMs(env) {
  const n = Number(env.DELAI_FERMETURE_S);
  return (Number.isFinite(n) && n > 0 ? n : 600) * 1000;
}

function reponseTexte(statut, texte) {
  return new Response(texte + "\n", { status: statut, headers: { "content-type": "text/plain; charset=utf-8" } });
}

function erreurJson(code, message) {
  return JSON.stringify({ type: "erreur", code, message: message || MESSAGES[code] || code });
}

function refuser(code, message) {
  const [client, serveur] = Object.values(new WebSocketPair());
  serveur.accept();
  serveur.send(erreurJson(code, message));
  serveur.close(FERMETURE[code], code);
  return new Response(null, { status: 101, webSocket: client });
}

// ---------------------------------------------------------------------------
// Durable Object : un salon
// ---------------------------------------------------------------------------
//
// Stockage (clé « salon ») : { code, joueursMax, creeLe, jetons: [jeton du joueur 0, …] }.
// Le numéro d'un joueur est sa place dans `jetons` (ordre d'arrivée, 0 = hôte). Une place
// libérée par l'hôte (expulsion) vaut null et sera reprise par le prochain arrivant.
// Chaque WebSocket porte { joueur, actif } (serializeAttachment) : l'état survit à
// l'hibernation, rien d'utile n'est gardé seulement en mémoire.
//
// Concurrence : toutes les décisions (numéro attribué, connexion remplacée, connexion
// acceptée) sont prises SANS « await » entre elles ; les écritures du stockage ne sont
// attendues qu'à la fin. Deux arrivées simultanées ne peuvent donc pas se croiser.
//
// Alarme : tant que le salon existe, une alarme est posée (à chaque arrivée et à chaque
// départ, échéance = maintenant + délai). À l'échéance, s'il reste quelqu'un elle est
// reposée ; sinon le salon est effacé. Même si un départ n'a pas été vu (éviction du
// runtime), le salon finit donc par être fermé.

export class Salon extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.salon = null;
    ctx.blockConcurrencyWhile(async () => {
      this.salon = (await ctx.storage.get("salon")) || null;
    });
    // Maintien de connexion sans réveiller l'objet : « ping » (texte) → « pong ».
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
  }

  async fetch(requete) {
    const url = new URL(requete.url);
    const action = url.pathname.slice(1);
    const code = url.searchParams.get("code");

    if (action === "creer") {
      if (this.salon) {
        return new Response("pris", { status: 409 }); // le Worker tire un autre code
      }
      this.salon = {
        code,
        joueursMax: Number(url.searchParams.get("joueurs")),
        creeLe: Date.now(),
        jetons: [nouveauJeton()],
      };
      return this.accepter(0, false);
    }

    if (action === "rejoindre") {
      if (!this.salon) {
        return refuser("salon-inconnu");
      }
      const jeton = url.searchParams.get("jeton");
      if (jeton) {
        const joueur = this.salon.jetons.indexOf(jeton);
        if (joueur < 0) {
          return refuser("jeton-invalide");
        }
        // Une ancienne connexion de ce joueur (onglet resté ouvert, réseau coupé non détecté)
        // est fermée : la nouvelle prend sa place.
        for (const ws of this.sockets(joueur)) {
          desactiver(ws, joueur);
          envoyer(ws, erreurJson("remplace"));
          fermer(ws, FERMETURE.remplace, "remplace");
        }
        return this.accepter(joueur, true);
      }
      // Première place libérée, sinon une nouvelle place.
      let joueur = this.salon.jetons.indexOf(null);
      if (joueur < 0) {
        if (this.salon.jetons.length >= this.salon.joueursMax) {
          return refuser("salon-plein");
        }
        joueur = this.salon.jetons.length;
      }
      this.salon.jetons[joueur] = nouveauJeton();
      return this.accepter(joueur, false);
    }

    return new Response("inconnu", { status: 404 });
  }

  /**
   * Accepte la connexion du joueur, lui envoie « bienvenue » et prévient les autres.
   * Tout est fait sans attente ; le stockage et l'alarme ne sont attendus qu'à la fin.
   */
  async accepter(joueur, reconnexion) {
    const [client, serveur] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(serveur, ["j" + joueur]);
    serveur.serializeAttachment({ joueur, actif: true });

    serveur.send(JSON.stringify({
      type: "bienvenue",
      version: VERSION_PROTOCOLE,
      code: this.salon.code,
      joueur,
      jeton: this.salon.jetons[joueur],
      joueursMax: this.salon.joueursMax,
      reconnexion,
      joueurs: this.etatJoueurs(),
    }));
    this.annoncer({ type: reconnexion ? "joueur-revenu" : "joueur-arrive", joueur }, serveur);

    const ecritures = [this.ctx.storage.setAlarm(Date.now() + delaiFermetureMs(this.env))];
    if (!reconnexion) ecritures.push(this.ctx.storage.put("salon", this.salon));
    await Promise.all(ecritures);
    return new Response(null, { status: 101, webSocket: client });
  }

  /** Envoie un message de session à toutes les connexions actives sauf `sauf`. */
  annoncer(message, sauf, restantes = this.actives()) {
    const texte = JSON.stringify({ ...message, joueurs: this.etatJoueurs(restantes) });
    for (const ws of restantes) {
      if (ws !== sauf) envoyer(ws, texte);
    }
  }

  /** Connexions actives (hors connexions retirées par le serveur). */
  actives() {
    return this.ctx.getWebSockets().filter((ws) => {
      const a = ws.deserializeAttachment();
      return a && a.actif && ws.readyState === WebSocket.OPEN;
    });
  }

  sockets(joueur) {
    return this.actives().filter((ws) => ws.deserializeAttachment().joueur === joueur);
  }

  /** État des places attribuées (les places libérées n'apparaissent pas). */
  etatJoueurs(actives = this.actives()) {
    const connectes = new Set(actives.map((ws) => ws.deserializeAttachment().joueur));
    const etat = [];
    this.salon.jetons.forEach((jeton, joueur) => {
      if (jeton !== null) etat.push({ joueur, connecte: connectes.has(joueur) });
    });
    return etat;
  }

  // Aucun « await » avant la fin de ce chemin : les messages sont traités un par un, dans
  // l'ordre d'arrivée au salon, et chaque destinataire les reçoit dans ce même ordre.
  async webSocketMessage(ws, message) {
    const a = ws.deserializeAttachment();
    if (!a || !a.actif || !this.salon) return;

    const taille = typeof message === "string" ? new TextEncoder().encode(message).byteLength : message.byteLength;
    if (taille > TAILLE_MAX_MESSAGE) {
      // Rien n'est relayé ; la connexion est fermée (le client peut revenir avec son jeton).
      desactiver(ws, a.joueur);
      envoyer(ws, erreurJson("message-trop-gros"));
      fermer(ws, FERMETURE["message-trop-gros"], "message-trop-gros");
      await this.annoncerDepart(a.joueur, ws);
      return;
    }
    if (typeof message === "string") {
      await this.messageTexte(ws, a.joueur, message);
      return;
    }

    const octets = new Uint8Array(message);
    if (octets.length < TAILLE_ENTETE || !TYPES_CONNUS.has(octets[0])) {
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }
    const type = octets[0];
    const dest = octets[2];
    if (dest !== DEST_TOUS && dest !== DEST_AUTRES && dest >= this.salon.joueursMax) {
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }
    // TOUR, VITESSE, EMPREINTE : à tous, émetteur compris. SAUVEGARDE : l'hôte l'envoie à
    // qui il veut ; un invité seulement à l'hôte (cas d'un hôte qui a rechargé sa page).
    if ((TYPES_POUR_TOUS.has(type) && dest !== DEST_TOUS)
      || (type === TYPE.SAUVEGARDE && a.joueur !== 0 && dest !== 0)) {
      envoyer(ws, erreurJson("destinataire-interdit"));
      return;
    }

    // Le serveur écrit l'émetteur : un joueur ne peut pas parler au nom d'un autre.
    octets[1] = a.joueur;
    for (const cible of this.actives()) {
      const j = cible.deserializeAttachment().joueur;
      if (dest === DEST_TOUS || (dest === DEST_AUTRES && cible !== ws) || dest === j) {
        envoyer(cible, octets);
      }
    }
  }

  /** Trames texte du client : seule la demande d'expulsion de l'hôte existe (et « ping »). */
  async messageTexte(ws, joueur, texte) {
    let m = null;
    try {
      m = JSON.parse(texte);
    } catch {
      // m reste null
    }
    if (!m || m.type !== "expulser") {
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }
    if (joueur !== 0) {
      envoyer(ws, erreurJson("reserve-a-l-hote"));
      return;
    }
    const cible = m.joueur;
    if (!Number.isInteger(cible) || cible < 1 || cible >= this.salon.jetons.length || this.salon.jetons[cible] === null) {
      envoyer(ws, erreurJson("message-invalide", "Expulsion : numéro de joueur invalide."));
      return;
    }
    // La place est libérée et le jeton annulé, puis la connexion fermée, puis tous sont prévenus.
    this.salon.jetons[cible] = null;
    for (const autre of this.sockets(cible)) {
      desactiver(autre, cible);
      envoyer(autre, erreurJson("expulse"));
      fermer(autre, FERMETURE.expulse, "expulse");
    }
    this.annoncer({ type: "joueur-expulse", joueur: cible }, null);
    await this.ctx.storage.put("salon", this.salon);
  }

  async webSocketClose(ws) {
    await this.depart(ws);
  }

  async webSocketError(ws) {
    await this.depart(ws);
  }

  async depart(ws) {
    const a = ws.deserializeAttachment();
    fermer(ws, 1000, "au revoir");
    // Connexion déjà retirée par le serveur (remplacée, expulsée, message trop gros) : déjà traitée.
    if (!a || !a.actif) return;
    desactiver(ws, a.joueur);
    await this.annoncerDepart(a.joueur, ws);
  }

  /** Prévient les autres si `joueur` n'a plus de connexion ; salon vide : alarme de fermeture. */
  async annoncerDepart(joueurParti, ws) {
    if (!this.salon) return;
    const restantes = this.actives().filter((autre) => autre !== ws);
    if (!restantes.some((autre) => autre.deserializeAttachment().joueur === joueurParti)) {
      this.annoncer({ type: "joueur-parti", joueur: joueurParti }, null, restantes);
    }
    if (restantes.length === 0) {
      await this.ctx.storage.setAlarm(Date.now() + delaiFermetureMs(this.env));
    }
  }

  /** Échéance : salon vide → effacé (son code devient inconnu) ; sinon l'alarme est reposée. */
  async alarm() {
    if (!this.salon) return;
    if (this.actives().length > 0) {
      await this.ctx.storage.setAlarm(Date.now() + delaiFermetureMs(this.env));
      return;
    }
    // Le salon est oublié AVANT l'attente : un retour pendant l'effacement reçoit
    // « salon-inconnu » au lieu de rejoindre un salon fantôme.
    this.salon = null;
    await this.ctx.storage.deleteAll();
  }
}

function envoyer(ws, donnees) {
  try {
    ws.send(donnees);
  } catch {
    // Connexion en cours de fermeture : le départ sera traité par webSocketClose.
  }
}

function desactiver(ws, joueur) {
  try {
    ws.serializeAttachment({ joueur, actif: false });
  } catch {
    // Connexion déjà fermée : elle n'apparaît plus dans getWebSockets().
  }
}

function fermer(ws, code, raison) {
  try {
    ws.close(code, raison);
  } catch {
    // Déjà fermée.
  }
}
