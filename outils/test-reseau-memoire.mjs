#!/usr/bin/env node
// Brutus III (M6b) : test du client réseau SANS PORT.
//
// Deux (puis trois) instances wasm du moteur, dans ce même processus Node, pilotées chacune par le
// VRAI client web/lockstep.js, reliées par un relais en mémoire qui reprend les règles de
// serveur-multi/src/index.js (en-tête de 8 octets, destinataires obligatoires, jetons, remplacement
// d'une connexion, messages bienvenue / joueur-arrive / joueur-revenu / joueur-parti / erreur, ping → pong).
// Aucune connexion réseau, aucun port. Livraison asynchrone avec une latence aléatoire (0 à 2 tours
// de boucle) qui garde l'ordre de chaque connexion.
//
// Un bot (outils/bot-rejouage.mjs) fabrique un journal de commandes par joueur ; chaque client injecte
// les commandes de son journal quand son tick les atteint, comme le ferait l'interface.
//
// Scénario (déclenché par la progression de l'hôte) : coupure propre de l'invité (hors ligne 1,5 s),
// coupure SILENCIEUSE de l'invité (le relais jette tout sans fermer ; retour sans joueur-parti avant),
// resynchronisation forcée (invité puis hôte), pause et vitesses partagées, coupure de l'hôte,
// PANNE INJECTÉE (une commande parasite sur l'invité seul : la divergence doit être détectée et
// réparée), rechargement de la page de l'hôte (nouvelle instance du moteur, état perdu).
//
//   node outils/test-reseau-memoire.mjs --ticks N --carte NOM --depart sauvegarde.svx --jeu dossier \
//        --module build-reseau-wasm/augustus.js --journaux hote.txt,invite.txt --rapport rapport.txt
//
// Critère de réussite : aucune divergence RÉELLE (hors la panne injectée), la panne détectée une fois
// et réparée, tous les événements du scénario réalisés, états finaux identiques.
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, appendFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { performance } from 'node:perf_hooks';

const require = createRequire(import.meta.url);
const args = process.argv.slice(2);
const opt = (nom, defaut) => {
  const i = args.indexOf(nom);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : defaut;
};
const TICKS = Number(opt('--ticks', '225000'));
const CARTE = opt('--carte', 'Lugdunum');
const DEPART = resolve(opt('--depart', 'build-reseau/depart-Lugdunum.svx'));
const JEU = resolve(opt('--jeu', 'jeu-original/app/jeu1'));
const MODULE = resolve(opt('--module', 'build-reseau-wasm/augustus.js'));
const RAPPORT = resolve(opt('--rapport', 'build-reseau/rapport-memoire.txt'));
const JOURNAUX = opt('--journaux', '').split(',').filter(Boolean);
const GRAINE_RELAIS = Number(opt('--graine-relais', '7'));
const SANS_RECHARGEMENT = args.includes('--sans-rechargement');
const DEBUG = args.includes('--debug');
// Mode « relais réel » : vrai WebSocket vers un relais qui tourne (wrangler dev), cadence réelle du jeu.
const RELAIS_URL = opt('--relais', '');
const REEL = RELAIS_URL !== '';
const VITESSE = Number(opt('--vitesse', '500'));
// Pour vérifier le mode réel SANS port : un « faux WebSocket » branché sur le relais en mémoire.
const FAUX_WS = args.includes('--faux-ws');
// Constantes de PRODUCTION de web/lockstep.js (au lieu des délais accélérés du test) : empreinte tous les
// 50 groupes, ping 20 s, coupure silencieuse à 45 s, attente 10 s, cadence réelle du jeu, pompe aussi
// déclenchée par les messages. Durée réelle : au moins 20 minutes à 200 % pour 225000 ticks.
const PROD = args.includes('--constantes-production');
const TEMPS_REEL = REEL || PROD; // le jeu suit l'horloge réelle (pas de « groupes dès qu'ils sont complets »)
const EMPREINTE_GROUPES = Number(opt('--empreinte-groupes', PROD ? '50' : '10')); // 10 : une empreinte tous les 50 ticks

const LS = require(fileURLToPath(new URL('../web/lockstep.js', import.meta.url)));
const BrutusModule = require(MODULE);
const { TYPE } = LS;

mkdirSync(dirname(RAPPORT), { recursive: true });
const FICHIER_JOURNAL = RAPPORT.replace(/\.txt$/, '') + '-journal.txt';
writeFileSync(FICHIER_JOURNAL, '');
const debut = performance.now();
const secondes = () => ((performance.now() - debut) / 1000).toFixed(1).padStart(7);
function ecrire(ligne) {
  appendFileSync(FICHIER_JOURNAL, `${secondes()} s  ${ligne}\n`);
}
function dire(ligne) {
  console.log(ligne);
  ecrire(ligne);
}

