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
  PRESENCE: 0x12,
  SIGNAL: 0x13,
  PRET: 0x14,
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

// Taille maximale d'une sauvegarde acceptée à la réception (règle du protocole, côté client).
export const SAUVEGARDE_MAX = 8 * 1024 * 1024;
export const MORCEAU_CONSEILLE = 60000;

/** Identifiant de transfert : u32 tiré au hasard, jamais 0. */
export function nouvelIdentifiant() {
  const v = new Uint32Array(1);
  do crypto.getRandomValues(v); while (v[0] === 0);
  return v[0];
}

/** Découpe une sauvegarde en messages SAUVEGARDE (voir docs/protocole.md). */
export function morceauxSauvegarde(sauvegarde, tick, identifiant = nouvelIdentifiant(), dest = DEST_AUTRES, taille = MORCEAU_CONSEILLE) {
  if (sauvegarde.length > SAUVEGARDE_MAX) throw new Error("sauvegarde trop grosse");
  if (taille < 1 || taille > MORCEAU_MAX) throw new Error("taille de morceau invalide");
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

/**
 * Recolle les morceaux d'une sauvegarde (règles : docs/protocole.md, « SAUVEGARDE »).
 * `ajouter()` retourne la sauvegarde complète, ou null (incomplète, ou morceau rejeté :
 * la raison est alors dans `erreur`). Les morceaux arrivent dans l'ordre : chacun doit
 * commencer exactement où le précédent s'est arrêté, sinon le transfert est abandonné.
 */
export class Reception {
  constructor(max = SAUVEGARDE_MAX) {
    this.max = max;
    this.erreur = null;
    this.raz();
  }

  raz() {
    this.id = null;
    this.tampon = null;
    this.total = 0;
    this.recus = 0;
  }

  rejeter(raison) {
    this.raz();
    this.erreur = raison;
    return null;
  }

  ajouter(donnees) {
    this.erreur = null;
    if (donnees.length < ENTETE_MORCEAU) return this.rejeter("morceau trop court");
    const v = new DataView(donnees.buffer, donnees.byteOffset, donnees.byteLength);
    const id = v.getUint32(0, true);
    const total = v.getUint32(4, true);
    const pos = v.getUint32(8, true);
    const part = donnees.subarray(ENTETE_MORCEAU);

    if (pos === 0) {
      // Début d'un transfert : il remplace celui en cours, quel qu'il soit.
      if (id === 0) return this.rejeter("identifiant nul");
      if (total > this.max) return this.rejeter("sauvegarde trop grosse");
      this.raz();
      this.id = id;
      this.total = total;
      this.tampon = new Uint8Array(total);
    } else if (id !== this.id) {
      // Suite d'un transfert dont on n'a pas vu le début (arrivée en cours d'envoi, ou
      // transfert remplacé) : ignorée, sans toucher au transfert en cours.
      this.erreur = "début du transfert manquant";
      return null;
    } else if (total !== this.total || pos !== this.recus) {
      return this.rejeter("morceau hors séquence (doublon ou trou)");
    }
    if (part.length === 0 && total !== 0) return this.rejeter("morceau vide");
    if (pos + part.length > total) return this.rejeter("morceau hors bornes");

    this.tampon.set(part, pos);
    this.recus += part.length;
    if (this.recus < total) return null;
    const complete = this.tampon;
    this.raz();
    return complete;
  }
}

/**
 * Connexion au relais. `base` = « ws://127.0.0.1:8787 ».
 * Les messages reçus (JSON ou binaires décodés) sont mis en file ; `attendre()` les consomme.
 */
export class ClientRelais {
  // version: null → paramètre absent (pour les tests).
  static creer(base, { joueurs, version = VERSION_PROTOCOLE, origine, info, pseudo } = {}) {
    const url = new URL("/creer", base);
    if (version !== null) url.searchParams.set("version", String(version));
    if (joueurs !== undefined) url.searchParams.set("joueurs", String(joueurs));
    if (info !== undefined) url.searchParams.set("info", info); // M12 : ligne d'information du salon
    if (pseudo !== undefined) url.searchParams.set("pseudo", pseudo); // M12 : pseudo du joueur
    return new ClientRelais(url, origine);
  }

  static rejoindre(base, code, { jeton, version = VERSION_PROTOCOLE, origine, pseudo } = {}) {
    const url = new URL("/rejoindre/" + encodeURIComponent(code), base);
    if (version !== null) url.searchParams.set("version", String(version));
    if (jeton) url.searchParams.set("jeton", jeton);
    if (pseudo !== undefined) url.searchParams.set("pseudo", pseudo); // M12
    return new ClientRelais(url, origine);
  }

  constructor(url, origine) {
    this.file = [];
    this.attentes = [];
    this.fermeture = null;
    this.ws = origine ? new WebSocket(url, { headers: { Origin: origine } }) : new WebSocket(url);
    this.ws.binaryType = "arraybuffer";
    this.ws.addEventListener("message", (e) => {
      if (typeof e.data !== "string") {
        // `type` = type numérique du message (TYPE.TOUR…) ; `binaire` le distingue du JSON.
        // (Avant : { type: "binaire", ...decoder() } → le type numérique écrasait "binaire".)
        this.pousser({ ...decoder(e.data), binaire: true });
      } else if (e.data === "pong") {
        this.pousser({ type: "pong" }); // réponse du runtime au texte « ping », pas du JSON
      } else {
        this.pousser(JSON.parse(e.data));
      }
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

  /** `type` : type JSON (« bienvenue »…), ou « binaire » pour tout message binaire. */
  attendreType(type, delai) {
    return this.attendre((m) => (type === "binaire" ? m.binaire === true : !m.binaire && m.type === type), delai);
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

  /** Hôte seulement : libère la place d'un joueur (son jeton ne marche plus). */
  expulser(joueur) {
    this.ws.send(JSON.stringify({ type: "expulser", joueur }));
  }

  fermer() {
    this.ws.close(1000, "fin");
    return this.ferme;
  }
}
