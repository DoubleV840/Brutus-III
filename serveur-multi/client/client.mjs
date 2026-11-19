// Brutus III : petit client du relais, pour les tests et la démo (Node 22+ : WebSocket global).
// Il sert aussi d'implémentation de référence de docs/protocole.md pour le client de M6b.

export const VERSION_PROTOCOLE = 1;
export const TYPE = {
  TOUR: 0x01,
  VITESSE: 0x02,
  EMPREINTE: 0x03,
  SAUVEGARDE_DEMANDE: 0x04,
  SAUVEGARDE: 0x05,
  CHAT: 0x10,
  PING: 0x11,
};
export const DEST_TOUS = 0xff;
export const DEST_AUTRES = 0xfe;
export const TAILLE_ENTETE = 8;
export const TAILLE_MAX_MESSAGE = 65536;
// En-tête du morceau de sauvegarde : identifiant, taille totale, position (3 × u32).
export const ENTETE_MORCEAU = 12;
export const MORCEAU_MAX = TAILLE_MAX_MESSAGE - TAILLE_ENTETE - ENTETE_MORCEAU;

/** Message binaire : en-tête de 8 octets (type, joueur, destinataire, réservé, tick u32 LE) + données. */
export function encoder(type, tick, donnees = new Uint8Array(0), dest = DEST_TOUS) {
  const m = new Uint8Array(TAILLE_ENTETE + donnees.length);
  const v = new DataView(m.buffer);
  m[0] = type;
  m[1] = 0; // écrit par le serveur
  m[2] = dest;
  m[3] = 0;
  v.setUint32(4, tick >>> 0, true);
  m.set(donnees, TAILLE_ENTETE);
  return m;
}

export function decoder(buffer) {
  const m = new Uint8Array(buffer);
  const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
  return {
    type: m[0],
    joueur: m[1],
    dest: m[2],
    tick: v.getUint32(4, true),
    donnees: m.subarray(TAILLE_ENTETE),
  };
}

/** Découpe une sauvegarde en messages SAUVEGARDE (voir docs/protocole.md). */
export function morceauxSauvegarde(sauvegarde, tick, identifiant, dest = DEST_AUTRES, taille = 60000) {
  const messages = [];
  for (let pos = 0; pos === 0 || pos < sauvegarde.length; pos += taille) {
    const part = sauvegarde.subarray(pos, pos + taille);
    const d = new Uint8Array(ENTETE_MORCEAU + part.length);
    const v = new DataView(d.buffer);
    v.setUint32(0, identifiant >>> 0, true);
    v.setUint32(4, sauvegarde.length, true);
    v.setUint32(8, pos, true);
    d.set(part, ENTETE_MORCEAU);
    messages.push(encoder(TYPE.SAUVEGARDE, tick, d, dest));
  }
  return messages;
}

/** Recolle les morceaux d'une sauvegarde. Retourne la sauvegarde complète ou null. */
export class Reception {
  constructor() {
    this.id = null;
  }
  ajouter(donnees) {
    const v = new DataView(donnees.buffer, donnees.byteOffset, donnees.byteLength);
    const id = v.getUint32(0, true);
    const total = v.getUint32(4, true);
    const pos = v.getUint32(8, true);
    if (id !== this.id) {
      this.id = id;
      this.tampon = new Uint8Array(total);
      this.recus = 0;
    }
    const part = donnees.subarray(ENTETE_MORCEAU);
    this.tampon.set(part, pos);
    this.recus += part.length;
    return this.recus >= total ? this.tampon : null;
  }
}

/**
 * Connexion au relais. `base` = « ws://127.0.0.1:8787 ».
 * Les messages reçus (JSON ou binaires décodés) sont mis en file ; `attendre()` les consomme.
 */
export class ClientRelais {
  static creer(base, { joueurs, version = VERSION_PROTOCOLE, origine } = {}) {
    const url = new URL("/creer", base);
    url.searchParams.set("version", String(version));
    if (joueurs !== undefined) url.searchParams.set("joueurs", String(joueurs));
    return new ClientRelais(url, origine);
  }

  static rejoindre(base, code, { jeton, version = VERSION_PROTOCOLE, origine } = {}) {
    const url = new URL("/rejoindre/" + encodeURIComponent(code), base);
    url.searchParams.set("version", String(version));
    if (jeton) url.searchParams.set("jeton", jeton);
    return new ClientRelais(url, origine);
  }

  constructor(url, origine) {
    this.file = [];
    this.attentes = [];
    this.fermeture = null;
    this.ws = origine ? new WebSocket(url, { headers: { Origin: origine } }) : new WebSocket(url);
    this.ws.binaryType = "arraybuffer";
    this.ws.addEventListener("message", (e) => {
      this.pousser(typeof e.data === "string" ? JSON.parse(e.data) : { type: "binaire", ...decoder(e.data) });
    });
    this.ferme = new Promise((resoudre) => {
      this.ws.addEventListener("close", (e) => {
        this.fermeture = { code: e.code, raison: e.reason };
        resoudre(this.fermeture);
        this.pousser({ type: "ferme", code: e.code, raison: e.reason });
      });
    });
    this.ouvert = new Promise((resoudre, rejeter) => {
      this.ws.addEventListener("open", () => resoudre());
      this.ws.addEventListener("error", () => rejeter(new Error("connexion refusée")));
    });
    this.ouvert.catch(() => {});
  }

  pousser(m) {
    const i = this.attentes.findIndex((a) => a.filtre(m));
    if (i >= 0) {
      const [a] = this.attentes.splice(i, 1);
      clearTimeout(a.minuteur);
      a.resoudre(m);
    } else {
      this.file.push(m);
    }
  }

  /** Prochain message qui vérifie `filtre` (les autres restent en file). */
  attendre(filtre = () => true, delai = 5000) {
    const i = this.file.findIndex(filtre);
    if (i >= 0) return Promise.resolve(this.file.splice(i, 1)[0]);
    return new Promise((resoudre, rejeter) => {
      const a = { filtre, resoudre };
      a.minuteur = setTimeout(() => {
        this.attentes.splice(this.attentes.indexOf(a), 1);
        rejeter(new Error("délai dépassé en attendant un message"));
      }, delai);
      this.attentes.push(a);
    });
  }

  attendreType(type, delai) {
    return this.attendre((m) => m.type === type, delai);
  }

  /** Message « bienvenue » ; garde code, joueur et jeton. */
  async bienvenue() {
    const m = await this.attendre((m) => m.type === "bienvenue" || m.type === "erreur" || m.type === "ferme");
    if (m.type !== "bienvenue") throw Object.assign(new Error("refusé : " + (m.code || m.raison)), { message_: m });
    this.code = m.code;
    this.joueur = m.joueur;
    this.jeton = m.jeton;
    return m;
  }

  envoyer(type, tick, donnees, dest) {
    this.ws.send(encoder(type, tick, donnees, dest));
  }

  envoyerBrut(octets) {
    this.ws.send(octets);
  }

  fermer() {
    this.ws.close(1000, "fin");
    return this.ferme;
  }
}