// --- Aléatoire reproductible (mulberry32), pour la latence du relais ---
let etatAlea = GRAINE_RELAIS >>> 0;
function alea() {
  etatAlea = (etatAlea + 0x6d2b79f5) >>> 0;
  let t = etatAlea;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const entier = (min, max) => min + Math.floor(alea() * (max - min + 1));

// ============================================================ relais en mémoire

const ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
const TAILLE_MAX = 65536;
const TYPES_POUR_TOUS = new Set([TYPE.TOUR, TYPE.VITESSE, TYPE.EMPREINTE]);
const TYPES_CONNUS = new Set(Object.values(TYPE));
const FERMETURE = { version: 4000, 'salon-inconnu': 4001, 'salon-plein': 4002, 'jeton-invalide': 4003, 'requete-invalide': 4004, remplace: 4005, expulse: 4006, 'message-trop-gros': 4009 };

class Relais {
  constructor() {
    this.it = 0;
    this.conns = [];
    this.salons = new Map();
    this.bloques = new Map(); // « code:joueur » → instant (ms) jusqu'auquel la reconnexion échoue
    this.stats = { binaires: 0, textes: 0, octets: 0, parType: {}, erreursEnvoyees: {}, remplacements: 0, departs: 0, arrivees: 0, retours: 0, retoursSansDepart: 0, jetes: 0 };
    this.dernierDepart = new Map(); // « code:joueur » → vrai si un joueur-parti a été annoncé depuis le dernier retour
    this.latence = null; // { min, max } en ms par trajet (client → relais, relais → client) : cas « forte latence »
  }

  retarder(c, item, sens) {
    if (this.latence) {
      const cle = sens === 'mont' ? 'dueMontMs' : 'dueDescMs';
      item.dueMs = Math.max(c[cle] || 0, performance.now() + entier(this.latence.min, this.latence.max));
      c[cle] = item.dueMs;
    }
  }

  // Transport injecté dans creerLockstep.
  ouvrir(url, ecouteur) {
    const c = { id: this.conns.length + 1, ecouteur, joueur: -1, salon: null, actif: false, silence: false, fermeClient: false, fermeRelais: false, montant: [], descendant: [], dueMont: 0, dueDesc: 0 };
    this.conns.push(c);
    this.monter(c, { kind: 'ouvrir', url });
    const relais = this;
    return {
      envoyer(m) {
        if (c.fermeClient) {
          return false;
        }
        relais.monter(c, { kind: 'msg', m: typeof m === 'string' ? m : new Uint8Array(m) });
        return true;
      },
      fermer() {
        if (!c.fermeClient) {
          c.fermeClient = true;
          relais.monter(c, { kind: 'fermer' });
        }
      },
      _c: c,
    };
  }

  monter(c, item) {
    item.due = Math.max(c.dueMont, this.it + entier(0, 2));
    c.dueMont = item.due;
    this.retarder(c, item, 'mont');
    c.montant.push(item);
  }

  descendre(c, item) {
    item.due = Math.max(c.dueDesc, this.it + entier(0, 2));
    c.dueDesc = item.due;
    this.retarder(c, item, 'desc');
    c.descendant.push(item);
  }

  // Un tour de boucle : le relais traite ce que les clients lui ont envoyé, puis livre aux clients.
  tic(it) {
    this.it = it;
    const maintenant = performance.now();
    const arrive = (item) => item.due <= it && !(item.dueMs > maintenant);
    for (const c of this.conns) {
      while (c.montant.length && arrive(c.montant[0])) {
        const item = c.montant.shift();
        if (c.silence) {
          this.stats.jetes++;
          continue;
        }
        this.traiter(c, item);
      }
    }
    for (const c of this.conns) {
      while (c.descendant.length && arrive(c.descendant[0])) {
        const item = c.descendant.shift();
        if (c.silence) {
          this.stats.jetes++;
          continue;
        }
        if (item.kind === 'ouvert') {
          c.ecouteur.ouvert();
        } else if (item.kind === 'msg') {
          c.ecouteur.message(item.m);
        } else if (item.kind === 'ferme') {
          c.ecouteur.ferme(item.code);
        } else if (item.kind === 'erreur') {
          c.ecouteur.erreur();
        }
      }
    }
    // Les connexions finies et vides sont oubliées (la liste reste courte sur 30 minutes de jeu).
    if (it % 1000 === 0) {
      this.conns = this.conns.filter((c) => !((c.fermeClient || c.fermeRelais) && !c.montant.length && !c.descendant.length));
    }
  }

  // ---- ce que le test peut faire subir au réseau
  couper(c) { // coupure propre : le relais ferme la connexion
    this.fermerParRelais(c, 1006, 'coupure');
  }
  silence(c) { // coupure silencieuse : plus rien ne passe, aucune fermeture
    c.silence = true;
  }
  bloquer(code, joueur, jusqua) {
    this.bloques.set(code + ':' + joueur, jusqua);
  }

  // ---- règles du relais
  actives(salon) {
    return [...salon.conns].filter((c) => c.actif && !c.fermeRelais);
  }

  etatJoueurs(salon, actives) {
    const connectes = new Set(actives.map((c) => c.joueur));
    const etat = [];
    salon.jetons.forEach((jeton, joueur) => {
      if (jeton !== null) {
        etat.push({ joueur, connecte: connectes.has(joueur) });
      }
    });
    return etat;
  }

  texteErreur(code, message) {
    this.stats.erreursEnvoyees[code] = (this.stats.erreursEnvoyees[code] || 0) + 1;
    return JSON.stringify({ type: 'erreur', code, message: message || code });
  }

  refuser(c, code) {
    this.descendre(c, { kind: 'ouvert' });
    this.descendre(c, { kind: 'msg', m: this.texteErreur(code) });
    this.descendre(c, { kind: 'ferme', code: FERMETURE[code] });
    c.fermeRelais = true;
  }

  fermerParRelais(c, code, raison) {
    if (c.fermeRelais) {
      return;
    }
    this.descendre(c, { kind: 'ferme', code });
    c.fermeRelais = true;
    this.depart(c);
  }

  annoncer(salon, message, sauf, restantes) {
    const actives = restantes || this.actives(salon);
    const texte = JSON.stringify({ ...message, joueurs: this.etatJoueurs(salon, actives) });
    for (const c of actives) {
      if (c !== sauf) {
        this.descendre(c, { kind: 'msg', m: texte });
      }
    }
  }

  traiter(c, item) {
    if (item.kind === 'ouvrir') {
      this.accepter(c, item.url);
    } else if (item.kind === 'fermer') {
      this.depart(c);
    } else if (item.kind === 'msg' && c.actif && !c.fermeRelais) {
      if (typeof item.m === 'string') {
        this.texte(c, item.m);
      } else {
        this.binaire(c, item.m);
      }
    }
  }

  accepter(c, url) {
    const u = new URL(url);
    if (u.searchParams.get('version') !== '1') {
      return this.refuser(c, 'version');
    }
    if (u.pathname === '/creer') {
      const demande = u.searchParams.get('joueurs');
      const joueurs = demande === null ? 2 : Number(demande);
      if (!Number.isInteger(joueurs) || joueurs < 2 || joueurs > 4) {
        return this.refuser(c, 'requete-invalide');
      }
      let code;
      do {
        code = '';
        for (let i = 0; i < 6; i++) {
          code += ALPHABET[entier(0, ALPHABET.length - 1)];
        }
      } while (this.salons.has(code));
      const salon = { code, joueursMax: joueurs, jetons: [this.jeton()], conns: new Set() };
      this.salons.set(code, salon);
      return this.accepterJoueur(c, salon, 0, false);
    }
    const m = u.pathname.match(/^\/rejoindre\/([^/]+)$/);
    if (!m) {
      return this.refuser(c, 'requete-invalide');
    }
    const salon = this.salons.get(m[1].toUpperCase());
    if (!salon) {
      return this.refuser(c, 'salon-inconnu');
    }
    const jeton = u.searchParams.get('jeton');
    if (jeton) {
      const joueur = salon.jetons.indexOf(jeton);
      if (joueur < 0) {
        return this.refuser(c, 'jeton-invalide');
      }
      const jusqua = this.bloques.get(salon.code + ':' + joueur) || 0;
      if (performance.now() < jusqua) {
        // Réseau injoignable : la connexion n'aboutit pas (erreur côté client, sans message du relais).
        this.descendre(c, { kind: 'erreur' });
        c.fermeRelais = true;
        return;
      }
      for (const ancienne of this.actives(salon)) {
        if (ancienne.joueur === joueur) {
          ancienne.actif = false;
          this.stats.remplacements++;
          this.descendre(ancienne, { kind: 'msg', m: this.texteErreur('remplace') });
          this.descendre(ancienne, { kind: 'ferme', code: FERMETURE.remplace });
          ancienne.fermeRelais = true;
        }
      }
      return this.accepterJoueur(c, salon, joueur, true);
    }
    let joueur = salon.jetons.indexOf(null);
    if (joueur < 0) {
      if (salon.jetons.length >= salon.joueursMax) {
        return this.refuser(c, 'salon-plein');
      }
      joueur = salon.jetons.length;
    }
    salon.jetons[joueur] = this.jeton();
    return this.accepterJoueur(c, salon, joueur, false);
  }

  jeton() {
    let t = '';
    for (let i = 0; i < 16; i++) {
      t += entier(0, 255).toString(16).padStart(2, '0');
    }
    return t;
  }

  accepterJoueur(c, salon, joueur, reconnexion) {
    c.joueur = joueur;
    c.salon = salon;
    c.actif = true;
    salon.conns.add(c);
    this.descendre(c, { kind: 'ouvert' });
    this.descendre(c, {
      kind: 'msg',
      m: JSON.stringify({ type: 'bienvenue', version: 1, code: salon.code, joueur, jeton: salon.jetons[joueur], joueursMax: salon.joueursMax, reconnexion, joueurs: this.etatJoueurs(salon, this.actives(salon)) }),
    });
    if (reconnexion) {
      this.stats.retours++;
      const cle = salon.code + ':' + joueur;
      if (!this.dernierDepart.get(cle)) {
        this.stats.retoursSansDepart++; // « joueur-revenu » sans « joueur-parti » avant
      }
      this.dernierDepart.set(cle, false);
    } else {
      this.stats.arrivees++;
    }
    this.annoncer(salon, { type: reconnexion ? 'joueur-revenu' : 'joueur-arrive', joueur }, c);
  }

  texte(c, texte) {
    this.stats.textes++;
    if (texte === 'ping') {
      this.descendre(c, { kind: 'msg', m: 'pong' });
      return;
    }
    let m = null;
    try {
      m = JSON.parse(texte);
    } catch (e) {
      m = null;
    }
    if (!m || m.type !== 'expulser') {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('message-invalide') });
      return;
    }
    if (c.joueur !== 0) {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('reserve-a-l-hote') });
      return;
    }
    const salon = c.salon;
    const cible = m.joueur;
    if (!Number.isInteger(cible) || cible < 1 || cible >= salon.jetons.length || salon.jetons[cible] === null) {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('message-invalide') });
      return;
    }
    salon.jetons[cible] = null;
    for (const autre of this.actives(salon)) {
      if (autre.joueur === cible) {
        autre.actif = false;
        this.descendre(autre, { kind: 'msg', m: this.texteErreur('expulse') });
        this.descendre(autre, { kind: 'ferme', code: FERMETURE.expulse });
        autre.fermeRelais = true;
      }
    }
    this.annoncer(salon, { type: 'joueur-expulse', joueur: cible }, null);
  }

  binaire(c, octets) {
    const salon = c.salon;
    if (octets.length > TAILLE_MAX) {
      c.actif = false;
      this.descendre(c, { kind: 'msg', m: this.texteErreur('message-trop-gros') });
      this.descendre(c, { kind: 'ferme', code: FERMETURE['message-trop-gros'] });
      c.fermeRelais = true;
      this.annoncerDepart(salon, c);
      return;
    }
    if (octets.length < 8 || !TYPES_CONNUS.has(octets[0])) {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('message-invalide') });
      return;
    }
    const type = octets[0];
    const dest = octets[2];
    if (dest !== 255 && dest !== 254 && dest >= salon.joueursMax) {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('message-invalide') });
      return;
    }
    if ((TYPES_POUR_TOUS.has(type) && dest !== 255) || (type === TYPE.SAUVEGARDE && c.joueur !== 0 && dest !== 0)) {
      this.descendre(c, { kind: 'msg', m: this.texteErreur('destinataire-interdit') });
      return;
    }
    this.stats.binaires++;
    this.stats.octets += octets.length;
    this.stats.parType[type] = (this.stats.parType[type] || 0) + 1;
    octets[1] = c.joueur; // le relais écrit l'émetteur
    for (const cible of this.actives(salon)) {
      if (dest === 255 || (dest === 254 && cible !== c) || dest === cible.joueur) {
        this.descendre(cible, { kind: 'msg', m: new Uint8Array(octets) });
      }
    }
  }

  depart(c) {
    if (!c.actif) {
      return;
    }
    c.actif = false;
    this.annoncerDepart(c.salon, c);
  }

  annoncerDepart(salon, c) {
    const restantes = this.actives(salon).filter((autre) => autre !== c);
    if (!restantes.some((autre) => autre.joueur === c.joueur)) {
      this.stats.departs++;
      this.dernierDepart.set(salon.code + ':' + c.joueur, true);
      this.annoncer(salon, { type: 'joueur-parti', joueur: c.joueur }, null, restantes);
    }
  }
}

