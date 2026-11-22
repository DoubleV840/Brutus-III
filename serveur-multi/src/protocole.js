// Brutus III : constantes et règles pures du protocole du relais (docs/protocole.md).
//
// Module à part : workerd traite CHAQUE export du module principal (src/index.js) comme un
// gestionnaire (ExportedHandler ou classe). Un export nommé de constante y fait échouer le
// chargement du Worker (« … is not of type ExportedHandler »). Le module principal ne doit
// donc exporter que `default` et la classe `Salon`.

export const VERSION_PROTOCOLE = 1;

// Taille maximale d'un message (binaire ou texte), en octets, en-tête compris.
export const TAILLE_MAX_MESSAGE = 65536;
export const TAILLE_ENTETE = 8;

// Codes de salon : 6 caractères, sans 0/O ni 1/I/L.
export const ALPHABET_CODE = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
export const LONGUEUR_CODE = 6;
export const ESSAIS_CREATION = 8;

// Types des messages binaires (octet 0 de l'en-tête).
export const TYPE = {
  TOUR: 0x01,
  VITESSE: 0x02,
  EMPREINTE: 0x03,
  SAUVEGARDE_DEMANDE: 0x04,
  SAUVEGARDE: 0x05,
  CHAT: 0x10,
  PING: 0x11,
  PRESENCE: 0x12, // M7 : vue et curseur d'un joueur (jamais dans la simulation)
  SIGNAL: 0x13, // M7 : signal posé sur la carte
};
export const TYPES_CONNUS = new Set(Object.values(TYPE));

// Destinataires (octet 2 de l'en-tête). 0 à 3 : un joueur précis.
export const DEST_TOUS = 0xff; // tout le salon, émetteur compris
export const DEST_AUTRES = 0xfe; // tout le salon sauf l'émetteur

// Types qui doivent aller à tout le salon, émetteur compris : sinon le flux diffère d'un
// joueur à l'autre et la partie se désynchronise sans aucune erreur.
export const TYPES_POUR_TOUS = new Set([TYPE.TOUR, TYPE.VITESSE, TYPE.EMPREINTE]);

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

export const MESSAGES = {
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
