// Brutus III : tests du relais sur le vrai runtime (workerd, via l'API de wrangler).
// Lancer : cd serveur-multi && npm test   (il faut pouvoir ouvrir un port sur 127.0.0.1)
import { after, before, describe, test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { randomBytes } from "node:crypto";

import {
  ClientRelais, DEST_AUTRES, DEST_TOUS, Reception, TAILLE_MAX_MESSAGE, TYPE,
  encoder, morceauxSauvegarde,
} from "../client/client.mjs";
import { demarrerRelais } from "../client/serveur-local.mjs";

const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
let relais;
const ouverts = [];

function client(c) {
  ouverts.push(c);
  return c;
}

async function salonADeux() {
  const hote = client(ClientRelais.creer(relais.base));
  const b = await hote.bienvenue();
  const invite = client(ClientRelais.rejoindre(relais.base, b.code));
  await invite.bienvenue();
  await hote.attendreType("joueur-arrive");
  return { hote, invite, code: b.code };
}

async function refusAttendu(c, code, fermeture) {
  const e = await c.attendreType("erreur");
  assert.equal(e.code, code);
  const f = await c.ferme;
  assert.equal(f.code, fermeture);
}

/**
 * Barrière sans attente fixe : `emetteur` envoie un CHAT témoin à tout le salon. Le salon
 * traite les messages dans l'ordre, donc quand un récepteur reçoit le témoin, tout ce qui lui
 * était destiné avant est arrivé. Retourne, pour chaque récepteur, les messages reçus avant.
 */
let numeroTemoin = 0;
async function barriere(emetteur, ...recepteurs) {
  const temoin = "témoin " + ++numeroTemoin;
  emetteur.envoyer(TYPE.CHAT, 0, new TextEncoder().encode(temoin), DEST_TOUS);
  const avant = [];
  for (const c of recepteurs) {
    const liste = [];
    for (;;) {
      const m = await c.attendre();
      if (m.type === "binaire" && new TextDecoder().decode(m.donnees) === temoin) break;
      liste.push(m);
    }
    avant.push(liste);
  }
  return avant;
}

/** Poignée de main WebSocket faite à la main : statut HTTP (101 accepté, 403 origine refusée…). */
function poignee(chemin, origine) {
  return new Promise((resoudre, rejeter) => {
    const headers = {
      Connection: "Upgrade",
      Upgrade: "websocket",
      "Sec-WebSocket-Version": "13",
      "Sec-WebSocket-Key": randomBytes(16).toString("base64"),
    };
    if (origine) headers.Origin = origine;
    const req = http.request(new URL(chemin, relais.http), { headers });
    req.on("upgrade", (res, socket) => {
      socket.destroy();
      resoudre(res.statusCode);
    });
    req.on("response", (res) => {
      res.resume();
      resoudre(res.statusCode);
    });
    req.on("error", rejeter);
    req.end();
  });
}

function memes(recu, attendu) {
  assert.ok(recu instanceof Uint8Array, "sauvegarde non reçue");
  assert.ok(Buffer.from(recu).equals(Buffer.from(attendu)), "sauvegarde différente");
}

async function recevoirSauvegarde(c) {
  const reception = new Reception();
  for (;;) {
    const m = await c.attendreType("binaire");
    if (m.type !== TYPE.SAUVEGARDE) continue;
    const complete = reception.ajouter(m.donnees);
    assert.equal(reception.erreur, null);
    if (complete) return { sauvegarde: complete, joueur: m.joueur, tick: m.tick };
  }
}

const attendreMs = (ms) => new Promise((r) => setTimeout(r, ms));

before(async () => {
  // Délai de fermeture court pour tester l'alarme ; 4 joueurs permis (2 en production).
  relais = await demarrerRelais({ DELAI_FERMETURE_S: 1, JOUEURS_MAX: 4 });
});

after(async () => {
  for (const c of ouverts) {
    try { c.ws.close(); } catch { /* déjà fermé */ }
  }
  await relais?.arreter();
});

describe("relais Brutus III", () => {
  test("santé", async () => {
    const r = await fetch(new URL("/sante", relais.http));
    assert.equal(r.status, 200);
    assert.equal(await r.text(), "ok\n");
  });

  test("créer puis rejoindre : code, numéros, jetons, annonces", async () => {
    const hote = client(ClientRelais.creer(relais.base));
    const b = await hote.bienvenue();
    assert.equal(b.version, 1);
    assert.equal(b.joueur, 0);
    assert.equal(b.joueursMax, 2);
    assert.equal(b.reconnexion, false);
    assert.match(b.code, /^[A-Z0-9]{6}$/);
    for (const c of b.code) assert.ok(ALPHABET.includes(c), "caractère ambigu : " + c);
    assert.match(b.jeton, /^[0-9a-f]{32}$/);
    assert.deepEqual(b.joueurs, [{ joueur: 0, connecte: true }]);

    // Le code se tape aussi en minuscules.
    const invite = client(ClientRelais.rejoindre(relais.base, b.code.toLowerCase()));
    const bi = await invite.bienvenue();
    assert.equal(bi.joueur, 1);
    assert.equal(bi.code, b.code);
    assert.notEqual(bi.jeton, b.jeton);
    assert.deepEqual(bi.joueurs, [{ joueur: 0, connecte: true }, { joueur: 1, connecte: true }]);

    const arrive = await hote.attendreType("joueur-arrive");
    assert.equal(arrive.joueur, 1);
  });

  test("relais dans un ordre stable, émetteur écrit par le serveur, tours vides", async () => {
    const { hote, invite } = await salonADeux();
    const N = 200;
    for (let t = 0; t < N; t++) {
      // Tour vide (en-tête seul) une fois sur deux ; octet joueur faux, corrigé par le serveur.
      const donnees = t % 2 ? new Uint8Array([t & 0xff, 1, 2]) : new Uint8Array(0);
      const mh = encoder(TYPE.TOUR, t, donnees);
      mh[1] = 3;
      hote.envoyerBrut(mh);
      invite.envoyer(TYPE.TOUR, t, new Uint8Array(0));
    }
    const recu = async (c) => {
      const l = [];
      for (let i = 0; i < 2 * N; i++) {
        const m = await c.attendreType("binaire");
        l.push(`${m.type}:${m.joueur}:${m.tick}:${m.donnees.length}`);
      }
      return l;
    };
    const [lh, li] = await Promise.all([recu(hote), recu(invite)]);
    // Les deux joueurs voient exactement le même flux (émetteur compris)…
    assert.deepEqual(lh, li);
    // … et l'ordre de chaque émetteur est conservé, tours vides compris.
    for (const j of [0, 1]) {
      const ticks = lh.filter((s) => s.split(":")[1] === String(j)).map((s) => Number(s.split(":")[2]));
      assert.deepEqual(ticks, [...Array(N).keys()]);
    }
    assert.ok(lh.includes("1:0:0:0"), "tour vide de l'hôte relayé");
    assert.ok(lh.includes("1:0:1:3"), "tour de l'hôte avec ses données");
    assert.ok(!lh.some((s) => s.startsWith("1:3:")), "joueur réécrit par le serveur");
  });

  test("ordre identique à 4 joueurs qui envoient en même temps", async () => {
    const hote = client(ClientRelais.creer(relais.base, { joueurs: 4 }));
    const b = await hote.bienvenue();
    const joueurs = [hote];
    for (let n = 1; n < 4; n++) {
      const c = client(ClientRelais.rejoindre(relais.base, b.code));
      await c.bienvenue();
      joueurs.push(c);
    }
    // Annonces d'arrivée consommées par une barrière, puis 50 tours chacun en même temps.
    await barriere(hote, ...joueurs);
    const N = 50;
    for (let t = 0; t < N; t++) for (const c of joueurs) c.envoyer(TYPE.TOUR, t);
    const flux = await Promise.all(joueurs.map(async (c) => {
      const l = [];
      for (let i = 0; i < 4 * N; i++) {
        const m = await c.attendreType("binaire");
        l.push(`${m.joueur}:${m.tick}`);
      }
      return l;
    }));
    for (const f of flux.slice(1)) assert.deepEqual(f, flux[0]);
  });

  test("messages de session et destinataires", async () => {
    const { hote, invite } = await salonADeux();
    // Vitesse et pause, à tout le salon.
    invite.envoyer(TYPE.VITESSE, 120, new Uint8Array([100, 0, 1]));
    for (const c of [hote, invite]) {
      const m = await c.attendreType("binaire");
      assert.equal(m.type, TYPE.VITESSE);
      assert.equal(m.joueur, 1);
      assert.equal(m.tick, 120);
      assert.deepEqual([...m.donnees], [100, 0, 1]);
    }
    // Empreinte, à tout le salon.
    const e = new Uint8Array(8);
    new DataView(e.buffer).setBigUint64(0, 0x0123456789abcdefn, true);
    hote.envoyer(TYPE.EMPREINTE, 500, e);
    for (const c of [hote, invite]) {
      const me = await c.attendreType("binaire");
      assert.equal(me.type, TYPE.EMPREINTE);
      assert.equal(new DataView(me.donnees.buffer, me.donnees.byteOffset).getBigUint64(0, true), 0x0123456789abcdefn);
    }
    // Demande de sauvegarde à l'hôte ; chat aux autres ; ping de l'invité à lui-même.
    invite.envoyer(TYPE.SAUVEGARDE_DEMANDE, 0, undefined, 0);
    invite.envoyer(TYPE.CHAT, 0, new TextEncoder().encode("salut"), DEST_AUTRES);
    invite.envoyer(TYPE.PING, 0, new Uint8Array([9]), 1);
    const [avantHote, avantInvite] = await barriere(invite, hote, invite);
    assert.deepEqual(avantHote.map((m) => [m.type, m.joueur]), [[TYPE.SAUVEGARDE_DEMANDE, 1], [TYPE.CHAT, 1]]);
    assert.deepEqual(avantInvite.map((m) => m.type), [TYPE.PING]);
  });

  test("TOUR, VITESSE et EMPREINTE : seulement à tout le salon", async () => {
    const { hote, invite } = await salonADeux();
    for (const [type, dest] of [[TYPE.TOUR, 1], [TYPE.TOUR, DEST_AUTRES], [TYPE.VITESSE, DEST_AUTRES], [TYPE.EMPREINTE, 0]]) {
      hote.envoyer(type, 1, new Uint8Array(8), dest);
      assert.equal((await hote.attendreType("erreur")).code, "destinataire-interdit");
    }
    // Rien n'a été relayé et la connexion reste ouverte.
    const [avant] = await barriere(hote, invite);
    assert.deepEqual(avant, []);
    assert.equal(hote.fermeture, null);
  });

  test("ping texte : pong automatique", async () => {
    const { hote } = await salonADeux();
    hote.ws.send("ping");
    await hote.attendreType("pong");
  });

  test("sauvegarde de 100 Ko en 2 morceaux, de l'hôte aux autres", async () => {
    const { hote, invite } = await salonADeux();
    const sauvegarde = new Uint8Array(100 * 1024);
    for (let i = 0; i < sauvegarde.length; i++) sauvegarde[i] = (i * 7 + 3) & 0xff;
    const morceaux = morceauxSauvegarde(sauvegarde, 4242);
    assert.equal(morceaux.length, 2);
    for (const m of morceaux) hote.envoyerBrut(m);
    const r = await recevoirSauvegarde(invite);
    memes(r.sauvegarde, sauvegarde);
    assert.equal(r.tick, 4242);
    assert.equal(r.joueur, 0);
    const [avant] = await barriere(invite, hote);
    assert.deepEqual(avant, [], "l'hôte ne reçoit pas sa propre sauvegarde");
  });

  test("sauvegarde en 4 morceaux entrelacés de tours", async () => {
    const { hote, invite } = await salonADeux();
    const sauvegarde = new Uint8Array(200000).map((_, i) => (i * 31) & 0xff);
    const morceaux = morceauxSauvegarde(sauvegarde, 900);
    assert.equal(morceaux.length, 4);
    for (const [i, m] of morceaux.entries()) {
      hote.envoyerBrut(m);
      invite.envoyer(TYPE.TOUR, 900 + i);
    }
    const reception = new Reception();
    const tours = [];
    let complete = null;
    while (!complete || tours.length < 4) {
      const m = await invite.attendreType("binaire");
      if (m.type === TYPE.TOUR) tours.push(m.tick);
      else complete = reception.ajouter(m.donnees) || complete;
      assert.equal(reception.erreur, null);
    }
    memes(complete, sauvegarde);
    assert.deepEqual(tours, [900, 901, 902, 903]);
  });

  test("invité → hôte : sauvegarde permise vers l'hôte seulement", async () => {
    const { hote, invite } = await salonADeux();
    // Un invité ne peut pas envoyer de sauvegarde à tous (la connexion reste ouverte).
    invite.envoyerBrut(morceauxSauvegarde(new Uint8Array(10), 1, undefined, DEST_AUTRES)[0]);
    assert.equal((await invite.attendreType("erreur")).code, "destinataire-interdit");
    invite.envoyerBrut(morceauxSauvegarde(new Uint8Array(10), 1, undefined, DEST_TOUS)[0]);
    assert.equal((await invite.attendreType("erreur")).code, "destinataire-interdit");
    // Mais il peut la renvoyer à l'hôte (hôte qui a rechargé sa page).
    const s = new Uint8Array(70000).fill(42);
    for (const m of morceauxSauvegarde(s, 12, undefined, 0)) invite.envoyerBrut(m);
    const r = await recevoirSauvegarde(hote);
    memes(r.sauvegarde, s);
    assert.equal(r.joueur, 1);
  });

  test("message invalide signalé sans fermer", async () => {
    const { hote } = await salonADeux();
    hote.envoyerBrut(new Uint8Array([1, 0, 0]));
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.envoyerBrut(encoder(0x7e, 0));
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.envoyer(TYPE.CHAT, 0, undefined, 3); // joueur 3 : hors d'un salon à 2
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.ws.send("bonjour");
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.ws.send(JSON.stringify({ type: "autre" }));
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    assert.equal(hote.fermeture, null);
  });

  test("message trop gros refusé", async () => {
    const { hote, invite } = await salonADeux();
    // Exactement la limite : relayé.
    hote.envoyer(TYPE.CHAT, 0, new Uint8Array(TAILLE_MAX_MESSAGE - 8), DEST_AUTRES);
    assert.equal((await invite.attendreType("binaire")).donnees.length, TAILLE_MAX_MESSAGE - 8);
    // Un octet de plus : erreur puis fermeture 4009, et les autres sont prévenus.
    hote.envoyer(TYPE.CHAT, 0, new Uint8Array(TAILLE_MAX_MESSAGE - 7), DEST_AUTRES);
    await refusAttendu(hote, "message-trop-gros", 4009);
    assert.equal((await invite.attendreType("joueur-parti")).joueur, 0);
    const [avant] = await barriere(invite, invite);
    assert.deepEqual(avant, []);
  });

  test("salon plein", async () => {
    const { code } = await salonADeux();
    const troisieme = client(ClientRelais.rejoindre(relais.base, code));
    await refusAttendu(troisieme, "salon-plein", 4002);
  });

  test("salon de 4 joueurs, limite réglable", async () => {
    const hote = client(ClientRelais.creer(relais.base, { joueurs: 4 }));
    const b = await hote.bienvenue();
    assert.equal(b.joueursMax, 4);
    for (const n of [1, 2, 3]) {
      const c = client(ClientRelais.rejoindre(relais.base, b.code));
      assert.equal((await c.bienvenue()).joueur, n);
    }
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, b.code)), "salon-plein", 4002);
    await refusAttendu(client(ClientRelais.creer(relais.base, { joueurs: 5 })), "requete-invalide", 4004);
    await refusAttendu(client(ClientRelais.creer(relais.base, { joueurs: 1 })), "requete-invalide", 4004);
  });

  test("invités qui arrivent en même temps : numéros et jetons distincts et cohérents", async () => {
    const hote = client(ClientRelais.creer(relais.base, { joueurs: 4 }));
    const b = await hote.bienvenue();
    const invites = [1, 2, 3].map(() => client(ClientRelais.rejoindre(relais.base, b.code)));
    const bienvenues = await Promise.all(invites.map((c) => c.bienvenue()));
    assert.deepEqual(bienvenues.map((x) => x.joueur).sort(), [1, 2, 3]);
    assert.equal(new Set(bienvenues.map((x) => x.jeton)).size, 3);
    // Chaque jeton ramène bien à la place annoncée avec lui.
    for (const c of invites) {
      const retour = client(ClientRelais.rejoindre(relais.base, b.code, { jeton: c.jeton }));
      assert.equal((await retour.bienvenue()).joueur, c.joueur);
      await refusAttendu(c, "remplace", 4005);
    }
  });

  test("code inconnu", async () => {
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "ZZZZZZ")), "salon-inconnu", 4001);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "ABC")), "salon-inconnu", 4001);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "AB0O1I")), "salon-inconnu", 4001);
  });

  test("version différente ou absente", async () => {
    await refusAttendu(client(ClientRelais.creer(relais.base, { version: 2 })), "version", 4000);
    await refusAttendu(client(ClientRelais.creer(relais.base, { version: null })), "version", 4000);
    const { code } = await salonADeux();
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { version: 0 })), "version", 4000);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { version: null })), "version", 4000);
  });

  test("origine", async () => {
    assert.equal(await poignee("/creer?version=1", "http://localhost:8000"), 101);
    assert.equal(await poignee("/creer?version=1", "http://127.0.0.1:8000"), 101);
    assert.equal(await poignee("/creer?version=1", "https://brutus.ivwi.fr"), 101);
    assert.equal(await poignee("/creer?version=1", null), 101);
    assert.equal(await poignee("/creer?version=1", "https://exemple.com"), 403);
    assert.equal(await poignee("/creer?version=1", "https://brutus.ivwi.fr.exemple.com"), 403);
    assert.equal(await poignee("/rejoindre/ABCDEF?version=1", "http://localhost.exemple.com"), 403);
  });

  test("déconnexion puis reconnexion avec le jeton, à la même place", async () => {
    const { hote, invite, code } = await salonADeux();
    const jeton = invite.jeton;
    await invite.fermer();
    const parti = await hote.attendreType("joueur-parti");
    assert.equal(parti.joueur, 1);
    assert.deepEqual(parti.joueurs, [{ joueur: 0, connecte: true }, { joueur: 1, connecte: false }]);

    // La place est gardée : un nouveau venu sans jeton trouve le salon plein.
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code)), "salon-plein", 4002);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { jeton: "0".repeat(32) })), "jeton-invalide", 4003);

    const retour = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    const b = await retour.bienvenue();
    assert.equal(b.joueur, 1);
    assert.equal(b.reconnexion, true);
    assert.equal(b.jeton, jeton);
    assert.equal((await hote.attendreType("joueur-revenu")).joueur, 1);

    // Le relais reprend.
    retour.envoyer(TYPE.TOUR, 77);
    const m = await hote.attendreType("binaire");
    assert.equal(m.joueur, 1);
    assert.equal(m.tick, 77);

    // Reconnexion alors que l'ancienne connexion est encore ouverte : l'ancienne est fermée,
    // et les autres voient « joueur-revenu » SANS « joueur-parti » avant.
    const double = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    assert.equal((await double.bienvenue()).joueur, 1);
    await refusAttendu(retour, "remplace", 4005);
    const [avant] = await barriere(double, hote);
    assert.deepEqual(avant.map((x) => x.type), ["joueur-revenu"]);
  });

  test("jeton d'un autre salon refusé", async () => {
    const a = await salonADeux();
    const b = await salonADeux();
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, a.code, { jeton: b.invite.jeton })), "jeton-invalide", 4003);
  });

  test("même jeton utilisé deux fois en même temps : une seule connexion reste", async () => {
    const { hote, invite, code } = await salonADeux();
    const jeton = invite.jeton;
    const x = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    const y = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    await Promise.all([x.bienvenue(), y.bienvenue()]);
    await refusAttendu(invite, "remplace", 4005);
    const premiere = await Promise.race([x.ferme.then(() => x), y.ferme.then(() => y)]);
    const restante = premiere === x ? y : x;
    assert.equal(premiere.fermeture.code, 4005);
    // Un tour de l'hôte n'arrive qu'une fois au joueur 1, sur la connexion restante.
    hote.envoyer(TYPE.TOUR, 5);
    const [avant] = await barriere(hote, restante);
    assert.deepEqual(avant.filter((m) => m.type === "binaire").map((m) => m.tick), [5]);
    assert.equal(restante.fermeture, null);
  });

  test("l'hôte part : pas remplacé, il revient à sa place", async () => {
    const { hote, invite, code } = await salonADeux();
    const jeton = hote.jeton;
    await hote.fermer();
    assert.equal((await invite.attendreType("joueur-parti")).joueur, 0);
    const retour = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    assert.equal((await retour.bienvenue()).joueur, 0);
    assert.equal((await invite.attendreType("joueur-revenu")).joueur, 0);
    // L'hôte revenu peut de nouveau envoyer la sauvegarde.
    for (const m of morceauxSauvegarde(new Uint8Array(5), 10)) retour.envoyerBrut(m);
    memes((await recevoirSauvegarde(invite)).sauvegarde, new Uint8Array(5));
  });

  test("expulsion par l'hôte : la place est libérée et reprise", async () => {
    const { hote, invite, code } = await salonADeux();
    // Seul l'hôte peut expulser, et pas lui-même.
    invite.expulser(1);
    assert.equal((await invite.attendreType("erreur")).code, "reserve-a-l-hote");
    hote.expulser(0);
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.expulser(3);
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");

    // Joueur déconnecté qui garde la place : l'hôte la libère.
    const ancien = invite.jeton;
    await invite.fermer();
    await hote.attendreType("joueur-parti");
    hote.expulser(1);
    const ex = await hote.attendreType("joueur-expulse");
    assert.equal(ex.joueur, 1);
    assert.deepEqual(ex.joueurs, [{ joueur: 0, connecte: true }]);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { jeton: ancien })), "jeton-invalide", 4003);

    // Un ami prend la place libérée ; expulsé alors qu'il est connecté, il est fermé en 4006.
    const ami = client(ClientRelais.rejoindre(relais.base, code));
    const b = await ami.bienvenue();
    assert.equal(b.joueur, 1);
    assert.notEqual(b.jeton, ancien);
    await hote.attendreType("joueur-arrive");
    hote.expulser(1);
    await refusAttendu(ami, "expulse", 4006);
    assert.equal((await hote.attendreType("joueur-expulse")).joueur, 1);
    const [avant] = await barriere(hote, hote);
    assert.deepEqual(avant, [], "pas de joueur-parti après une expulsion");
  });

  test("salon occupé : l'alarme est reposée, il reste ouvert", async () => {
    const { hote, invite, code } = await salonADeux();
    await attendreMs(2500); // plus de deux échéances de 1 s
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code)), "salon-plein", 4002);
    hote.envoyer(TYPE.TOUR, 3);
    assert.equal((await invite.attendreType("binaire")).tick, 3);
  });

  test("salon fermé quand tout le monde est parti depuis le délai", async () => {
    const { hote, invite, code } = await salonADeux();
    const jeton = hote.jeton;
    await invite.fermer();
    await hote.fermer();
    // Retour avant le délai (1 s pour ce test) : le salon existe encore.
    const vite = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    await vite.bienvenue();
    await vite.fermer();
    // Sans jeton, on ne repousse pas l'alarme : « salon-plein » tant qu'il existe, puis inconnu.
    let code_ = "salon-plein";
    const fin = Date.now() + 15000;
    while (code_ === "salon-plein" && Date.now() < fin) {
      await attendreMs(300);
      const c = client(ClientRelais.rejoindre(relais.base, code));
      code_ = (await c.attendreType("erreur")).code;
    }
    assert.equal(code_, "salon-inconnu");
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { jeton })), "salon-inconnu", 4001);
  });
});