// ============================================================ clients

const relais = new Relais();

// Faux WebSocket (option --faux-ws) : même interface que celui de Node, branché sur le relais en mémoire.
if (FAUX_WS) {
  globalThis.WebSocket = class FauxWebSocket {
    constructor(url) {
      this.readyState = 0;
      const moi = this;
      this._h = relais.ouvrir(url, {
        ouvert() { moi.readyState = 1; if (moi.onopen) moi.onopen(); },
        message(m) {
          if (moi.onmessage) moi.onmessage({ data: typeof m === 'string' ? m : m.buffer.slice(m.byteOffset, m.byteOffset + m.byteLength) });
        },
        ferme(code) { moi.readyState = 3; if (moi.onclose) moi.onclose({ code }); },
        erreur() { if (moi.onerror) moi.onerror(); },
      });
    }
    send(m) { this._h.envoyer(m); }
    close() { this.readyState = 3; this._h.fermer(); }
  };
}

// Transport du mode réel : le WebSocket de Node (client web/lockstep.js, le même que dans la page),
// avec un interrupteur « muet » par connexion pour simuler un réseau qui ne répond plus sans fermer.
const transportReel = {
  ouvrir(url, ecouteur) {
    const base = LS.transportWebSocket();
    const poignee = { muet: false };
    const h = base.ouvrir(url, {
      ouvert: () => ecouteur.ouvert(),
      message: (m) => { if (!poignee.muet) ecouteur.message(m); },
      ferme: (c) => ecouteur.ferme(c),
      erreur: () => ecouteur.erreur(),
    });
    poignee.envoyer = (m) => (poignee.muet ? true : h.envoyer(m));
    poignee.fermer = () => h.fermer();
    return poignee;
  },
};

// Coupures du scénario : côté relais en mémoire, ou côté client avec le vrai relais.
function coupurePropre(client, dureeMs) {
  if (REEL) {
    client.lock.couperConnexion();
  } else {
    relais.bloquer(client.lock.interne.code, client.joueurAttendu, performance.now() + dureeMs);
    relais.couper(client.s().conn._c);
  }
}
function coupureSilencieuse(client) {
  if (REEL) {
    client.s().conn.muet = true;
  } else {
    relais.silence(client.s().conn._c);
  }
}
const respirer = () => (TEMPS_REEL ? new Promise((r) => setTimeout(r, 2)) : Promise.resolve());

function lireJournal(chemin) {
  const cmds = [];
  for (const ligne of readFileSync(chemin, 'utf8').split('\n')) {
    if (!ligne || ligne.startsWith('#')) {
      continue;
    }
    const [tick, , nom, ...params] = ligne.trim().split(/\s+/);
    cmds.push({ tick: Number(tick), nom, params: params.map(Number) });
  }
  return cmds;
}

function parametresLockstep(nom, extra) {
  if (PROD) {
    // Aucune des constantes de temps n'est donnée : ce sont celles de web/lockstep.js (production).
    return {
      libre: false,
      pomperSurMessage: true,
      empreinteGroupes: EMPREINTE_GROUPES,
      chemin: '/tmp/brutus-' + nom + '.svx',
      base: REEL ? RELAIS_URL : 'mem://relais',
      ...extra,
    };
  }
  return {
    libre: !TEMPS_REEL,
    maxGroupesParPompe: 8,
    empreinteGroupes: EMPREINTE_GROUPES,
    pingMs: REEL ? 2000 : 1000,
    morteMs: REEL ? 8000 : 4000,
    attenteMs: 1500,
    essaisMs: REEL ? [1000, 1500, 2000] : [200, 400, 800],
    demandeSauvegardeMs: REEL ? 1000 : 500,
    reessaiResyncMs: REEL ? 500 : 200,
    chemin: '/tmp/brutus-' + nom + '.svx',
    base: REEL ? RELAIS_URL : 'mem://relais',
    ...extra,
  };
}

// Empreintes vues par les clients : « époque:tick » → { joueur → empreinte }.
const empreintesVues = new Map();
const mismatches = [];
let panne = null; // { epoque, tick } : la panne injectée

function noterEmpreinte(tick, joueur, h, epoque) {
  const cle = epoque + ':' + tick;
  let e = empreintesVues.get(cle);
  if (!e) {
    e = {};
    empreintesVues.set(cle, e);
    if (empreintesVues.size > 20000) {
      empreintesVues.delete(empreintesVues.keys().next().value);
    }
  }
  e[joueur] = h;
  const valeurs = Object.values(e);
  if (valeurs.length >= 2 && new Set(valeurs).size > 1 && !e.signale) {
    e.signale = true;
    const attendue = panne !== null && panne.epoque === epoque && tick >= panne.tick;
    mismatches.push({ epoque, tick, attendue, valeurs: { ...e } });
    ecrire(`DIVERGENCE ${attendue ? 'attendue (panne injectée)' : 'RÉELLE'} : époque ${epoque}, tick ${tick} : ${JSON.stringify(e)}`);
  }
}

class Client {
  constructor(nom, joueurAttendu, journal, stockage) {
    this.nom = nom;
    this.journal = journal;
    this.idx = 0;
    this.stockage = stockage || { v: null, lire() { return this.v; }, ecrire(x) { this.v = x; }, effacer() { this.v = null; } };
    this.joueurAttendu = joueurAttendu;
    this.mort = false;
    this.injectees = 0;
    this.phases = {};
    this.types = {};
  }

  async demarrer(extraLock) {
    const sortie = [];
    const M = await BrutusModule({
      arguments: ['--reseau', '/hote' + DEPART, '/hote' + JEU],
      print: (l) => { sortie.push(l); },
      printErr: () => {},
    });
    this.M = M;
    this.sortie = sortie;
    this.moteur = LS.moteurEmscripten(M);
    const nom = this.nom;
    this.lock = LS.creerLockstep(parametresLockstep(nom, {
      moteur: this.moteur,
      transport: REEL ? transportReel : relais,
      horloge: { maintenant: () => performance.now() },
      stockage: this.stockage,
      journal: (l) => ecrire(`[${nom}] ${l}`),
      surEmpreinte: noterEmpreinte,
      surChangement: (e) => {
        this.phases[e.phase] = (this.phases[e.phase] || 0) + 1;
        ecrire(`[${nom}] phase « ${e.phase} »${e.texte ? ' : ' + e.texte : ''}`);
      },
      ...extraLock,
    }));
  }

  pointeurTampon() {
    return this.M.ccall('brutus_reseau_tampon', 'number', [], []);
  }

  typeParNom(nom) {
    if (this.types[nom] === undefined) {
      const oct = new TextEncoder().encode(nom);
      this.M.HEAPU8.set(oct, this.pointeurTampon());
      this.M.HEAPU8[this.pointeurTampon() + oct.length] = 0;
      this.types[nom] = this.M.ccall('brutus_reseau_type_par_nom', 'number', ['number'], [this.pointeurTampon()]);
    }
    return this.types[nom];
  }

  octetsCommande(nom, params) {
    const type = this.typeParNom(nom);
    const o = new Uint8Array(2 + params.length * 4);
    o[0] = type;
    o[1] = params.length;
    const dv = new DataView(o.buffer);
    params.forEach((p, i) => dv.setInt32(2 + i * 4, p, true));
    return { type, octets: o };
  }

  // Ce que fait l'interface : la commande part dans la boîte de sortie, puis dans le prochain TOUR.
  injecter(cmd) {
    const type = this.typeParNom(cmd.nom);
    if (!type) {
      return;
    }
    const dv = new DataView(this.M.HEAPU8.buffer, this.pointeurTampon(), 4 * cmd.params.length);
    cmd.params.forEach((p, i) => dv.setInt32(i * 4, p, true));
    this.M.ccall('brutus_reseau_injecter', 'number', ['number', 'number'], [type, cmd.params.length]);
    this.injectees++;
  }

