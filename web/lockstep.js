// Brutus III (M6b) : client réseau lockstep (docs/protocole.md, « Ce qui est fait côté client »).
//
// Script classique, sans accès au DOM : il est inséré tel quel dans la page (web/shell.html.in, par
// moteur/CMakeLists.txt) et chargé sous Node par les tests (outils/test-reseau-memoire.mjs).
// Contraintes : la page passe par configure_file, donc aucun dollar suivi d'accolade, aucune
// accolade triple, aucun marqueur entre arobases, ni fermeture de balise script dans ce fichier.
//
// Tout ce qui touche au monde extérieur est injecté : horloge, transport (WebSocket), stockage
// (sessionStorage), moteur (wasm). Le client ne décide que du lockstep :
//  - un TOUR par groupe de 5 ticks (voir docs/protocole.md pour la raison), délai d'entrée de 2 groupes ;
//  - un groupe n'est joué que quand les TOUR de tous les joueurs attendus sont arrivés ;
//  - vitesse et pause partagées (message VITESSE avec tick d'effet) ;
//  - sauvegarde envoyée par l'hôte à l'arrivée ou au retour d'un joueur, rechargée par tous ;
//  - empreinte tous les 250 ticks, resynchronisation de tous si elles diffèrent ;
//  - reconnexion par jeton, ping et détection des coupures silencieuses ;
//  - Coop (M7), hors lockstep : chat, latence entre joueurs (PING), vue et curseur des autres
//    (PRESENCE), signaux sur la carte (SIGNAL). Rien de cela n'entre dans la simulation.
(function (racine) {
  "use strict";

  var VERSION_PROTOCOLE = 1;
  var TYPE = { TOUR: 1, VITESSE: 2, EMPREINTE: 3, SAUVEGARDE_DEMANDE: 4, SAUVEGARDE: 5, CHAT: 16, PING: 17, PRESENCE: 18, SIGNAL: 19 };
  var DEST_TOUS = 255;
  var DEST_AUTRES = 254;
  var ENTETE = 8;
  var TAILLE_MAX_MESSAGE = 65536;
  var ENTETE_MORCEAU = 12;
  var MORCEAU = 60000;
  var SAUVEGARDE_MAX = 8 * 1024 * 1024;
  var JOUEURS_MAX = 4;
  // Préfixe de la SAUVEGARDE (ajouté par ce client devant les octets de la sauvegarde du moteur) :
  // « BR3S », version, époque, nombre de joueurs attendus, leurs numéros, vitesse (u16), pause (u8).
  var MAGIE = [0x42, 0x52, 0x33, 0x53];
  var VERSION_PREFIXE = 1;
  var VITESSES = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 500];
  var MS_PAR_TICK = [702, 502, 352, 242, 162, 112, 82, 57, 37, 22, 16];
  var MS_PAR_TICK_RAPIDE = [702, 16, 8, 5, 3, 2];

  var DEFAUTS = {
    groupe: 5, // ticks par TOUR
    delai: 2, // groupes de délai d'entrée (80 à 160 ms de jeu à vitesse normale)
    empreinteGroupes: 50, // une empreinte tous les 250 ticks
    pingMs: 20000,
    morteMs: 45000,
    attenteMs: 10000,
    essaisMs: [1000, 2000, 4000, 8000],
    rattrapageGroupes: 40,
    maxGroupesParPompe: 200,
    demandeSauvegardeMs: 5000,
    reessaiResyncMs: 1000,
    divergencesMax: 3, // divergences réparées par resynchronisation tolérées dans la fenêtre ci-dessous
    fenetreDivergencesMs: 600000,
    recuperationMs: 60000, // hôte qui attend sa partie auprès d'un invité : au-delà, il abandonne
    pomperSurMessage: false, // page : traiter aussi à chaque message reçu (les minuteries d'un onglet masqué sont ralenties)
    libre: false, // tests : pas de cadencement, les groupes se jouent dès qu'ils sont complets
    chemin: "/tmp/brutus-reseau.svx",
    joueurs: 2,
    // M7 (Coop)
    presenceMs: 150, // au plus une présence par 150 ms quand elle change
    presenceMaxMs: 2000, // et au moins une toutes les 2 s
    latenceMs: 5000, // mesure de latence entre joueurs
    chatMax: 50, // lignes de chat gardées
    chatOctets: 400, // longueur maximale d'un message
  };

  // ---------------------------------------------------------------- messages binaires

  function encoder(type, tick, donnees, dest, epoque) {
    var d = donnees || new Uint8Array(0);
    var m = new Uint8Array(ENTETE + d.length);
    m[0] = type;
    m[1] = 0; // écrit par le relais
    m[2] = dest === undefined ? DEST_TOUS : dest;
    m[3] = epoque || 0;
    new DataView(m.buffer).setUint32(4, tick >>> 0, true);
    m.set(d, ENTETE);
    return m;
  }

  function decoder(octets) {
    var m = octets;
    var v = new DataView(m.buffer, m.byteOffset, m.byteLength);
    return { type: m[0], joueur: m[1], dest: m[2], epoque: m[3], tick: v.getUint32(4, true), donnees: m.subarray(ENTETE) };
  }

  function nouvelIdentifiant() {
    var v = new Uint32Array(1);
    do {
      if (racine.crypto && racine.crypto.getRandomValues) {
        racine.crypto.getRandomValues(v);
      } else {
        v[0] = Math.floor(Math.random() * 4294967296);
      }
    } while (v[0] === 0);
    return v[0];
  }

  function morceauxSauvegarde(charge, tick, epoque, dest) {
    if (charge.length > SAUVEGARDE_MAX) {
      throw new Error("sauvegarde trop grosse");
    }
    var id = nouvelIdentifiant();
    var messages = [];
    for (var pos = 0; pos === 0 || pos < charge.length; pos += MORCEAU) {
      var part = charge.subarray(pos, pos + MORCEAU);
      var d = new Uint8Array(ENTETE_MORCEAU + part.length);
      var v = new DataView(d.buffer);
      v.setUint32(0, id, true);
      v.setUint32(4, charge.length, true);
      v.setUint32(8, pos, true);
      d.set(part, ENTETE_MORCEAU);
      messages.push(encoder(TYPE.SAUVEGARDE, tick, d, dest, epoque));
    }
    return messages;
  }

  // Recolle les morceaux d'une sauvegarde (mêmes règles que Reception dans serveur-multi/client/client.mjs).
  function Reception() {
    this.erreur = null;
    this.raz();
  }
  Reception.prototype.raz = function () {
    this.id = null;
    this.tampon = null;
    this.total = 0;
    this.recus = 0;
  };
  Reception.prototype.rejeter = function (raison) {
    this.raz();
    this.erreur = raison;
    return null;
  };
  Reception.prototype.ajouter = function (donnees) {
    this.erreur = null;
    if (donnees.length < ENTETE_MORCEAU) {
      return this.rejeter("morceau trop court");
    }
    var v = new DataView(donnees.buffer, donnees.byteOffset, donnees.byteLength);
    var id = v.getUint32(0, true);
    var total = v.getUint32(4, true);
    var pos = v.getUint32(8, true);
    var part = donnees.subarray(ENTETE_MORCEAU);
    if (pos === 0) {
      if (id === 0) {
        return this.rejeter("identifiant nul");
      }
      if (total > SAUVEGARDE_MAX) {
        return this.rejeter("sauvegarde trop grosse");
      }
      this.raz();
      this.id = id;
      this.total = total;
      this.tampon = new Uint8Array(total);
    } else if (id !== this.id) {
      this.erreur = "début du transfert manquant";
      return null;
    } else if (total !== this.total || pos !== this.recus) {
      return this.rejeter("morceau hors séquence (doublon ou trou)");
    }
    if (part.length === 0 && total !== 0) {
      return this.rejeter("morceau vide");
    }
    if (pos + part.length > total) {
      return this.rejeter("morceau hors bornes");
    }
    this.tampon.set(part, pos);
    this.recus += part.length;
    if (this.recus < total) {
      return null;
    }
    var complete = this.tampon;
    this.raz();
    return complete;
  };

  function fabriquerCharge(epoque, attendus, vitesse, pause, sauvegarde) {
    var n = attendus.length;
    var charge = new Uint8Array(4 + 1 + 1 + 1 + n + 2 + 1 + sauvegarde.length);
    var p = 0;
    for (var i = 0; i < 4; i++) {
      charge[p++] = MAGIE[i];
    }
    charge[p++] = VERSION_PREFIXE;
    charge[p++] = epoque;
    charge[p++] = n;
    for (var j = 0; j < n; j++) {
      charge[p++] = attendus[j];
    }
    charge[p++] = vitesse & 255;
    charge[p++] = (vitesse >> 8) & 255;
    charge[p++] = pause ? 1 : 0;
    charge.set(sauvegarde, p);
    return charge;
  }

  function lireCharge(charge) {
    if (charge.length < 11) {
      return null;
    }
    for (var i = 0; i < 4; i++) {
      if (charge[i] !== MAGIE[i]) {
        return null;
      }
    }
    if (charge[4] !== VERSION_PREFIXE) {
      return null;
    }
    var epoque = charge[5];
    var n = charge[6];
    if (n < 1 || n > JOUEURS_MAX || 7 + n + 3 > charge.length) {
      return null;
    }
    var attendus = [];
    for (var j = 0; j < n; j++) {
      if (charge[7 + j] >= JOUEURS_MAX) {
        return null;
      }
      attendus.push(charge[7 + j]);
    }
    var p = 7 + n;
    return {
      epoque: epoque,
      attendus: attendus,
      vitesse: charge[p] | (charge[p + 1] << 8),
      pause: charge[p + 2] === 1,
      sauvegarde: charge.subarray(p + 3),
    };
  }

  function hex(octets) {
    var s = "";
    for (var i = 0; i < octets.length; i++) {
      s += (octets[i] < 16 ? "0" : "") + octets[i].toString(16);
    }
    return s;
  }

  function msParTick(pct) {
    if (pct < 10) {
      return 702;
    }
    if (pct <= 100) {
      return MS_PAR_TICK[Math.floor(pct / 10)];
    }
    return MS_PAR_TICK_RAPIDE[Math.floor(Math.min(pct, 500) / 100)];
  }

  function pasVitesse(pct, delta) {
    var i = 0;
    while (i < VITESSES.length - 1 && VITESSES[i] < pct) {
      i++;
    }
    i += delta;
    if (i < 0) {
      i = 0;
    }
    if (i > VITESSES.length - 1) {
      i = VITESSES.length - 1;
    }
    return VITESSES[i];
  }

  function nomJoueur(j) {
    return "Joueur " + (j + 1);
  }

  // ---------------------------------------------------------------- adaptateurs

  // Transport WebSocket du navigateur (et de Node 22+). L'écouteur reçoit ouvert(), message(texte ou
  // Uint8Array), ferme(code), erreur().
  function transportWebSocket() {
    return {
      ouvrir: function (url, ecouteur) {
        var ws = new racine.WebSocket(url);
        ws.binaryType = "arraybuffer";
        ws.onopen = function () {
          ecouteur.ouvert();
        };
        ws.onmessage = function (e) {
          ecouteur.message(typeof e.data === "string" ? e.data : new Uint8Array(e.data));
        };
        ws.onclose = function (e) {
          ecouteur.ferme(e.code);
        };
        ws.onerror = function () {
          ecouteur.erreur();
        };
        return {
          envoyer: function (m) {
            if (ws.readyState !== 1) {
              return false;
            }
            ws.send(m);
            return true;
          },
          fermer: function () {
            ws.onopen = ws.onmessage = ws.onclose = ws.onerror = null;
            try {
              ws.close(1000, "fin");
            } catch (e) {
              /* déjà fermée */
            }
          },
        };
      },
    };
  }

  // Moteur wasm (Module d'Emscripten). L'échange passe par le tampon statique du moteur
  // (brutus_reseau_tampon) ; HEAPU8 est relu à chaque accès (la mémoire peut avoir grandi).
  function moteurEmscripten(M) {
    var pointeur = null;
    function appel(nom, types, args) {
      return M.ccall(nom, "number", types, args);
    }
    function tampon() {
      if (pointeur === null) {
        pointeur = appel("brutus_reseau_tampon", [], []);
      }
      return pointeur;
    }
    function ecrireChaine(texte) {
      var oct = new TextEncoder().encode(texte);
      M.HEAPU8.set(oct, tampon());
      M.HEAPU8[tampon() + oct.length] = 0;
    }
    function hexa32(v) {
      var o = new Uint8Array(4);
      new DataView(o.buffer).setUint32(0, v >>> 0, true);
      return o;
    }
    return {
      activer: function (j) {
        return appel("brutus_reseau_activer", ["number"], [j]) === 1;
      },
      desactiver: function () {
        appel("brutus_reseau_desactiver", [], []);
      },
      sortie: function () {
        var n = appel("brutus_reseau_sortie", [], []);
        return n > 0 ? M.HEAPU8.slice(tampon(), tampon() + n) : new Uint8Array(0);
      },
      entree: function (joueur, tick, octets) {
        if (octets.length > TAILLE_MAX_MESSAGE) {
          return false;
        }
        M.HEAPU8.set(octets, tampon());
        return appel("brutus_reseau_entree", ["number", "number", "number"], [joueur, tick >>> 0, octets.length]) === 1;
      },
      avancer: function (n) {
        return appel("brutus_reseau_avancer", ["number"], [n]);
      },
      tick: function () {
        return appel("brutus_reseau_tick", [], []) >>> 0;
      },
      // 8 octets, petit-boutiste : empreinte FNV-1a 64 bits de l'état de la partie.
      empreinte: function () {
        var bas = appel("brutus_reseau_empreinte", [], []) >>> 0;
        var haut = appel("brutus_reseau_empreinte_haute", [], []) >>> 0;
        var o = new Uint8Array(8);
        o.set(hexa32(bas), 0);
        o.set(hexa32(haut), 4);
        return o;
      },
      sauvegarder: function (chemin) {
        ecrireChaine(chemin);
        return appel("brutus_reseau_sauvegarder", ["number"], [tampon()]) === 1;
      },
      charger: function (chemin) {
        ecrireChaine(chemin);
        return appel("brutus_reseau_charger", ["number"], [tampon()]) === 1;
      },
      lireFichier: function (chemin) {
        return M.FS.readFile(chemin);
      },
      ecrireFichier: function (chemin, octets) {
        M.FS.writeFile(chemin, octets);
      },
      jeuEnCours: function () {
        return appel("brutus_reseau_jeu_en_cours", [], []) === 1;
      },
      // Détail par morceau de la dernière empreinte (texte), pour diagnostiquer une divergence.
      detailEmpreinte: function () {
        try {
          var f = M.FS.readFile("/tmp/brutus-empreinte-detail.txt");
          var t = "";
          for (var i = 0; i < f.length; i++) {
            t += String.fromCharCode(f[i]);
          }
          return t;
        } catch (e) {
          return "";
        }
      },
      // Brutus III (M6b) : vrai si le joueur a quitté la ville (menu principal, choix de mission).
      villeQuittee: function () {
        return appel("brutus_reseau_ville_est_quittee", [], []) === 1;
      },
      // Nombre de menus refusés (Charger, Recommencer, Quitter) depuis le dernier appel.
      refusMenu: function () {
        return appel("brutus_reseau_refus_menu", [], []);
      },
      demandeVitesse: function () {
        return appel("brutus_reseau_demande_vitesse", [], []);
      },
      demandePause: function () {
        return appel("brutus_reseau_demande_pause", [], []);
      },
      vitesseAppliquer: function (pct, pause) {
        appel("brutus_reseau_vitesse_appliquer", ["number", "number"], [pct, pause ? 1 : 0]);
      },
      vitesse: function () {
        return appel("brutus_reseau_vitesse", [], []);
      },
      enPause: function () {
        return appel("brutus_reseau_en_pause", [], []) === 1;
      },
      // Coop (M7) : données d'écran seulement (multijoueur/coop.h).
      coopPresence: function () {
        var n = appel("brutus_coop_ma_presence", [], []);
        return n > 0 ? M.HEAPU8.slice(tampon(), tampon() + n) : null;
      },
      coopPresenceAutre: function (joueur, octets) {
        M.HEAPU8.set(octets.subarray(0, Math.min(octets.length, 64)), tampon());
        appel("brutus_coop_presence", ["number", "number"], [joueur, Math.min(octets.length, 64)]);
      },
      coopOublier: function (joueur) {
        appel("brutus_coop_oublier", ["number"], [joueur]);
      },
      coopCurseur: function () {
        return appel("brutus_coop_curseur", [], []);
      },
      coopSignal: function (joueur, case_) {
        return appel("brutus_coop_signal", ["number", "number"], [joueur, case_]) === 1;
      },
      coopAller: function (case_) {
        appel("brutus_coop_aller", ["number"], [case_]);
      },
      coopConflits: function () {
        return appel("brutus_coop_conflits", [], []);
      },
    };
  }

  // Stockage de session (sessionStorage) : une seule clé JSON.
  function stockageSession(cle) {
    return {
      lire: function () {
        try {
          var t = racine.sessionStorage.getItem(cle);
          return t ? JSON.parse(t) : null;
        } catch (e) {
          return null;
        }
      },
      ecrire: function (valeur) {
        try {
          racine.sessionStorage.setItem(cle, JSON.stringify(valeur));
        } catch (e) {
          /* stockage refusé : la reconnexion après rechargement sera impossible */
        }
      },
      effacer: function () {
        try {
          racine.sessionStorage.removeItem(cle);
        } catch (e) {
          /* rien */
        }
      },
    };
  }

  // ---------------------------------------------------------------- le client

  function creerLockstep(options) {
    var o = {};
    var k;
    for (k in DEFAUTS) {
      o[k] = DEFAUTS[k];
    }
    for (k in options) {
      if (options[k] !== undefined) {
        o[k] = options[k];
      }
    }
    var moteur = o.moteur;
    var transport = o.transport;
    var horloge = o.horloge || { maintenant: function () { return Date.now(); } };
    var stockage = o.stockage || { lire: function () { return null; }, ecrire: function () {}, effacer: function () {} };
    var journal = o.journal || function () {};
    var G = o.groupe;
    var D = o.delai;

    var stats = {
      groupes: 0,
      empreintesEnvoyees: 0,
      empreintesComparees: 0,
      divergences: 0,
      resyncsHote: 0,
      chargements: 0,
      reconnexions: 0,
      coupuresSilencieuses: 0,
      fermetures: 0,
      toursIgnores: 0,
      sauvegardesRejetees: 0,
      menusRefuses: 0,
      vitessesCorrigees: 0,
      envoisRates: 0,
      recuperationsAbandonnees: 0,
      presencesEnvoyees: 0,
      presencesRecues: 0,
      chatsRecus: 0,
      signauxRecus: 0,
      conflits: 0,
    };

    var s = {
      base: o.base || "",
      session: false, // code et jeton connus : on peut revenir à sa place
      code: null,
      jeton: null,
      joueur: -1,
      joueursMax: o.joueurs,
      salon: [], // dernier état des places : [{joueur, connecte}]
      conn: null,
      gen: 0,
      connecte: false, // bienvenue reçue sur la connexion courante
      dernierRecu: 0,
      dernierPing: 0,
      essai: 0,
      prochainEssai: 0,
      arret: false,
      erreur: null,
      rechargee: false, // page rechargée : l'état du jeu en mémoire est perdu
      actif: false, // lockstep en cours (partie à plusieurs chargée)
      epoque: 0,
      attendus: [],
      T0: 0,
      prochain: 0, // prochain groupe à démarrer
      restants: 0, // ticks du groupe démarré pas encore joués (0 : à une frontière de groupe)
      envoye: 0,
      tours: {},
      vitesses: [],
      projete: { pct: 100, pause: false },
      echeance: 0,
      dernierProgres: 0,
      empreintes: {},
      monEmpreinte: {},
      receptions: {},
      resyncEnAttente: null,
      prochainResync: 0,
      attendSauvegardeInvite: false,
      derniereDemande: 0,
      resyncJusqua: 0,
      derniereNotif: "",
      vitesseCommune: null, // vitesse et pause partagées effectivement appliquées
      rangVitesse: 0, // compteur croissant : ordre d'envoi des VITESSE d'un même joueur
      divergencesRecentes: [], // dates des divergences réparées (fenêtre glissante)
      alerte: "", // divergences à répétition : la partie continue, mais le joueur est prévenu
      details: {}, // tick -> détail par morceau de notre empreinte (diagnostic)
      detailDivergence: "", // dernier détail journalisé lors d'une divergence (tests)
      debutRecuperation: 0,
      rechargeEssayee: false, // une sauvegarde refusée par le moteur est redemandée une fois
      avis: "",
      avisJusqua: 0,
      // Coop (M7)
      chat: [], // [{id, joueur, texte, signal}] (signal : case de la carte, ou -1)
      chatId: 0,
      latences: {}, // joueur -> ms (aller-retour)
      pingId: 0,
      dernierPingJoueurs: 0,
      dernierePresence: 0,
      presenceEnvoyee: "",
      signalesNonLus: 0,
      info: "", // M12 : mode, carte et options de la partie (donnés par l'hôte, transmis par le relais)
    };

    function villeQuittee() {
      return !!(moteur.villeQuittee && moteur.villeQuittee());
    }

    // ------------------------------------------------------------ état visible par la page

    function placeConnectee(j) {
      for (var i = 0; i < s.salon.length; i++) {
        if (s.salon[i].joueur === j) {
          return s.salon[i].connecte;
        }
      }
      return false;
    }

    function manquants() {
      // Joueurs attendus dont le TOUR du groupe en cours n'est pas arrivé.
      var m = [];
      if (!s.actif || s.prochain < D) {
        return m;
      }
      for (var i = 0; i < s.attendus.length; i++) {
        var j = s.attendus[i];
        if (!s.tours[s.prochain + ":" + j]) {
          m.push(j);
        }
      }
      return m;
    }

    function attente(t) {
      if (!s.actif || moteur.enPause()) {
        return [];
      }
      var manque = manquants();
      var noms = [];
      var bloque = t - s.dernierProgres > o.attenteMs;
      for (var i = 0; i < manque.length; i++) {
        var j = manque[i];
        if (j !== s.joueur && (!placeConnectee(j) || bloque)) {
          noms.push(j);
        }
      }
      return noms;
    }

    function etat() {
      var t = horloge.maintenant();
      var att = attente(t);
      var texte = "";
      var phase;
      if (s.erreur) {
        phase = "erreur";
        texte = s.erreur;
      } else if (!s.session && !s.conn) {
        phase = "inactif";
      } else if (!s.session) {
        phase = "connexion";
        texte = "Connexion au relais…";
      } else if (!s.connecte) {
        phase = "deconnecte";
        texte = "Connexion perdue : reconnexion…";
      } else if (t < s.resyncJusqua) {
        phase = "resync";
        texte = "resynchronisation…";
      } else if (s.attendSauvegardeInvite) {
        phase = "recuperation";
        texte = "Récupération de la partie auprès d'un autre joueur…";
      } else if (!s.actif) {
        phase = "salon";
        texte = s.joueur === 0 ? "En attente d'un autre joueur. Code de la partie : " + s.code : "Connecté. En attente de la partie de l'hôte…";
      } else if (villeQuittee()) {
        phase = "hors-ville";
        texte = "Vous avez quitté la ville : la partie multijoueur est en pause pour vous deux. Revenez dans la partie ou quittez-la.";
      } else if (att.length) {
        phase = "attente";
        var n = [];
        for (var i = 0; i < att.length; i++) {
          n.push(nomJoueur(att[i]));
        }
        texte = "en attente de " + n.join(", ") + "…";
      } else {
        phase = "jeu";
      }
      return {
        phase: phase,
        texte: texte,
        code: s.code,
        joueur: s.joueur,
        hote: s.joueur === 0,
        salon: s.salon,
        attente: att,
        actif: s.actif,
        connecte: s.connecte,
        pause: s.actif ? moteur.enPause() : false,
        tick: s.actif ? moteur.tick() : 0,
        epoque: s.epoque,
        alerte: s.alerte,
        avis: t < s.avisJusqua ? s.avis : "",
        chat: s.chat.slice(),
        latences: s.latences,
        info: s.info,
      };
    }

    function notifier() {
      if (!o.surChangement) {
        return;
      }
      var e = etat();
      var cle = JSON.stringify([e.phase, e.texte, e.code, e.joueur, e.salon, e.pause, e.alerte, e.avis, s.chatId, e.latences, e.info]);
      if (cle !== s.derniereNotif) {
        s.derniereNotif = cle;
        o.surChangement(e);
      }
    }

    // ------------------------------------------------------------ connexion

    function envoyerTexte(texte) {
      return s.conn ? s.conn.envoyer(texte) : false;
    }

    function envoyerBin(type, tick, donnees, dest) {
      if (!s.conn || !s.connecte) {
        return false;
      }
      return s.conn.envoyer(encoder(type, tick, donnees, dest, s.epoque));
    }

    // Envoie une suite de messages ; faux si l'un est refusé (les suivants ne partent pas).
    function envoyerTous(msgs) {
      if (!s.conn || !s.connecte) {
        return false;
      }
      for (var i = 0; i < msgs.length; i++) {
        if (!s.conn.envoyer(msgs[i])) {
          return false;
        }
      }
      return true;
    }

    function fermerConnexion() {
      s.gen++; // les événements de l'ancienne connexion seront ignorés
      var c = s.conn;
      s.conn = null;
      s.connecte = false;
      if (c) {
        c.fermer();
      }
    }

    function ouvrir(chemin) {
      fermerConnexion();
      var g = s.gen;
      s.arret = false;
      var url = s.base + chemin;
      journal("connexion : " + chemin.split("?")[0]);
      var t = horloge.maintenant();
      s.dernierRecu = t;
      s.dernierPing = t;
      s.conn = transport.ouvrir(url, {
        ouvert: function () {
          if (g !== s.gen) {
            return;
          }
          s.dernierRecu = horloge.maintenant();
        },
        message: function (m) {
          if (g !== s.gen) {
            return;
          }
          s.dernierRecu = horloge.maintenant();
          if (typeof m === "string") {
            traiterTexte(m);
          } else {
            traiterBinaire(m);
          }
          notifier();
          if (o.pomperSurMessage) {
            pomper(); // onglet masqué : le navigateur ralentit les minuteries, mais livre les messages
          }
        },
        ferme: function (code) {
          if (g !== s.gen) {
            return;
          }
          surFermeture(code);
        },
        erreur: function () {
          if (g !== s.gen) {
            return;
          }
          surFermeture(1006);
        },
      });
    }

    function surFermeture(code) {
      s.gen++; // onerror puis onclose arrivent pour la même coupure : la seconde est ignorée (un seul essai compté)
      stats.fermetures++;
      var etaitConnecte = s.connecte;
      s.conn = null;
      s.connecte = false;
      journal("connexion fermée (" + code + ")");
      if (!s.session) {
        s.erreur = "Connexion au relais impossible.";
        return;
      }
      if (s.arret) {
        return;
      }
      programmerReconnexion(etaitConnecte);
      notifier();
    }

    function programmerReconnexion(immediat) {
      var t = horloge.maintenant();
      if (immediat) {
        s.essai = 0;
      }
      s.prochainEssai = t + (immediat ? 0 : o.essaisMs[Math.min(s.essai, o.essaisMs.length - 1)]);
      s.essai++;
    }

    function enregistrerSession() {
      stockage.ecrire({ code: s.code, jeton: s.jeton, joueur: s.joueur });
    }

    function reconnecter(compter) {
      if (compter) {
        stats.reconnexions++;
      }
      ouvrir("/rejoindre/" + encodeURIComponent(s.code) + "?version=" + VERSION_PROTOCOLE + "&jeton=" + encodeURIComponent(s.jeton));
    }

    // info (M12) : ligne d'information de la partie (mode, carte, options), que le relais donne à tous.
    function creer(joueurs, info) {
      s.rechargee = false;
      s.erreur = null;
      s.info = "";
      ouvrir("/creer?version=" + VERSION_PROTOCOLE + "&joueurs=" + (joueurs || o.joueurs) +
        (info ? "&info=" + encodeURIComponent(String(info).slice(0, 200)) : ""));
    }

    function rejoindre(code) {
      s.rechargee = false;
      s.erreur = null;
      ouvrir("/rejoindre/" + encodeURIComponent(String(code).trim().toUpperCase()) + "?version=" + VERSION_PROTOCOLE);
    }

    // Reprend la session gardée dans le stockage (après rechargement de la page).
    function reprendre() {
      var g = stockage.lire();
      if (!g || !g.code || !g.jeton || typeof g.joueur !== "number") {
        return false;
      }
      s.code = g.code;
      s.jeton = g.jeton;
      s.joueur = g.joueur;
      s.session = true;
      s.rechargee = true;
      s.erreur = null;
      s.essai = 0;
      reconnecter(false); // la reprise après rechargement n'est pas une reconnexion après coupure
      return true;
    }

    function oublier() {
      stockage.effacer();
      s.session = false;
      s.code = null;
      s.jeton = null;
      s.joueur = -1;
      s.salon = [];
      s.info = "";
    }

    // ------------------------------------------------------------ messages de session (JSON)

    function traiterTexte(texte) {
      if (texte === "pong") {
        return;
      }
      var m;
      try {
        m = JSON.parse(texte);
      } catch (e) {
        journal("message texte illisible");
        return;
      }
      switch (m.type) {
        case "bienvenue":
          surBienvenue(m);
          break;
        case "joueur-arrive":
          s.salon = m.joueurs || s.salon;
          if (s.joueur === 0) {
            demanderResync("arrivée du joueur " + (m.joueur + 1));
          }
          break;
        case "joueur-revenu":
          // Peut arriver sans joueur-parti avant (connexion remplacée) : même traitement.
          s.salon = m.joueurs || s.salon;
          if (s.joueur === 0) {
            demanderResync("retour du joueur " + (m.joueur + 1));
          }
          break;
        case "joueur-parti":
          s.salon = m.joueurs || s.salon;
          journal("joueur " + (m.joueur + 1) + " parti");
          oublierJoueur(m.joueur);
          break;
        case "joueur-expulse":
          s.salon = m.joueurs || s.salon;
          oublierJoueur(m.joueur);
          if (s.joueur === 0) {
            demanderResync("expulsion du joueur " + (m.joueur + 1));
          }
          break;
        case "erreur":
          surErreur(m);
          break;
        default:
          break;
      }
    }

    function surBienvenue(m) {
      s.code = m.code;
      s.joueur = m.joueur;
      s.jeton = m.jeton;
      s.joueursMax = m.joueursMax;
      s.info = typeof m.info === "string" ? m.info : "";
      s.salon = m.joueurs || [];
      s.session = true;
      s.connecte = true;
      s.essai = 0;
      s.erreur = null;
      enregistrerSession();
      journal("bienvenue : joueur " + (m.joueur + 1) + ", salon " + m.code + (m.reconnexion ? " (retour)" : ""));
      if (!m.reconnexion || m.joueur !== 0) {
        return;
      }
      // Retour de l'hôte.
      if (s.actif) {
        // Coupure réseau seule : l'état est en mémoire, mais les tours des autres manquent. Tous rechargent.
        demanderResync("retour de l'hôte");
      } else if (s.rechargee) {
        // Page rechargée : l'état est perdu, un invité le renvoie.
        s.attendSauvegardeInvite = true;
        s.derniereDemande = 0;
        s.debutRecuperation = horloge.maintenant();
      }
    }

    function surErreur(m) {
      var code = m.code;
      journal("erreur du relais : " + code);
      // On agit tout de suite, sans attendre la fermeture (docs/protocole.md, point 7).
      if (code === "remplace" || code === "expulse") {
        s.arret = true;
        s.erreur = m.message || code;
        if (code === "expulse") {
          oublier();
        }
        arreterJeu();
        fermerConnexion();
      } else if (code === "message-trop-gros") {
        fermerConnexion();
        programmerReconnexion(true);
      } else if (code === "salon-inconnu" || code === "jeton-invalide" || code === "salon-plein" || code === "version" || code === "requete-invalide") {
        s.arret = true;
        s.erreur = m.message || code;
        if (code === "salon-inconnu" || code === "jeton-invalide") {
          oublier();
          arreterJeu();
        }
        fermerConnexion();
      }
      // message-invalide, destinataire-interdit, reserve-a-l-hote : le relais garde la connexion ; on note seulement.
    }

    function arreterJeu() {
      if (s.actif) {
        s.actif = false;
        if (moteur.coopOublier) {
          moteur.coopOublier(-1);
        }
        moteur.desactiver();
      }
      s.restants = 0;
      s.attendSauvegardeInvite = false;
      s.resyncEnAttente = null;
    }

    // ------------------------------------------------------------ messages de jeu (binaire)

    function traiterBinaire(octets) {
      if (octets.length < ENTETE) {
        return;
      }
      var m = decoder(octets);
      switch (m.type) {
        case TYPE.TOUR:
          surTour(m);
          break;
        case TYPE.VITESSE:
          surVitesse(m);
          break;
        case TYPE.EMPREINTE:
          surEmpreinte(m);
          break;
        case TYPE.SAUVEGARDE_DEMANDE:
          surDemandeSauvegarde(m);
          break;
        case TYPE.SAUVEGARDE:
          surSauvegarde(m);
          break;
        case TYPE.CHAT:
          surChat(m);
          break;
        case TYPE.PING:
          surPing(m);
          break;
        case TYPE.PRESENCE:
          surPresence(m);
          break;
        case TYPE.SIGNAL:
          surSignal(m);
          break;
        default:
          break;
      }
    }

    // ------------------------------------------------------------ Coop (M7) : hors lockstep

    function oublierJoueur(j) {
      delete s.latences[j];
      if (moteur.coopOublier) {
        moteur.coopOublier(j);
      }
    }

    function ajouterChat(joueur, texte, signal) {
      s.chatId++;
      s.chat.push({ id: s.chatId, joueur: joueur, texte: texte, signal: signal });
      while (s.chat.length > o.chatMax) {
        s.chat.shift();
      }
    }

    // Texte : UTF-8, envoyé à tous (émetteur compris : la ligne s'affiche à l'écho, dans le même
    // ordre chez tous).
    function surChat(m) {
      var texte;
      try {
        texte = new TextDecoder("utf-8", { fatal: true }).decode(m.donnees.subarray(0, o.chatOctets));
      } catch (e) {
        return;
      }
      texte = texte.replace(/[\u0000-\u001f\u007f]/g, " ").trim();
      if (!texte) {
        return;
      }
      stats.chatsRecus++;
      ajouterChat(m.joueur, texte, -1);
    }

    // PING : u8 sorte (0 demande, 1 réponse), u32 numéro, f64 heure d'envoi (horloge de l'émetteur).
    function surPing(m) {
      var d = m.donnees;
      if (d.length < 13) {
        return;
      }
      if (d[0] === 0) {
        var r = d.slice(0, 13);
        r[0] = 1;
        envoyerBin(TYPE.PING, 0, r, m.joueur);
      } else if (d[0] === 1) {
        var envoi = new DataView(d.buffer, d.byteOffset, d.byteLength).getFloat64(5, true);
        var ms = horloge.maintenant() - envoi;
        if (ms >= 0 && ms < 600000) {
          s.latences[m.joueur] = Math.round(ms);
        }
      }
    }

    function surPresence(m) {
      if (m.joueur === s.joueur || !moteur.coopPresenceAutre) {
        return;
      }
      stats.presencesRecues++;
      moteur.coopPresenceAutre(m.joueur, m.donnees);
    }

    // SIGNAL : i32 case de la carte (grid offset). Envoyé à tous, émetteur compris.
    function surSignal(m) {
      if (m.donnees.length < 4) {
        return;
      }
      var g = new DataView(m.donnees.buffer, m.donnees.byteOffset, 4).getInt32(0, true);
      if (moteur.coopSignal && !moteur.coopSignal(m.joueur, g)) {
        return;
      }
      stats.signauxRecus++;
      ajouterChat(m.joueur, "a signalé un endroit (touche G).", g);
    }

    function envoyerPresence(t) {
      if (!moteur.coopPresence || t - s.dernierePresence < o.presenceMs) {
        return;
      }
      var p = moteur.coopPresence();
      if (!p) {
        return;
      }
      var cle = Array.prototype.join.call(p, ",");
      if (cle === s.presenceEnvoyee && t - s.dernierePresence < o.presenceMaxMs) {
        return;
      }
      if (envoyerBin(TYPE.PRESENCE, 0, p, DEST_AUTRES)) {
        stats.presencesEnvoyees++;
        s.presenceEnvoyee = cle;
        s.dernierePresence = t;
      }
    }

    function mesurerLatences(t) {
      if (t - s.dernierPingJoueurs < o.latenceMs) {
        return;
      }
      s.dernierPingJoueurs = t;
      for (var i = 0; i < s.salon.length; i++) {
        var j = s.salon[i].joueur;
        if (j === s.joueur || !s.salon[i].connecte) {
          continue;
        }
        var d = new Uint8Array(13);
        var v = new DataView(d.buffer);
        d[0] = 0;
        v.setUint32(1, ++s.pingId >>> 0, true);
        v.setFloat64(5, t, true);
        envoyerBin(TYPE.PING, 0, d, j);
      }
    }

    function lireConflits(t) {
      if (!moteur.coopConflits) {
        return;
      }
      var n = moteur.coopConflits();
      if (n > 0) {
        stats.conflits += n;
        s.avis = "L'autre joueur a agi au même moment : ton ordre n'a pas abouti.";
        s.avisJusqua = t + 6000;
      }
    }

    // Page : envoie une ligne de chat. Faux si pas connecté ou texte vide.
    function envoyerChat(texte) {
      texte = String(texte || "").replace(/[\u0000-\u001f\u007f]/g, " ").trim();
      if (!texte || !s.session) {
        return false;
      }
      var octets = new TextEncoder().encode(texte);
      if (octets.length > o.chatOctets) {
        // Coupé sans casser un caractère : on retire des caractères jusqu'à tenir.
        while (octets.length > o.chatOctets) {
          texte = texte.slice(0, -1);
          octets = new TextEncoder().encode(texte);
        }
      }
      return envoyerBin(TYPE.CHAT, 0, octets, DEST_TOUS);
    }

    // Page (touche G) : signale la case sous la souris. Faux si la souris n'est pas sur la carte.
    function signaler() {
      if (!s.actif || !moteur.coopCurseur || villeQuittee()) {
        return false;
      }
      var g = moteur.coopCurseur();
      if (g < 0) {
        return false;
      }
      var d = new Uint8Array(4);
      new DataView(d.buffer).setInt32(0, g, true);
      return envoyerBin(TYPE.SIGNAL, 0, d, DEST_TOUS);
    }

    // Page : centre la vue sur la case d'un signal du chat.
    function allerA(g) {
      if (s.actif && moteur.coopAller && g >= 0) {
        moteur.coopAller(g);
      }
    }

    function debutGroupe(k) {
      return (s.T0 + k * G) >>> 0;
    }

    function surTour(m) {
      if (!s.actif || m.epoque !== s.epoque) {
        stats.toursIgnores++;
        return;
      }
      var delta = m.tick - s.T0;
      if (delta < 0 || delta % G !== 0) {
        stats.toursIgnores++;
        return;
      }
      var k = delta / G;
      if (k < D || k < s.prochain || k >= s.prochain + 2 * D + 1) {
        stats.toursIgnores++;
        return;
      }
      if (s.attendus.indexOf(m.joueur) < 0) {
        stats.toursIgnores++; // joueur hors liste : jamais appliqué, jamais gardé
        return;
      }
      var cle = k + ":" + m.joueur;
      if (!s.tours[cle]) {
        s.tours[cle] = m.donnees.slice();
      }
    }

    function surVitesse(m) {
      if (!s.actif || m.epoque !== s.epoque || m.donnees.length < 3) {
        return;
      }
      var pct = m.donnees[0] | (m.donnees[1] << 8);
      var pause = m.donnees[2] === 1;
      s.vitesses.push({ tick: m.tick, joueur: m.joueur, rang: s.rangVitesse++, pct: pct, pause: pause });
      s.projete = { pct: pct, pause: pause };
    }

    function surEmpreinte(m) {
      if (!s.actif || m.epoque !== s.epoque || m.donnees.length < 8) {
        return;
      }
      var h = hex(m.donnees.subarray(0, 8));
      if (!s.empreintes[m.tick]) {
        s.empreintes[m.tick] = {};
      }
      s.empreintes[m.tick][m.joueur] = h;
      if (o.surEmpreinte) {
        o.surEmpreinte(m.tick, m.joueur, h, s.epoque);
      }
      comparer(m.tick);
    }

    // Texte compact du détail par morceau : « morceau/champ=empreinte » (voir docs/determinisme.md).
    function resumeDetail(texte) {
      var lignes = texte.split("\n");
      var r = [];
      for (var i = 0; i < lignes.length; i++) {
        var p = lignes[i].split(" ");
        // « morceau N champ C taille T empreinte » (M12 : l'empreinte, et non la taille, est la 7e colonne)
        if (p[0] === "morceau" && p.length >= 7) {
          r.push(p[1] + "/" + p[3] + "=" + p[6].slice(0, 8));
        } else if (p[0] && p[0] !== "#" && p.length >= 2) {
          r.push(p[0] + "=" + p[p.length - 1].slice(0, 8));
        }
      }
      return r.join(" ");
    }

    // Une divergence réparée par la resynchronisation n'est pas un succès : la cause est un défaut du
    // moteur. On la compte, on journalise le détail par morceau (le nôtre, celui de l'hôte) et, si elles
    // se répètent, on prévient le joueur à l'écran.
    function signalerDivergence(tick, joueur, theirs, mienne) {
      var t = horloge.maintenant();
      stats.divergences++;
      journal("divergence au tick " + tick + " : joueur " + (joueur + 1) + " " + theirs + " contre hôte " + mienne);
      var detail = s.details[tick] ? resumeDetail(s.details[tick]) : "";
      s.detailDivergence = detail;
      if (detail) {
        journal("détail hôte au tick " + tick + " (morceau/champ=empreinte) : " + detail);
      }
      s.divergencesRecentes.push(t);
      var gardees = [];
      for (var i = 0; i < s.divergencesRecentes.length; i++) {
        if (t - s.divergencesRecentes[i] <= o.fenetreDivergencesMs) {
          gardees.push(s.divergencesRecentes[i]);
        }
      }
      s.divergencesRecentes = gardees;
      if (gardees.length >= o.divergencesMax) {
        s.alerte = "Les deux parties divergent à répétition (" + gardees.length + " fois en " + Math.round(o.fenetreDivergencesMs / 60000) + " min). " +
          "C'est un défaut du jeu : ouvrez la console du navigateur (journal « divergence ») et gardez-la pour le correctif.";
        journal("ALERTE : " + s.alerte);
      }
      if (o.surDivergence) {
        o.surDivergence(tick, joueur, theirs, mienne, detail);
      }
    }

    function comparer(tick) {
      // Seul l'hôte compare ; il resynchronise tout le monde si un invité diffère de lui.
      if (s.joueur !== 0 || s.monEmpreinte[tick] === undefined || !s.empreintes[tick]) {
        return;
      }
      var mienne = s.monEmpreinte[tick];
      var autres = s.empreintes[tick];
      for (var j in autres) {
        if (Number(j) === 0 || autres[j] === null) {
          continue;
        }
        stats.empreintesComparees++;
        var h = autres[j];
        autres[j] = null; // comparée une fois
        if (h !== mienne) {
          signalerDivergence(tick, Number(j), h, mienne);
          demanderResync("divergence");
          return;
        }
      }
    }

    function surDemandeSauvegarde(m) {
      if (s.joueur === 0) {
        // Un invité demande la sauvegarde : ignoré si sa demande date d'une autre époque.
        if (s.actif && m.epoque === s.epoque) {
          demanderResync("demande du joueur " + (m.joueur + 1));
        }
        return;
      }
      if (m.joueur === 0 && s.actif && !villeQuittee()) {
        // L'hôte a rechargé sa page (ou quitté la ville) : on lui envoie notre état (à lui seul).
        finirGroupe();
        if (!moteur.sauvegarder(o.chemin)) {
          return;
        }
        var charge = fabriquerCharge(s.epoque, s.attendus, moteur.vitesse(), moteur.enPause(), moteur.lireFichier(o.chemin));
        var msgs = morceauxSauvegarde(charge, moteur.tick(), s.epoque, 0);
        if (!envoyerTous(msgs)) {
          journal("sauvegarde non envoyée à l'hôte : connexion perdue (il la redemandera)");
          return;
        }
        journal("sauvegarde envoyée à l'hôte (" + charge.length + " octets)");
      }
    }

    function surSauvegarde(m) {
      var r = s.receptions[m.joueur];
      if (!r) {
        r = s.receptions[m.joueur] = new Reception();
      }
      var complete = r.ajouter(m.donnees);
      if (r.erreur && r.erreur !== "début du transfert manquant") {
        stats.sauvegardesRejetees++;
        journal("sauvegarde rejetée : " + r.erreur);
      }
      if (!complete) {
        return;
      }
      var info = lireCharge(complete);
      if (!info) {
        stats.sauvegardesRejetees++;
        journal("sauvegarde illisible (préfixe invalide)");
        return;
      }
      if (s.joueur !== 0 && m.joueur === 0) {
        appliquerSauvegarde(info, true);
      } else if (s.joueur === 0 && m.joueur !== 0 && s.attendSauvegardeInvite) {
        // Notre propre état, retrouvé chez un invité : on le charge puis on le renvoie à tous.
        s.attendSauvegardeInvite = false;
        s.rechargee = false;
        s.epoque = info.epoque;
        s.resyncJusqua = horloge.maintenant() + 2000;
        moteur.activer(s.joueur);
        moteur.ecrireFichier(o.chemin, info.sauvegarde);
        if (!moteur.charger(o.chemin)) {
          s.erreur = "Impossible de charger la partie récupérée.";
          moteur.desactiver();
          return;
        }
        stats.chargements++;
        s.actif = false;
        demanderResync("rechargement de l'hôte");
      }
    }

    // ------------------------------------------------------------ chargement et resynchronisation

    function demarrerPartie(info) {
      s.epoque = info.epoque;
      s.attendus = info.attendus.slice();
      s.T0 = moteur.tick();
      s.prochain = 0;
      s.restants = 0; // le chargement a vidé la file des commandes du moteur (brutus_partie_chargee)
      s.envoye = D;
      s.tours = {};
      s.vitesses = [];
      s.empreintes = {};
      s.monEmpreinte = {};
      s.details = {};
      s.rangVitesse = 0;
      // Commandes du moteur de l'époque précédente : abandonnées (elles visaient un état qui n'existe plus).
      moteur.sortie();
      reserve = new Uint8Array(0);
      s.actif = true;
      s.projete = { pct: info.vitesse, pause: info.pause };
      s.vitesseCommune = { pct: info.vitesse, pause: info.pause };
      moteur.vitesseAppliquer(info.vitesse, info.pause);
      var t = horloge.maintenant();
      s.echeance = t;
      s.dernierProgres = t;
      s.resyncJusqua = t + 2000;
    }

    // Charge la sauvegarde reçue (ou la nôtre, pour l'hôte) puis démarre le lockstep de cette époque.
    function appliquerSauvegarde(info, ecrire) {
      if (s.actif && s.joueur !== 0) {
        // Resynchronisation chez un invité : on journalise nos détails pour les comparer à ceux de l'hôte.
        var dt = Object.keys(s.details).map(Number).sort(function (a, b) { return a - b; }).slice(-2);
        for (var d = 0; d < dt.length; d++) {
          journal("détail invité au tick " + dt[d] + " (morceau/champ=empreinte) : " + resumeDetail(s.details[dt[d]]));
        }
      }
      moteur.activer(s.joueur);
      if (ecrire) {
        moteur.ecrireFichier(o.chemin, info.sauvegarde);
      }
      if (!moteur.charger(o.chemin)) {
        stats.sauvegardesRejetees++;
        journal("chargement de la sauvegarde refusé par le moteur");
        arreterJeu();
        if (s.joueur !== 0 && !s.rechargeEssayee && s.connecte) {
          // Invité : l'hôte attend ; on redemande la sauvegarde une seule fois (avec l'époque de l'hôte).
          s.rechargeEssayee = true;
          s.epoque = info.epoque;
          envoyerBin(TYPE.SAUVEGARDE_DEMANDE, 0, new Uint8Array(0), 0);
          journal("sauvegarde redemandée à l'hôte");
        } else {
          s.erreur = "Impossible de charger la partie reçue.";
        }
        return false;
      }
      s.rechargeEssayee = false;
      stats.chargements++;
      demarrerPartie(info);
      journal("partie chargée : époque " + info.epoque + ", tick " + s.T0 + ", attendus " + info.attendus.join(","));
      return true;
    }

    function demanderResync(motif) {
      s.resyncEnAttente = motif;
      s.prochainResync = 0;
    }

    function placesAttribuees() {
      var l = [];
      for (var i = 0; i < s.salon.length; i++) {
        l.push(s.salon[i].joueur);
      }
      l.sort(function (a, b) { return a - b; });
      return l;
    }

    // Hôte : sauvegarde, envoi aux autres, rechargement par l'hôte lui-même. Retourne vrai si fait.
    function resyncHote(motif) {
      if (s.joueur !== 0 || !s.connecte || s.attendSauvegardeInvite) {
        return false;
      }
      // En partie : on ne se fie pas à jeuEnCours (qui exclut la fenêtre d'options, les boîtes de
      // sauvegarde…), seulement à « la ville a-t-elle été quittée ». Avant la partie : la ville doit être affichée.
      if (s.actif ? villeQuittee() : !moteur.jeuEnCours()) {
        return false;
      }
      finirGroupe();
      if (!moteur.sauvegarder(o.chemin)) {
        journal("sauvegarde impossible");
        return false;
      }
      var octets = moteur.lireFichier(o.chemin);
      var epoque = (s.epoque % 255) + 1;
      var attendus = placesAttribuees();
      var vitesse = moteur.vitesse();
      var pause = s.actif ? moteur.enPause() : false;
      var charge = fabriquerCharge(epoque, attendus, vitesse, pause, octets);
      var msgs = morceauxSauvegarde(charge, moteur.tick(), epoque, DEST_AUTRES);
      if (!envoyerTous(msgs)) {
        // Socket mort : on ne recharge pas seul (les autres n'ont rien reçu) ; nouvel essai plus tard.
        stats.envoisRates++;
        journal("resynchronisation (" + motif + ") : envoi impossible, nouvel essai");
        return false;
      }
      stats.resyncsHote++;
      journal("resynchronisation (" + motif + ") : époque " + epoque + ", " + charge.length + " octets");
      var info = { epoque: epoque, attendus: attendus, vitesse: vitesse, pause: pause, sauvegarde: octets };
      return appliquerSauvegarde(info, false);
    }

    // ------------------------------------------------------------ boucle

    // Commandes sorties du moteur mais pas encore parties (envoi refusé) : gardées pour le prochain TOUR.
    var reserve = new Uint8Array(0);

    function envoyerTours() {
      if (!s.connecte) {
        return;
      }
      while (s.envoye < s.prochain + D) {
        // Seul le premier TOUR de la boucle porte les commandes : la boîte de sortie est vide ensuite.
        var neuves = moteur.sortie();
        var donnees = neuves;
        if (reserve.length) {
          donnees = new Uint8Array(reserve.length + neuves.length);
          donnees.set(reserve, 0);
          donnees.set(neuves, reserve.length);
        }
        if (!envoyerBin(TYPE.TOUR, debutGroupe(s.envoye), donnees, DEST_TOUS)) {
          reserve = donnees;
          return;
        }
        reserve = new Uint8Array(0);
        s.envoye++;
      }
    }

    function lireDemandes() {
      if (!s.actif || !s.connecte || villeQuittee()) {
        return;
      }
      var dv = moteur.demandeVitesse();
      var dp = moteur.demandePause();
      if (!dv && !(dp % 2)) {
        return;
      }
      var pct = s.projete.pct;
      var pause = s.projete.pause;
      for (var i = 0; i < Math.abs(dv); i++) {
        pct = pasVitesse(pct, dv > 0 ? 1 : -1);
      }
      if (dp % 2) {
        pause = !pause;
      }
      if (pct === s.projete.pct && pause === s.projete.pause) {
        return;
      }
      // Déjà en pause : le tick d'effet est le départ du groupe en attente, sinon la reprise s'enfermerait.
      var effet = moteur.enPause() ? debutGroupe(s.prochain) : debutGroupe(s.envoye);
      var d = new Uint8Array(3);
      d[0] = pct & 255;
      d[1] = (pct >> 8) & 255;
      d[2] = pause ? 1 : 0;
      if (envoyerBin(TYPE.VITESSE, effet, d, DEST_TOUS)) {
        s.projete = { pct: pct, pause: pause };
      }
    }

    function groupeComplet(k) {
      if (k < D) {
        return true;
      }
      for (var i = 0; i < s.attendus.length; i++) {
        if (!s.tours[k + ":" + s.attendus[i]]) {
          return false;
        }
      }
      return true;
    }

    // Les changements de vitesse dont le tick d'effet est atteint, dans un ordre qui ne dépend pas de
    // l'ordre d'arrivée (tick, puis joueur, puis ordre d'envoi du même joueur) : deux joueurs qui
    // changent la vitesse en même temps aboutissent au même résultat chez tous.
    function appliquerVitesses(debut) {
      var prets = [];
      var reste = [];
      for (var i = 0; i < s.vitesses.length; i++) {
        (s.vitesses[i].tick <= debut ? prets : reste).push(s.vitesses[i]);
      }
      prets.sort(function (a, b) {
        return a.tick - b.tick || a.joueur - b.joueur || a.rang - b.rang;
      });
      for (var j = 0; j < prets.length; j++) {
        s.vitesseCommune = { pct: prets[j].pct, pause: prets[j].pause };
        moteur.vitesseAppliquer(prets[j].pct, prets[j].pause);
      }
      s.vitesses = reste;
    }

    // La vitesse et la pause communes sont celles des VITESSE appliquées. Si le moteur en a une autre
    // (un réglage local qui aurait échappé aux gardes du moteur), on la corrige et on le dit : le rythme
    // ne doit pas dépendre d'un réglage local.
    function verifierVitesse() {
      var v = s.vitesseCommune;
      if (v && moteur.vitesse() !== v.pct) {
        stats.vitessesCorrigees++;
        journal("vitesse locale " + moteur.vitesse() + " % remise à la vitesse commune " + v.pct + " %");
        moteur.vitesseAppliquer(v.pct, v.pause);
      }
    }

    // Joue le groupe `prochain` s'il est complet. Retourne vrai si les ticks ont avancé.
    // Démarre le groupe s.prochain s'il est complet : vitesse et pause du groupe, puis commandes de
    // tous remises au moteur (chacune porte son tick). Ses G ticks se jouent ensuite un par un, à
    // l'heure (jouerTick) : jouer les 5 d'un coup faisait avancer les personnages par à-coups.
    function demarrerGroupe() {
      var k = s.prochain;
      if (!groupeComplet(k)) {
        return false;
      }
      var debut = debutGroupe(k);
      appliquerVitesses(debut);
      if (moteur.enPause()) {
        return false;
      }
      for (var i = 0; i < s.attendus.length; i++) {
        var cle = k + ":" + s.attendus[i];
        var tour = s.tours[cle];
        if (tour && tour.length) {
          if (!moteur.entree(s.attendus[i], debut, tour)) {
            journal("tour invalide de " + nomJoueur(s.attendus[i]) + " au tick " + debut);
          }
        }
        delete s.tours[cle];
      }
      s.prochain++;
      s.restants = G;
      return true;
    }

    // Un tick du groupe démarré ; l'empreinte part après son dernier tick (frontière de groupe).
    function jouerTick() {
      moteur.avancer(1);
      s.restants--;
      if (s.restants === 0) {
        stats.groupes++;
        if (s.prochain % o.empreinteGroupes === 0) {
          envoyerEmpreinte(debutGroupe(s.prochain));
        }
      }
    }

    // Joue tout de suite la fin du groupe démarré : une sauvegarde part toujours d'une frontière de groupe.
    function finirGroupe() {
      while (s.restants > 0) {
        jouerTick();
      }
    }

    function envoyerEmpreinte(tick) {
      var e = moteur.empreinte();
      s.monEmpreinte[tick] = hex(e);
      if (moteur.detailEmpreinte) {
        s.details[tick] = moteur.detailEmpreinte();
        var ticks = Object.keys(s.details).map(Number).sort(function (a, b) { return a - b; });
        while (ticks.length > 4) {
          delete s.details[ticks.shift()];
        }
      }
      stats.empreintesEnvoyees++;
      envoyerBin(TYPE.EMPREINTE, tick, e, DEST_TOUS);
      comparer(tick); // l'invité peut avoir été plus rapide : son empreinte attend déjà
      nettoyerEmpreintes(tick);
    }

    function nettoyerEmpreintes(tick) {
      var limite = tick - 20 * o.empreinteGroupes * G;
      var t;
      for (t in s.monEmpreinte) {
        if (Number(t) < limite) {
          delete s.monEmpreinte[t];
        }
      }
      for (t in s.empreintes) {
        if (Number(t) < limite) {
          delete s.empreintes[t];
        }
      }
    }

    function jouer(t) {
      if (villeQuittee()) {
        // Ville quittée (menu principal, fin de mission) : on n'avance plus hors de la ville. L'autre
        // joueur attend (« en attente de Joueur N ») ; le retour passe par revenir().
        s.dernierProgres = t;
        s.echeance = t;
        return;
      }
      verifierVitesse();
      // Cadence au tick près (comme le solo) : un tick dès que son heure est passée.
      var limite = (o.libre ? o.maxGroupesParPompe : 1000) * G;
      var faits = 0;
      while (faits < limite && (o.libre || t >= s.echeance)) {
        if (!s.restants && !demarrerGroupe()) {
          break;
        }
        jouerTick();
        faits++;
        s.dernierProgres = t;
        if (!o.libre) {
          s.echeance += msParTick(moteur.vitesse());
        }
      }
      if (!o.libre) {
        if (moteur.enPause()) {
          s.echeance = t; // en pause : pas de temps à rattraper à la reprise
        } else {
          // Rattrapage limité : après une longue attente, on ne rejoue pas tout d'un coup.
          var ms = msParTick(moteur.vitesse()) * G;
          if (t - s.echeance > o.rattrapageGroupes * ms) {
            s.echeance = t - o.rattrapageGroupes * ms;
          }
        }
      }
      if (!faits && moteur.enPause()) {
        s.dernierProgres = t; // en pause : rien n'est attendu
      }
    }

    function gererConnexion(t) {
      if (!s.session) {
        return;
      }
      if (!s.conn) {
        if (!s.arret && t >= s.prochainEssai) {
          reconnecter(true);
        }
        return;
      }
      if (t - s.dernierRecu > o.morteMs) {
        // Coupure silencieuse : plus aucun message du relais (pas même pong) depuis trop longtemps.
        stats.coupuresSilencieuses++;
        journal("coupure silencieuse détectée : reconnexion");
        fermerConnexion();
        programmerReconnexion(true);
        return;
      }
      if (t - s.dernierPing >= o.pingMs) {
        s.dernierPing = t;
        envoyerTexte("ping");
      }
    }

    // Hôte qui attend sa partie auprès d'un invité depuis trop longtemps (aucun invité connecté, ou les
    // deux pages rechargées) : on abandonne la partie à deux, avec un message, au lieu de réessayer sans fin.
    function abandonnerRecuperation() {
      stats.recuperationsAbandonnees++;
      journal("récupération de la partie abandonnée après " + Math.round(o.recuperationMs / 1000) + " s");
      arreterJeu();
      s.arret = true;
      fermerConnexion();
      oublier();
      s.erreur = "La partie n'a pas pu être récupérée : aucun autre joueur n'a pu la renvoyer. La partie multijoueur est abandonnée ; vous pouvez en créer une nouvelle.";
    }

    function gererResync(t) {
      if (s.joueur === 0 && s.attendSauvegardeInvite && t - s.debutRecuperation > o.recuperationMs) {
        abandonnerRecuperation();
        return;
      }
      if (s.joueur === 0 && s.attendSauvegardeInvite && s.connecte && t - s.derniereDemande >= o.demandeSauvegardeMs) {
        // Hôte qui a rechargé sa page : demande à l'invité connecté de plus petit numéro.
        s.derniereDemande = t;
        var cible = -1;
        for (var i = 0; i < s.salon.length; i++) {
          if (s.salon[i].joueur !== 0 && s.salon[i].connecte && (cible < 0 || s.salon[i].joueur < cible)) {
            cible = s.salon[i].joueur;
          }
        }
        if (cible >= 0) {
          envoyerBin(TYPE.SAUVEGARDE_DEMANDE, 0, new Uint8Array(0), cible);
          journal("sauvegarde demandée au joueur " + (cible + 1));
        } else {
          journal("aucun invité connecté pour renvoyer la partie");
        }
      }
      if (s.resyncEnAttente !== null && t >= s.prochainResync) {
        if (s.joueur === 0 && resyncHote(s.resyncEnAttente)) {
          s.resyncEnAttente = null;
        } else if (s.joueur !== 0) {
          s.resyncEnAttente = null;
        } else {
          s.prochainResync = t + o.reessaiResyncMs;
        }
      }
    }

    var enPompe = false;

    function pomper() {
      if (enPompe) {
        return; // un message livré pendant le traitement : le prochain passage s'en chargera
      }
      enPompe = true;
      try {
        var t = horloge.maintenant();
        gererConnexion(t);
        gererResync(t);
        if (moteur.refusMenu) {
          var refus = moteur.refusMenu();
          if (refus > 0) {
            stats.menusRefuses += refus;
            s.avis = "Charger, Recommencer et Quitter la mission sont désactivés pendant une partie multijoueur.";
            s.avisJusqua = t + 6000;
          }
        }
        if (s.actif) {
          lireDemandes();
          envoyerTours();
          jouer(t);
          envoyerTours();
          if (s.connecte && !villeQuittee()) {
            envoyerPresence(t);
          }
          lireConflits(t);
        }
        if (s.connecte) {
          mesurerLatences(t);
        }
        notifier();
      } finally {
        enPompe = false;
      }
    }

    // ------------------------------------------------------------ commandes de la page et des tests

    function expulser(j) {
      if (s.joueur === 0) {
        envoyerTexte(JSON.stringify({ type: "expulser", joueur: j }));
      }
    }

    function quitter() {
      s.arret = true;
      arreterJeu();
      fermerConnexion();
      oublier();
      s.erreur = null;
      notifier();
    }

    // Tests : force une resynchronisation (l'hôte la lance ; un invité la demande à l'hôte).
    function forcerResync() {
      if (s.joueur === 0) {
        demanderResync("forcée");
      } else {
        envoyerBin(TYPE.SAUVEGARDE_DEMANDE, 0, new Uint8Array(0), 0);
      }
    }

    // Après avoir quitté la ville (menu principal, fin de mission) : revenir dans la partie à deux.
    // Invité : demande la partie à l'hôte, tous rechargent. Hôte : réclame sa partie à un invité (comme
    // après un rechargement de page), puis la renvoie à tous. Retourne faux si rien à faire.
    function revenir() {
      if (!s.actif || !s.connecte || !villeQuittee()) {
        return false;
      }
      if (s.joueur === 0) {
        s.attendSauvegardeInvite = true;
        s.derniereDemande = 0;
        s.debutRecuperation = horloge.maintenant();
      } else {
        envoyerBin(TYPE.SAUVEGARDE_DEMANDE, 0, new Uint8Array(0), 0);
      }
      journal("retour dans la partie demandé");
      return true;
    }

    // Tests : coupe la connexion sans prévenir le serveur (comme un réseau qui tombe) ou normalement.
    function couperConnexion() {
      var immediat = true;
      fermerConnexion();
      programmerReconnexion(immediat);
    }

    return {
      creer: creer,
      rejoindre: rejoindre,
      reprendre: reprendre,
      quitter: quitter,
      expulser: expulser,
      pomper: pomper,
      etat: etat,
      stats: stats,
      forcerResync: forcerResync,
      revenir: revenir,
      couperConnexion: couperConnexion,
      envoyerChat: envoyerChat,
      signaler: signaler,
      allerA: allerA,
      // Tests seulement.
      interne: s,
      moteur: moteur,
    };
  }

  var API = {
    creerLockstep: creerLockstep,
    transportWebSocket: transportWebSocket,
    moteurEmscripten: moteurEmscripten,
    stockageSession: stockageSession,
    encoder: encoder,
    decoder: decoder,
    Reception: Reception,
    fabriquerCharge: fabriquerCharge,
    lireCharge: lireCharge,
    TYPE: TYPE,
    DEST_TOUS: DEST_TOUS,
    DEST_AUTRES: DEST_AUTRES,
    msParTick: msParTick,
    VERSION_PROTOCOLE: VERSION_PROTOCOLE,
  };
  racine.BrutusLockstep = API;
  if (typeof module !== "undefined" && module.exports) {
    module.exports = API;
  }
})(typeof globalThis !== "undefined" ? globalThis : this);
