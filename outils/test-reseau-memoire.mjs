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
const EMPREINTE_GROUPES = Number(opt('--empreinte-groupes', '10')); // une empreinte tous les 50 ticks

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
    c.montant.push(item);
  }

  descendre(c, item) {
    item.due = Math.max(c.dueDesc, this.it + entier(0, 2));
    c.dueDesc = item.due;
    c.descendant.push(item);
  }

  // Un tour de boucle : le relais traite ce que les clients lui ont envoyé, puis livre aux clients.
  tic(it) {
    this.it = it;
    for (const c of this.conns) {
      while (c.montant.length && c.montant[0].due <= it) {
        const item = c.montant.shift();
        if (c.silence) {
          this.stats.jetes++;
          continue;
        }
        this.traiter(c, item);
      }
    }
    for (const c of this.conns) {
      while (c.descendant.length && c.descendant[0].due <= it) {
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
const respirer = () => (REEL ? new Promise((r) => setTimeout(r, 2)) : Promise.resolve());

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
  return {
    libre: !REEL,
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
  const invite = new Client('invité', 1, journaux[1]);
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

  if (REEL) {
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
    outage = { nom: 'coupure silencieuse invité', fin: performance.now() + 6000 };
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
      if (pauseTest.etape === 0 && it - pauseTest.depuis > (REEL ? 300 : 60)) {
        pauseVue = hote.moteur.enPause() && invite.moteur.enPause();
        dire(`  pause : hôte ${hote.moteur.enPause()}, invité ${invite.moteur.enPause()} (ticks ${hote.tick()} / ${invite.tick()})`);
        M_pause(invite); // l'invité reprend
        pauseTest.etape = 1;
        pauseTest.depuis = it;
        pauseTest.tick = hote.tick();
      } else if (pauseTest.etape === 1 && it - pauseTest.depuis > (REEL ? 300 : 60)) {
        faits.pause = { vue: pauseVue, reprise: hote.tick() > pauseTest.tick && !hote.moteur.enPause() && !invite.moteur.enPause() };
        dire(`  reprise : ${faits.pause.reprise}`);
        pauseTest = null;
      }
    }
    if (attendreFin && attendreFin.verifier === 'vitesse' && it - attendreFin.it > (REEL ? 400 : 80)) {
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
    if (REEL) {
      await respirer();
    } else if (it % 300 === 0) {
      await new Promise((r) => setImmediate(r)); // laisse respirer la boucle d'événements
    }
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
  return { hote, invite, evenements, faits, attenteVue, tick0, cible, direct, rechargeFaite, it };
}

function M_pause(c) {
  c.M.ccall('brutus_reseau_test_pause', null, [], []);
}
function M_vitesse(c, delta) {
  c.M.ccall('brutus_reseau_test_vitesse', null, ['number'], [delta]);
}

// ============================================================ rapport

function rapport(r, erreur) {
  const { hote, invite, evenements, faits, attenteVue, tick0, direct, rechargeFaite } = r || {};
  const duree = (performance.now() - debut) / 1000;
  const reelles = mismatches.filter((m) => !m.attendue);
  const attendues = mismatches.filter((m) => m.attendue);
  const divergencesHote = () => (panne ? (hote.ancien ? hote.ancien.divergences : 0) + hote.lock.stats.divergences - panne.divergencesAvant : 0);
  const complets = [...empreintesVues.values()].filter((e) => Object.keys(e).filter((k) => k !== 'signale').length >= 2).length;
  const L = [];
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
    'empreintes comparées': complets > 0,
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