  pomper() {
    if (this.mort) {
      return;
    }
    const s = this.lock.interne;
    if (s.actif && s.connecte) {
      const t = this.moteur.tick();
      let n = 0;
      while (this.idx < this.journal.length && this.journal[this.idx].tick <= t && n < 30) {
        this.injecter(this.journal[this.idx]);
        this.idx++;
        n++;
      }
    }
    this.lock.pomper();
  }

  tick() { return this.moteur.tick(); }
  s() { return this.lock.interne; }
}

// ============================================================ scénario

async function principal() {
  dire(`Test réseau sans port : carte ${CARTE}, ${TICKS} ticks, une empreinte tous les ${EMPREINTE_GROUPES * 5} ticks.`);
  const journaux = JOURNAUX.map(lireJournal);
  while (journaux.length < 2) {
    journaux.push([]);
  }
  dire(`Journaux du bot : ${journaux.map((j) => j.length + ' commandes').join(', ')}.`);

  let hote = new Client('hôte', 0, journaux[0]);
  let invite = new Client('invité', 1, journaux[1]);
  let premierInvite = null; // l'onglet remplacé (cas des deux onglets)
  await hote.demarrer();
  await invite.demarrer();
  dire(`Deux instances du moteur prêtes (${secondes().trim()} s).`);
  const tick0 = hote.tick();
  const cible = tick0 + TICKS;

  hote.lock.creer(2);
  let it = 0;
  const clients = () => [hote, invite];
  const pomperTout = () => {
    it++;
    if (!REEL || FAUX_WS) relais.tic(it);
    for (const c of clients()) {
      c.pomper();
    }
  };

  // Salon : l'hôte crée, l'invité rejoint avec le code.
  while (!hote.s().code && it < 5000) {
    pomperTout();
    await respirer();
  }
  if (!hote.s().code) {
    throw new Error('Le salon n’a pas été créé.');
  }
  invite.lock.rejoindre(hote.s().code);
  dire(`Salon ${hote.s().code} : l'invité rejoint.`);
  while ((!hote.s().actif || !invite.s().actif) && it < 20000) {
    pomperTout();
    await respirer();
  }
  if (!hote.s().actif || !invite.s().actif) {
    throw new Error('La partie à deux n’a pas démarré (resynchronisation d’arrivée).');
  }
  dire(`Partie à deux démarrée au tick ${hote.tick()} (époque ${hote.s().epoque}).`);

  if (TEMPS_REEL) {
    // Le jeu tourne à la cadence réelle : on monte la vitesse partagée jusqu'à la vitesse demandée.
    dire(`Vitesse de départ ${hote.moteur.vitesse()} %, cible ${VITESSE} %.`);
    for (let essai = 0; essai < 20 && hote.moteur.vitesse() < VITESSE; essai++) {
      M_vitesse(hote, 1);
      for (let i = 0; i < 100; i++) {
        pomperTout();
        await respirer();
      }
    }
    dire(`Vitesse partagée : hôte ${hote.moteur.vitesse()} %, invité ${invite.moteur.vitesse()} %.`);
  }

  const evenements = [];
  const faits = {};
  let outage = null; // { fin, nom, attenteVue }
  const attenteVue = {};
  let pauseTest = null;
  let pauseVue = false;
  let attendreFin = null;

  function evenement(nom, fraction, action, pret) {
    evenements.push({ nom, fraction, action, pret, fait: false });
  }

  // 1. coupure propre de l'invité (hors ligne 1,5 s)
  evenement('coupure propre de l\'invité', 0.10, () => {
    coupurePropre(invite, 1500);
    outage = { nom: 'coupure propre invité', fin: performance.now() + 1500 };
    attenteVue.propre = false;
  });
  // 2. coupure silencieuse de l'invité
  evenement('coupure silencieuse de l\'invité', 0.25, () => {
    coupureSilencieuse(invite);
    // Production : la coupure n'est détectée qu'après 45 s sans message (morteMs).
    outage = { nom: 'coupure silencieuse invité', fin: performance.now() + (PROD ? 50000 : 6000) };
    attenteVue.silencieuse = false;
  });
  // 3. resynchronisation forcée : invité puis hôte
  evenement('resynchronisation forcée (demandée par l\'invité)', 0.40, () => invite.lock.forcerResync());
  evenement('resynchronisation forcée (demandée par l\'hôte)', 0.42, () => hote.lock.forcerResync());
  // 4. pause et vitesses partagées
  evenement('pause partagée (hôte), reprise (invité)', 0.50, () => {
    M_pause(hote);
    pauseTest = { etape: 0, depuis: it };
  });
  evenement('vitesses simultanées (hôte +1, invité -1)', 0.52, () => {
    M_vitesse(hote, 1);
    M_vitesse(invite, -1);
    attendreFin = { it, verifier: 'vitesse' };
  });
  // 5. coupure propre de l'hôte
  evenement('coupure propre de l\'hôte', 0.60, () => {
    coupurePropre(hote, 1000);
    outage = { nom: 'coupure propre hôte', fin: performance.now() + 1000 };
    attenteVue.hote = false;
  });
  // 6. panne injectée : une commande parasite sur l'invité seul
  evenement('PANNE INJECTÉE : commande parasite sur l\'invité seul', 0.70, () => {
    const { octets } = invite.octetsCommande('impots', [3]);
    const t = invite.tick();
    invite.moteur.entree(1, t, octets);
    panne = { epoque: invite.s().epoque, tick: t, resyncsAvant: hote.lock.stats.resyncsHote, divergencesAvant: hote.lock.stats.divergences };
    dire(`PANNE INJECTÉE au tick ${t} (époque ${panne.epoque}) : impôts +3 sur l'invité seul.`);
  }, () => th0() - tickEpoque > 400);
  // 7. rechargement de la page de l'hôte
  if (!SANS_RECHARGEMENT) {
    evenement('rechargement de la page de l\'hôte (nouvelle instance du moteur)', 0.80, () => {
      attendreFin = { it, rechargerHote: true };
    }, () => !!panne && (mismatches.some((m) => m.attendue) && hote.s().epoque !== panne.epoque));
  }

  // Dernier changement d'époque vu (une resynchronisation répare tout : la panne ne doit pas tomber juste avant).
  let epoqueVue = hote.s().epoque;
  let tickEpoque = hote.tick();
  const th0 = () => hote.tick();
  let prochainAffichage = tick0 + 20000;
  let rechargeFaite = null;
  let finDemandee = false;
  let dernierProgres = performance.now();
  let dernierTickVu = hote.tick();

  while (true) {
    pomperTout();
    const th = hote.tick();
    if (th !== dernierTickVu) {
      dernierTickVu = th;
      dernierProgres = performance.now();
    }
    if (performance.now() - dernierProgres > 120000) {
      throw new Error(`Plus aucun progrès depuis 120 s (hôte au tick ${th}, phase ${hote.lock.etat().phase}, invité ${invite.lock.etat().phase}).`);
    }
    const fraction = (th - tick0) / TICKS;
    if (hote.s().epoque !== epoqueVue) {
      epoqueVue = hote.s().epoque;
      tickEpoque = th;
    }
    if (th > cible + 400000) {
      throw new Error(`Scénario bloqué : tick ${th} très au-delà de la cible ${cible} ; événements non faits : ${evenements.filter((e) => !e.fait).map((e) => e.nom).join(' ; ')}.`);
    }

    if (th >= prochainAffichage) {
      prochainAffichage += 20000;
      dire(`  tick ${th} / ${cible} (${Math.round(fraction * 100)} %), époque ${hote.s().epoque}, ${hote.lock.stats.resyncsHote} resynchronisation(s), ${mismatches.length} écart(s)`);
    }

    // coupure en cours : observer le message « en attente de … » du côté qui reste
    if (outage) {
      const reste = outage.nom.includes('hôte') ? invite : hote;
      const e = reste.lock.etat();
      if (e.phase === 'attente' && /en attente de/.test(e.texte)) {
        if (outage.nom.includes('silencieuse')) attenteVue.silencieuse = e.texte;
        else if (outage.nom.includes('hôte')) attenteVue.hote = e.texte;
        else attenteVue.propre = e.texte;
      }
      if (performance.now() > outage.fin && hote.s().actif && invite.s().actif && hote.lock.etat().phase !== 'attente' && invite.lock.etat().phase !== 'attente') {
        outage = null;
      }
    }

    // pause partagée
    if (pauseTest) {
      if (pauseTest.etape === 0 && it - pauseTest.depuis > (TEMPS_REEL ? 300 : 60)) {
        pauseVue = hote.moteur.enPause() && invite.moteur.enPause();
        dire(`  pause : hôte ${hote.moteur.enPause()}, invité ${invite.moteur.enPause()} (ticks ${hote.tick()} / ${invite.tick()})`);
        M_pause(invite); // l'invité reprend
        pauseTest.etape = 1;
        pauseTest.depuis = it;
        pauseTest.tick = hote.tick();
      } else if (pauseTest.etape === 1 && it - pauseTest.depuis > (TEMPS_REEL ? 300 : 60)) {
        faits.pause = { vue: pauseVue, reprise: hote.tick() > pauseTest.tick && !hote.moteur.enPause() && !invite.moteur.enPause() };
        dire(`  reprise : ${faits.pause.reprise}`);
        pauseTest = null;
      }
    }
    if (attendreFin && attendreFin.verifier === 'vitesse' && it - attendreFin.it > (TEMPS_REEL ? 400 : 80)) {
      faits.vitesse = { hote: hote.moteur.vitesse(), invite: invite.moteur.vitesse(), identiques: hote.moteur.vitesse() === invite.moteur.vitesse() };
      dire(`  vitesses : hôte ${faits.vitesse.hote} %, invité ${faits.vitesse.invite} %`);
      attendreFin = null;
    }

    // rechargement de la page de l'hôte
    if (attendreFin && attendreFin.rechargerHote) {
      attendreFin = null;
      const stockage = hote.stockage;
      const anciennes = { ...hote.lock.stats };
      hote.lock.interne.conn.fermer(); // la page disparaît : sa connexion se ferme
      hote.mort = true;
      const ancienIdx = hote.idx;
      const ancien = hote;
      hote = new Client('hôte (page rechargée)', 0, journaux[0], stockage);
      hote.idx = ancienIdx; // le bot reprend où il en était (les commandes déjà parties ne sont pas rejouées)
      await hote.demarrer();
      hote.ancien = anciennes;
      ancien.M = null; // libère l'ancienne instance
      ancien.moteur = null;
      const ok = hote.lock.reprendre();
      dire(`Page de l'hôte rechargée (nouvelle instance, tick ${hote.tick()}), reprise de session : ${ok}.`);
      rechargeFaite = { debut: performance.now(), epoque: invite.s().epoque, ticksAvant: invite.tick() };
    }
    if (rechargeFaite && hote.s().actif && hote.s().epoque !== rechargeFaite.epoque && !rechargeFaite.fini) {
      rechargeFaite.fini = true;
      rechargeFaite.duree = performance.now() - rechargeFaite.debut;
      dire(`  hôte rechargé : partie récupérée auprès de l'invité en ${(rechargeFaite.duree / 1000).toFixed(1)} s, tick ${hote.tick()}, époque ${hote.s().epoque}.`);
    }

    // déclenchement des événements
    if (DEBUG && it % 20 === 0 && evenements.some((e) => !e.fait && fraction >= e.fraction)) {
      dire(`  [debug] it ${it} tick ${th} : outage ${!!outage}, pause ${!!pauseTest}, attendreFin ${!!attendreFin}, recharge ${!!rechargeFaite}, actifs ${hote.s().actif}/${invite.s().actif}, phases ${hote.lock.etat().phase}/${invite.lock.etat().phase}`);
    }
    for (const ev of evenements) {
      if (!ev.fait && fraction >= ev.fraction && (!ev.pret || ev.pret()) && !outage && !pauseTest && !attendreFin && !(rechargeFaite && !rechargeFaite.fini) && hote.s().actif && invite.s().actif && ['jeu', 'resync'].includes(hote.lock.etat().phase) && ['jeu', 'resync'].includes(invite.lock.etat().phase)) {
        ev.fait = true;
        ev.tick = th;
        dire(`ÉVÉNEMENT à ${Math.round(fraction * 100)} % (tick ${th}) : ${ev.nom}`);
        ev.action();
        break;
      }
    }

    if (th >= cible && !finDemandee && evenements.every((e) => e.fait) && !outage && !pauseTest && !attendreFin && !(rechargeFaite && !rechargeFaite.fini)) {
      finDemandee = true;
    }
    if (finDemandee) {
      break;
    }
    if (TEMPS_REEL) {
      await respirer();
    } else if (it % 300 === 0) {
      await new Promise((r) => setImmediate(r)); // laisse respirer la boucle d'événements
    }
  }

  // Cas complémentaires (relecture QA de M6b) : menus refusés, vitesse locale, ville quittée, échecs.
  const compl = await jouerComplements();

  async function jouerComplements() {
    const res = {};
    const ATT = PROD ? 25000 : 10000; // l'attente (« en attente de… ») s'affiche après 10 s en production
    const enJeu = () => hote.s().actif && invite.s().actif && hote.lock.etat().phase === 'jeu' && invite.lock.etat().phase === 'jeu' && hote.s().epoque === invite.s().epoque;
    async function attendre(cond, msMax) {
      const t0 = performance.now();
      let n = 0;
      while (performance.now() - t0 < msMax) {
        pomperTout();
        if (cond()) {
          return true;
        }
        if (TEMPS_REEL) {
          await respirer();
        } else if (++n % 300 === 0) {
          await new Promise((r) => setImmediate(r));
        }
      }
      return cond();
    }
    const appel = (c, nom) => c.M.ccall(nom, 'number', [], []);
    dire('Cas complémentaires : menus, vitesse locale, ville quittée, échecs de chargement et d\'envoi.');
    res.pret = await attendre(() => enJeu() && !hote.moteur.enPause(), 60000);

    // 1. Menus Charger / Recommencer / Quitter : refusés en partie à deux, avec un avis à l'écran.
    {
      const av = { h: hote.lock.stats.menusRefuses, i: invite.lock.stats.menusRefuses };
      const refus = appel(hote, 'brutus_reseau_test_menu') === 1 && appel(invite, 'brutus_reseau_test_menu') === 1;
      const compte = await attendre(() => hote.lock.stats.menusRefuses > av.h && invite.lock.stats.menusRefuses > av.i, 5000);
      res.menus = { refus, compte, avis: !!(hote.lock.etat().avis && invite.lock.etat().avis) };
      dire(`  menus : ${JSON.stringify(res.menus)}`);
    }

    // 2. Vitesse changée hors du client (fenêtre d'options, invasion) : remise à la vitesse commune.
    {
      const v = hote.moteur.vitesse();
      const autre = v > 20 ? v - 10 : v + 10;
      const av = invite.lock.stats.vitessesCorrigees;
      invite.moteur.vitesseAppliquer(autre, invite.moteur.enPause());
      const change = invite.moteur.vitesse() === autre;
      const revenue = await attendre(() => invite.moteur.vitesse() === v && invite.lock.stats.vitessesCorrigees > av, 5000);
      res.vitesse = { change, revenue, commune: hote.moteur.vitesse() === invite.moteur.vitesse() };
      dire(`  vitesse locale : ${JSON.stringify(res.vitesse)}`);
    }

    // 3. L'invité quitte la ville : il ne joue plus, l'hôte attend, puis « revenir » resynchronise.
    {
      await attendre(enJeu, 30000);
      const ep = hote.s().epoque;
      appel(invite, 'brutus_reseau_test_quitter_ville');
      const hors = await attendre(() => invite.lock.etat().phase === 'hors-ville', 3000);
      const t1 = invite.tick();
      const attente = await attendre(() => hote.lock.etat().phase === 'attente', ATT);
      const fige = invite.tick() === t1;
      const demande = invite.lock.revenir();
      const retour = await attendre(() => hote.s().epoque !== ep && enJeu(), 60000);
      res.inviteQuitte = { hors, attente, fige, demande, retour };
      dire(`  invité hors de la ville : ${JSON.stringify(res.inviteQuitte)}`);
    }

    // 4. L'hôte quitte la ville : pas de resynchronisation tant qu'il est dehors ; retour par « revenir ».
    {
      await attendre(enJeu, 30000);
      const ep = hote.s().epoque;
      const ra = hote.lock.stats.resyncsHote;
      appel(hote, 'brutus_reseau_test_quitter_ville');
      const hors = await attendre(() => hote.lock.etat().phase === 'hors-ville', 3000);
      hote.lock.forcerResync();
      await attendre(() => false, 800);
      const sansResync = hote.lock.stats.resyncsHote === ra;
      const attente = await attendre(() => invite.lock.etat().phase === 'attente', ATT);
      const demande = hote.lock.revenir();
      const retour = await attendre(() => hote.s().epoque !== ep && enJeu(), 90000);
      res.hoteQuitte = { hors, sansResync, attente, demande, retour, abandons: hote.lock.stats.recuperationsAbandonnees };
      dire(`  hôte hors de la ville : ${JSON.stringify(res.hoteQuitte)}`);
    }

    // 5. Chargement refusé par le moteur de l'invité : la sauvegarde est redemandée une fois.
    {
      await attendre(enJeu, 30000);
      const rej = invite.lock.stats.sauvegardesRejetees;
      const chg = invite.lock.stats.chargements;
      const orig = invite.moteur.charger;
      let echecs = 0;
      invite.moteur.charger = (chemin) => {
        if (echecs === 0) {
          echecs++;
          return false;
        }
        return orig.call(invite.moteur, chemin);
      };
      hote.lock.forcerResync();
      const ok = await attendre(() => echecs === 1 && invite.lock.stats.sauvegardesRejetees > rej && invite.lock.stats.chargements > chg && enJeu(), 90000);
      invite.moteur.charger = orig;
      res.chargementRefuse = { echecs, ok, erreurs: !!(hote.s().erreur || invite.s().erreur) };
      dire(`  chargement refusé chez l'invité : ${JSON.stringify(res.chargementRefuse)}`);
    }

    // 6. Envoi refusé pendant une resynchronisation (socket qui tombe) : l'hôte ne recharge pas seul.
    {
      await attendre(enJeu, 30000);
      const conn = hote.s().conn;
      const orig = conn.envoyer;
      const er = hote.lock.stats.envoisRates;
      const ra = hote.lock.stats.resyncsHote;
      const chg = hote.lock.stats.chargements;
      conn.envoyer = (m) => (typeof m !== 'string' && m.byteLength > 1000 ? false : orig.call(conn, m));
      hote.lock.forcerResync();
      const vu = await attendre(() => hote.lock.stats.envoisRates > er, 5000);
      const seul = hote.lock.stats.resyncsHote === ra && hote.lock.stats.chargements === chg;
      conn.envoyer = orig;
      const fini = await attendre(() => hote.lock.stats.resyncsHote > ra && enJeu(), 90000);
      res.envoiRate = { vu, seul, fini };
      dire(`  envoi refusé : ${JSON.stringify(res.envoiRate)}`);
    }

    // 7. Coop (M7) : vue de l'autre, latence, chat, signal, et deux ordres incompatibles au même tour.
    {
      await attendre(enJeu, 30000);
      const caseLibre = (c, n) => {
        const g = c.M.ccall('brutus_coop_test_case_libre', 'number', ['number'], [n]);
        const dv = new DataView(c.M.HEAPU8.buffer, c.pointeurTampon(), 8);
        return { g, x: dv.getInt32(0, true), y: dv.getInt32(4, true) };
      };
      const presences = await attendre(() => hote.lock.stats.presencesRecues > 0 && invite.lock.stats.presencesRecues > 0, 5000);
      const latences = await attendre(() => hote.s().latences[1] !== undefined && invite.s().latences[0] !== undefined, 15000);
      const texte = 'Salut ! On pose le forum ici ? é✓';
      const chatEnvoye = hote.lock.envoyerChat(texte);
      const chat = await attendre(() => [hote, invite].every((c) => c.s().chat.some((l) => l.joueur === 0 && l.texte === texte)), 5000);
      const cs = caseLibre(invite, 0);
      const curseur = invite.moteur.coopCurseur;
      invite.moteur.coopCurseur = () => cs.g;
      const signalEnvoye = invite.lock.signaler();
      invite.moteur.coopCurseur = curseur;
      const signal = cs.g >= 0 && await attendre(() => [hote, invite].every((c) => c.s().chat.some((l) => l.joueur === 1 && l.signal === cs.g)), 5000);
      // Conflit : les deux joueurs posent un puits sur la même case, dans le MÊME tour K. L'hôte passe en
      // premier ; l'invité doit être prévenu (et l'hôte jamais). Pour viser K exactement (en temps réel,
      // deux joueurs ne sont jamais au même groupe au même instant), l'ordre est ajouté au TOUR du
      // groupe K au moment où le client le fabrique (comme s'il sortait de la boîte du moteur).
      const PUITS = 92;
      const viserTour = (c, K, octets) => {
        const orig = c.moteur.sortie;
        c.moteur.sortie = () => {
          const neuves = orig.call(c.moteur);
          if (c.s().envoye !== K) {
            return neuves;
          }
          c.moteur.sortie = orig;
          const t = new Uint8Array(neuves.length + octets.length);
          t.set(neuves, 0);
          t.set(octets, neuves.length);
          return t;
        };
        return () => { c.moteur.sortie = orig; };
      };
      hote.injecter({ nom: 'triche', params: [100] }); // de quoi payer les puits, même ville à sec
      await attendre(() => false, 300);
      const conflitsAvant = { h: hote.lock.stats.conflits, i: invite.lock.stats.conflits };
      let essais = 0;
      let conflit = false;
      for (let n = 1; n <= 15 && !conflit; n++) {
        await attendre(enJeu, 30000);
        const c = caseLibre(hote, n * 3);
        if (c.g < 0) {
          break;
        }
        essais++;
        const { octets } = hote.octetsCommande('construire', [PUITS, c.x, c.y, c.x, c.y, 0, 0, 0, 0]);
        const K = Math.max(hote.s().envoye, invite.s().envoye) + 2;
        const remettre = [viserTour(hote, K, octets), viserTour(invite, K, octets)];
        conflit = await attendre(() => invite.lock.stats.conflits > conflitsAvant.i, 3000);
        remettre.forEach((f) => f());
      }
      const hoteJamais = hote.lock.stats.conflits === conflitsAvant.h;
      const avis = /au même moment/.test(invite.lock.etat().avis);
      res.coop = { presences, latences: { hote: hote.s().latences[1], invite: invite.s().latences[0] }, chatEnvoye, chat,
        signalEnvoye, signal, conflit, essais, hoteJamais, avis };
      dire(`  coop : ${JSON.stringify(res.coop)}`);
    }

    // 8. Route de Rome coupée (M8, petits restes) : l'hôte entoure le point d'entrée de murs. Le moteur
    // avertit, puis retire des murs (« route obstruée ») ou détruit le dernier bâtiment posé (« route bloquée »),
    // et met le jeu en pause chez lui : en réseau, seule la pause partagée compte, la partie continue.
    {
      await attendre(enJeu, 30000);
      const t = (c, i) => new DataView(c.M.HEAPU8.buffer, c.pointeurTampon(), 40).getInt32(i * 4, true);
      const valide = hote.M.ccall('brutus_reseau_test_entree', 'number', [], []) === 1;
      const [ex, ey, sx, sy, xmin, ymin, xmax, ymax, population, tresor] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9].map((i) => t(hote, i));
      const R = 3;
      const separables = valide && Math.max(Math.abs(ex - sx), Math.abs(ey - sy)) > R + 1;
      const reellesAvant = mismatches.filter((m) => !m.attendue).length;
      const avant = [hote, invite].map((c) => c.M.ccall('brutus_reseau_test_route_rome', 'number', [], []));
      let cotes = 0;
      if (separables) {
        const borne = (v, a, b) => Math.max(a, Math.min(b, v));
        const x0 = borne(ex - R, xmin, xmax), x1 = borne(ex + R, xmin, xmax);
        const y0 = borne(ey - R, ymin, ymax), y1 = borne(ey + R, ymin, ymax);
        const lignes = [];
        if (ey - R >= ymin) lignes.push([x0, y0, x1, y0]);
        if (ey + R <= ymax) lignes.push([x0, y1, x1, y1]);
        if (ex - R >= xmin) lignes.push([x0, y0, x0, y1]);
        if (ex + R <= xmax) lignes.push([x1, y0, x1, y1]);
        cotes = lignes.length;
        // Commandes de l'hôte, par sa boîte de sortie (comme l'interface) : argent, défrichage, murs.
        const poser = async (nom, params) => {
          hote.injecter({ nom, params });
          await attendre(() => false, 300);
        };
        await poser('triche', [100]);
        await poser('triche', [100]);
        for (const [a, b, c, d] of lignes) {
          await poser('construire', [9, a, b, c, d, 0, 0, 0, 0]); // défrichage de la ligne (routes comprises)
        }
        for (const [a, b, c, d] of lignes) {
          await poser('construire', [6, a, b, c, d, 0, 0, 0, 0]); // mur sur la ligne
        }
      }
      const vu = separables && await attendre(() => [hote, invite].every((c) => c.M.ccall('brutus_reseau_test_route_rome', 'number', [], []) & 6), 60000);
      const apres = [hote, invite].map((c) => c.M.ccall('brutus_reseau_test_route_rome', 'number', [], []));
      const t1 = hote.tick();
      const continue_ = await attendre(() => hote.tick() > t1 + 500 && enJeu(), 60000);
      const pauseLocale = hote.moteur.enPause() || invite.moteur.enPause();
      res.routeRome = { valide, population, tresor, entree: [ex, ey], sortie: [sx, sy], separables, cotes, avant, apres, vu, memesMessages: apres[0] === apres[1],
        continue: continue_, pauseLocale, divergences: mismatches.filter((m) => !m.attendue).length - reellesAvant };
      dire(`  route de Rome coupée : ${JSON.stringify(res.routeRome)}`);
    }

    // 9. Forte latence : 150 à 350 ms par trajet (300 à 700 ms d'un joueur à l'autre) pendant 15 s.
    if (!REEL || FAUX_WS) {
      await attendre(enJeu, 30000);
      const reellesAvant = mismatches.filter((m) => !m.attendue).length;
      const t1 = hote.tick();
      relais.latence = { min: 150, max: 350 };
      let attenteVueLatence = false;
      let maxLat = 0;
      await attendre(() => {
        if (hote.lock.etat().phase === 'attente' || invite.lock.etat().phase === 'attente') attenteVueLatence = true;
        maxLat = Math.max(maxLat, hote.s().latences[1] || 0, invite.s().latences[0] || 0);
        return false;
      }, 15000);
      relais.latence = null;
      const ticks = hote.tick() - t1;
      const retour = await attendre(enJeu, 30000);
      res.latence = { ticks, latenceMaxMesuree: maxLat, attente: attenteVueLatence, retour, divergences: mismatches.filter((m) => !m.attendue).length - reellesAvant };
      dire(`  forte latence : ${JSON.stringify(res.latence)}`);
    }

    // 10. Arrivée pendant une resynchronisation : l'invité est coupé pendant qu'il reçoit la sauvegarde.
    if (!REEL || FAUX_WS) {
      await attendre(enJeu, 30000);
      const reellesAvant = mismatches.filter((m) => !m.attendue).length;
      const ep = hote.s().epoque;
      relais.latence = { min: 30, max: 60 }; // laisse le temps de couper au milieu du transfert
      hote.lock.forcerResync();
      const enCours = await attendre(() => invite.lock.etat().phase === 'resync' || Object.keys(invite.s().receptions || {}).length > 0, 5000);
      coupurePropre(invite, 800);
      relais.latence = null;
      const retour = await attendre(() => hote.s().epoque !== ep && enJeu(), 90000);
      const t1 = hote.tick();
      const suite = await attendre(() => hote.tick() > t1 + 300 && enJeu(), 30000);
      res.arriveeResync = { enCours, retour, suite, epoques: [ep, hote.s().epoque], divergences: mismatches.filter((m) => !m.attendue).length - reellesAvant };
      dire(`  arrivée pendant une resynchronisation : ${JSON.stringify(res.arriveeResync)}`);
    }

    // 11. Deux onglets avec le même jeton : le 2e onglet de l'invité (même stockage) reprend la session ;
    // le relais remplace la 1re connexion, qui s'arrête SANS se reconnecter (pas de ping-pong entre onglets).
    if (!REEL || FAUX_WS) {
      await attendre(enJeu, 30000);
      const reellesAvant = mismatches.filter((m) => !m.attendue).length;
      const remplAvant = relais.stats.remplacements;
      const ancien = invite;
      const reconnexionsAvant = ancien.lock.stats.reconnexions;
      const onglet2 = new Client('invité (2e onglet)', 1, [], ancien.stockage);
      await onglet2.demarrer();
      const repris = onglet2.lock.reprendre();
      premierInvite = ancien;
      invite = onglet2; // clients() pompe désormais le 2e onglet (et l'ancien à part)
      const arret = await attendre(() => { ancien.pomper(); return !!ancien.s().arret; }, 10000);
      const enJeu2 = await attendre(() => { ancien.pomper(); return enJeu(); }, 90000);
      await attendre(() => { ancien.pomper(); return false; }, 3000);
      const reconnexionsAncien = ancien.lock.stats.reconnexions - reconnexionsAvant;
      const t1 = hote.tick();
      const suite = await attendre(() => { ancien.pomper(); return hote.tick() > t1 + 300 && enJeu(); }, 30000);
      res.deuxOnglets = { repris, arret, erreur: ancien.s().erreur, remplacements: relais.stats.remplacements - remplAvant, enJeu: enJeu2, suite,
        ancienActif: !!ancien.s().actif, reconnexionsAncien, divergences: mismatches.filter((m) => !m.attendue).length - reellesAvant };
      dire(`  deux onglets, même jeton : ${JSON.stringify(res.deuxOnglets)}`);
    }
    return res;
  }

  // Comparaison finale directe : on avance jusqu'à ce que les deux soient au même tick et à la même époque.
  let direct = { comparees: false };
  for (let i = 0; i < 5000; i++) {
    pomperTout();
    await respirer();
    if (hote.tick() === invite.tick() && hote.s().epoque === invite.s().epoque && hote.s().actif && invite.s().actif) {
      const a = Buffer.from(hote.moteur.empreinte()).toString('hex');
      const b = Buffer.from(invite.moteur.empreinte()).toString('hex');
      direct = { comparees: true, tick: hote.tick(), a, b, identiques: a === b };
      break;
    }
  }
  return { hote, invite, evenements, faits, attenteVue, tick0, cible, direct, rechargeFaite, it, compl, premierInvite };
}

