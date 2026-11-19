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
const TYPES_HOTE = new Set([TYPE.SAUVEGARDE]);

// Destinataires (octet 2 de l'en-tête). 0 à 3 : un joueur précis.
export const DEST_TOUS = 0xff; // tout le salon, émetteur compris
export const DEST_AUTRES = 0xfe; // tout le salon sauf l'émetteur

// Codes de fermeture WebSocket (4000-4999 : réservés aux applications).
export const FERMETURE = {
  "version": 4000,
  "salon-inconnu": 4001,
  "salon-plein": 4002,
  "jeton-invalide": 4003,
  "requete-invalide": 4004,
  "remplace": 4005,
  "message-trop-gros": 4009,
};

const MESSAGES = {
  "version": "Version du protocole différente : recharge la page.",
  "salon-inconnu": "Aucun salon avec ce code (ou il a été fermé).",
  "salon-plein": "Ce salon est complet.",
  "jeton-invalide": "Jeton de reconnexion inconnu pour ce salon.",
  "requete-invalide": "Requête invalide.",
  "remplace": "Ce joueur s'est reconnecté depuis une autre connexion.",
  "message-trop-gros": "Message trop gros (64 Kio au plus).",
  "message-invalide": "Message invalide (en-tête trop court, type ou destinataire inconnu).",
  "reserve-a-l-hote": "Seul l'hôte (joueur 0) peut envoyer ce message.",
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
  // une page d'un autre site ouverte dans leur navigateur.
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
  return Number.isInteger(n) && n >= 2 && n <= 4 ? n : 4;
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
// Le numéro d'un joueur est sa place dans `jetons` (ordre d'arrivée, 0 = hôte).
// Chaque WebSocket porte { joueur, actif } (serializeAttachment) : l'état survit à
// l'hibernation, rien d'utile n'est gardé seulement en mémoire.

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
      await this.ctx.storage.put("salon", this.salon);
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
      if (this.salon.jetons.length >= this.salon.joueursMax) {
        return refuser("salon-plein");
      }
      this.salon.jetons.push(nouveauJeton());
      await this.ctx.storage.put("salon", this.salon);
      return this.accepter(this.salon.jetons.length - 1, false);
    }

    return new Response("inconnu", { status: 404 });
  }

  /** Accepte la connexion du joueur, lui envoie « bienvenue » et prévient les autres. */
  async accepter(joueur, reconnexion) {
    await this.ctx.storage.deleteAlarm();
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
    const annonce = JSON.stringify({
      type: reconnexion ? "joueur-revenu" : "joueur-arrive",
      joueur,
      joueurs: this.etatJoueurs(),
    });
    for (const ws of this.actives()) {
      if (ws !== serveur) envoyer(ws, annonce);
    }
    return new Response(null, { status: 101, webSocket: client });
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

  etatJoueurs() {
    const connectes = new Set(this.actives().map((ws) => ws.deserializeAttachment().joueur));
    return this.salon.jetons.map((_, joueur) => ({ joueur, connecte: connectes.has(joueur) }));
  }

  // Aucun « await » dans ce chemin : les messages sont traités un par un, dans l'ordre
  // d'arrivée au salon, et chaque destinataire les reçoit dans ce même ordre.
  async webSocketMessage(ws, message) {
    const a = ws.deserializeAttachment();
    if (!a || !a.actif) return;

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
      // Aucun message texte n'est attendu du client en version 1 (sauf « ping », servi
      // automatiquement sans passer par ici).
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }

    const octets = new Uint8Array(message);
    if (octets.length < TAILLE_ENTETE || !TYPES_CONNUS.has(octets[0])) {
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }
    const dest = octets[2];
    if (dest !== DEST_TOUS && dest !== DEST_AUTRES && dest >= this.salonJoueursMax()) {
      envoyer(ws, erreurJson("message-invalide"));
      return;
    }
    if (TYPES_HOTE.has(octets[0]) && a.joueur !== 0) {
      envoyer(ws, erreurJson("reserve-a-l-hote"));
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

  salonJoueursMax() {
    // Après une hibernation, this.salon est relu dans le constructeur.
    return this.salon ? this.salon.joueursMax : 4;
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
    // Connexion déjà retirée par le serveur (remplacée, message trop gros) : déjà traitée.
    if (!a || !a.actif) return;
    desactiver(ws, a.joueur);
    await this.annoncerDepart(a.joueur, ws);
  }

  /** Prévient les autres si `joueur` n'a plus de connexion ; salon vide : alarme de fermeture. */
  async annoncerDepart(joueurParti, ws) {
    if (!this.salon) return;
    const restantes = this.actives().filter((autre) => autre !== ws);
    if (!restantes.some((autre) => autre.deserializeAttachment().joueur === joueurParti)) {
      const annonce = JSON.stringify({
        type: "joueur-parti",
        joueur: joueurParti,
        joueurs: this.salon.jetons.map((_, joueur) => ({
          joueur,
          connecte: restantes.some((autre) => autre.deserializeAttachment().joueur === joueur),
        })),
      });
      for (const autre of restantes) envoyer(autre, annonce);
    }
    if (restantes.length === 0) {
      await this.ctx.storage.setAlarm(Date.now() + delaiFermetureMs(this.env));
    }
  }

  /** Salon vide depuis le délai de fermeture : il est supprimé, son code devient inconnu. */
  async alarm() {
    if (this.actives().length > 0) return;
    await this.ctx.storage.deleteAll();
    this.salon = null;
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