function M_pause(c) {
  c.M.ccall('brutus_reseau_test_pause', null, [], []);
}
function M_vitesse(c, delta) {
  c.M.ccall('brutus_reseau_test_vitesse', null, ['number'], [delta]);
}

// ============================================================ rapport

function rapport(r, erreur) {
  const { hote, invite, evenements, faits, attenteVue, tick0, direct, rechargeFaite, compl } = r || {};
  const duree = (performance.now() - debut) / 1000;
  const reelles = mismatches.filter((m) => !m.attendue);
  const attendues = mismatches.filter((m) => m.attendue);
  const divergencesHote = () => (panne ? (hote.ancien ? hote.ancien.divergences : 0) + hote.lock.stats.divergences - panne.divergencesAvant : 0);
  const complets = [...empreintesVues.values()].filter((e) => Object.keys(e).filter((k) => k !== 'signale').length >= 2).length;
  const minEmpreintes = Math.max(1, Math.min(100, Math.floor(TICKS / (EMPREINTE_GROUPES * 5) / 2)));
  const L = [];
  if (PROD) {
    L.push('MODE PRODUCTION : constantes de web/lockstep.js (ping 20 s, mort 45 s, attente 10 s, empreinte tous les 50 groupes), jeu à l\'horloge réelle.');
  }
  L.push(REEL ? 'RAPPORT : test réseau avec le vrai relais (deux moteurs wasm dans Node, WebSocket vers ' + RELAIS_URL + ')' : 'RAPPORT : test réseau sans port (deux moteurs wasm dans Node, relais en mémoire)');
  L.push(`Date : ${new Date().toISOString()}`);
  L.push(`Carte : ${CARTE}. Ticks demandés : ${TICKS}. Une empreinte tous les ${EMPREINTE_GROUPES * 5} ticks.`);
  L.push(`Durée réelle : ${duree.toFixed(0)} s.`);
  if (erreur) {
    L.push(`ERREUR : ${erreur.message || erreur}`);
  }
  if (r) {
    const sh = hote.lock.stats;
    const si = invite.lock.stats;
    L.push('');
    L.push('Progression');
    L.push(`  Tick de départ ${tick0}, tick final hôte ${hote.tick()} / invité ${invite.tick()} (${hote.tick() - tick0} ticks joués par l'hôte, soit ${((hote.tick() - tick0) * 0.008 / 60).toFixed(1)} min de jeu à 200 %).`);
    L.push(`  Groupes de 5 ticks joués : hôte ${sh.groupes} (instance actuelle), invité ${si.groupes}.`);
    L.push(`  Commandes du bot injectées : hôte ${hote.injectees}, invité ${invite.injectees}.`);
    L.push(`  Commandes appliquées / refusées par le moteur : hôte ${hote.M.ccall('brutus_reseau_commandes_appliquees', 'number', [], [])} / ${hote.M.ccall('brutus_reseau_commandes_refusees', 'number', [], [])}, invité ${invite.M.ccall('brutus_reseau_commandes_appliquees', 'number', [], [])} / ${invite.M.ccall('brutus_reseau_commandes_refusees', 'number', [], [])}.`);
    L.push('');
    L.push('Empreintes');
    L.push(`  Empreintes comparables vues par le test (même époque, même tick, deux joueurs) : ${complets}.`);
    L.push(`  Comparées par le client hôte : ${sh.empreintesComparees} (instance actuelle).`);
    L.push(`  Divergences détectées par le client hôte : ${sh.divergences}.`);
    L.push(`  Écarts relevés par le test : ${reelles.length} RÉEL(S), ${attendues.length} attendu(s) (panne injectée).`);
    for (const m of reelles.slice(0, 10)) {
      L.push(`    RÉEL : époque ${m.epoque}, tick ${m.tick} : ${JSON.stringify(m.valeurs)}`);
    }
    if (direct.comparees) {
      L.push(`  Comparaison finale directe au tick ${direct.tick} : ${direct.identiques ? 'identiques' : 'DIFFÉRENTES'} (${direct.a} / ${direct.b}).`);
    } else {
      L.push('  Comparaison finale directe : impossible (ticks différents).');
    }
    L.push('');
    L.push('Coupures et resynchronisations');
    L.push(`  Reconnexions : hôte ${sh.reconnexions} (instance actuelle), invité ${si.reconnexions}.`);
    L.push(`  Coupures silencieuses détectées par un client : hôte ${sh.coupuresSilencieuses}, invité ${si.coupuresSilencieuses}.`);
    if (!REEL) {
      L.push(`  « joueur-revenu » sans « joueur-parti » avant, vu par le relais : ${relais.stats.retoursSansDepart}. Remplacements de connexion : ${relais.stats.remplacements}.`);
    }
    L.push(`  Resynchronisations lancées par l'hôte (instance actuelle) : ${sh.resyncsHote}. Chargements : hôte ${sh.chargements}, invité ${si.chargements}.`);
    L.push(`  Sauvegardes rejetées : hôte ${sh.sauvegardesRejetees}, invité ${si.sauvegardesRejetees}.`);
    L.push(`  Messages « en attente de… » observés : ${JSON.stringify(attenteVue)}.`);
    if (rechargeFaite) {
      L.push(`  Rechargement de la page de l'hôte : ${rechargeFaite.fini ? 'récupéré en ' + (rechargeFaite.duree / 1000).toFixed(1) + ' s' : 'NON TERMINÉ'}.`);
    }
    L.push(`  Pause partagée : ${JSON.stringify(faits.pause)}. Vitesses : ${JSON.stringify(faits.vitesse)}.`);
    if (panne) {
      L.push(`  Panne injectée au tick ${panne.tick} : ${attendues.length ? 'détectée (écart au tick ' + attendues[0].tick + ')' : 'NON DÉTECTÉE'}, divergences détectées par l'hôte depuis : ${divergencesHote()}.`);
    }
    if (compl) {
      L.push('');
      L.push('Cas complémentaires');
      for (const [nom, v] of Object.entries(compl)) {
        L.push(`  ${nom} : ${JSON.stringify(v)}`);
      }
      L.push(`  Alerte de divergences à répétition : hôte « ${hote.s().alerte} », invité « ${invite.s().alerte} » ; menus refusés ${sh.menusRefuses}/${si.menusRefuses} ; vitesses corrigées ${sh.vitessesCorrigees}/${si.vitessesCorrigees} ; envois ratés ${sh.envoisRates}/${si.envoisRates}.`);
    }
    L.push('');
    L.push('Scénario');
    for (const e of evenements) {
      L.push(`  [${e.fait ? 'x' : ' '}] ${e.nom}${e.fait ? ' (tick ' + e.tick + ')' : ''}`);
    }
    L.push('');
    if (REEL) {
      L.push('Relais réel');
      L.push(`  ${RELAIS_URL} (wrangler dev) ; vitesse du jeu ${VITESSE} % ; statistiques détaillées dans le journal du relais (build-reseau/wrangler.log).`);
    } else {
      L.push('Relais en mémoire');
      L.push(`  Messages binaires ${relais.stats.binaires} (${(relais.stats.octets / 1e6).toFixed(1)} Mo), par type ${JSON.stringify(relais.stats.parType)}, textes ${relais.stats.textes}, jetés (coupures) ${relais.stats.jetes}.`);
      L.push(`  Erreurs envoyées : ${JSON.stringify(relais.stats.erreursEnvoyees)}.`);
    }
  }
  const criteres = {
    'pas d\'erreur': !erreur,
    'zéro divergence réelle': !!r && reelles.length === 0,
    'comparaison finale identique': !!(direct.comparees && direct.identiques),
    'tous les événements faits': evenements.every((e) => e.fait),
    'panne détectée par le test': attendues.length >= 1,
    'panne détectée une fois par l\'hôte': !!panne && divergencesHote() === 1,
    [`empreintes comparées (au moins ${minEmpreintes})`]: complets >= minEmpreintes,
    'aucune divergence avant la panne injectée': !!panne && panne.divergencesAvant === 0,
    'panne réparée dans une nouvelle époque': !!panne && !!r && hote.s().epoque !== panne.epoque,
    'menus refusés avec avis (les deux joueurs)': !!(compl && compl.menus && compl.menus.refus && compl.menus.compte && compl.menus.avis),
    'vitesse locale remise à la vitesse commune': !!(compl && compl.vitesse && compl.vitesse.change && compl.vitesse.revenue && compl.vitesse.commune),
    'invité hors de la ville : figé, hôte en attente, retour resynchronisé': !!(compl && compl.inviteQuitte && Object.values(compl.inviteQuitte).every(Boolean)),
    'hôte hors de la ville : pas de resynchronisation, retour sans abandon': !!(compl && compl.hoteQuitte && compl.hoteQuitte.hors && compl.hoteQuitte.sansResync && compl.hoteQuitte.demande && compl.hoteQuitte.retour && compl.hoteQuitte.abandons === 0),
    'chargement refusé chez l\'invité : sauvegarde redemandée, partie reprise': !!(compl && compl.chargementRefuse && compl.chargementRefuse.ok && !compl.chargementRefuse.erreurs),
    'envoi refusé : l\'hôte ne recharge pas seul, puis resynchronisation': !!(compl && compl.envoiRate && compl.envoiRate.vu && compl.envoiRate.seul && compl.envoiRate.fini),
    'coop : présence et latence reçues, chat et signal vus par les deux': !!(compl && compl.coop && compl.coop.presences && compl.coop.latences.hote !== undefined && compl.coop.latences.invite !== undefined && compl.coop.chat && compl.coop.signal),
    'coop : ordre en conflit signalé au second joueur seulement': !!(compl && compl.coop && compl.coop.conflit && compl.coop.hoteJamais && compl.coop.avis),
    'route de Rome coupée : message chez les deux, partie continue, sans pause locale': !!(compl && compl.routeRome && compl.routeRome.vu && compl.routeRome.memesMessages && compl.routeRome.continue && !compl.routeRome.pauseLocale && compl.routeRome.divergences === 0),
    'forte latence (300 à 700 ms) : la partie avance, sans attente ni divergence': !!(compl && compl.latence && compl.latence.ticks > 100 && !compl.latence.attente && compl.latence.retour && compl.latence.divergences === 0) || (REEL && !FAUX_WS),
    'arrivée pendant une resynchronisation : partie reprise, sans divergence': !!(compl && compl.arriveeResync && compl.arriveeResync.retour && compl.arriveeResync.suite && compl.arriveeResync.divergences === 0) || (REEL && !FAUX_WS),
    'deux onglets, même jeton : l\'ancien s\'arrête sans se reconnecter, le nouveau joue': !!(compl && compl.deuxOnglets && compl.deuxOnglets.repris && compl.deuxOnglets.arret && compl.deuxOnglets.remplacements >= 1 && compl.deuxOnglets.enJeu && compl.deuxOnglets.suite && !compl.deuxOnglets.ancienActif && compl.deuxOnglets.reconnexionsAncien === 0 && compl.deuxOnglets.divergences === 0) || (REEL && !FAUX_WS),
    'hôte rechargé': !rechargeFaite || !!rechargeFaite.fini,
    'vitesses identiques': !!(faits.vitesse && faits.vitesse.identiques),
    'pause vue puis reprise': !!(faits.pause && faits.pause.vue && faits.pause.reprise),
  };
  const ok = Object.values(criteres).every(Boolean);
  L.push('');
  L.push('Critères');
  for (const [nom, v] of Object.entries(criteres)) {
    L.push(`  [${v ? 'x' : ' '}] ${nom}`);
  }
  L.push('');
  L.push(ok ? 'RÉSULTAT : OK (aucune divergence réelle ; la panne injectée a été détectée une fois et réparée).' : 'RÉSULTAT : ÉCHEC');
  const texte = L.join('\n') + '\n';
  writeFileSync(RAPPORT, texte);
  console.log('\n' + texte);
  return ok;
}

let resultat = null;
let echec = null;
try {
  resultat = await principal();
} catch (e) {
  echec = e;
  console.error(e && e.stack ? e.stack : e);
}
const ok = rapport(resultat, echec);
process.exit(ok ? 0 : 1);
