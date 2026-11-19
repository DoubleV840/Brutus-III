// Brutus III : tests du relais sur le vrai runtime (workerd, via l'API de wrangler).
// Lancer : cd serveur-multi && npm test
import { after, before, describe, test } from "node:test";
import assert from "node:assert/strict";

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

const attendreMs = (ms) => new Promise((r) => setTimeout(r, ms));

before(async () => {
  relais = await demarrerRelais({ DELAI_FERMETURE_S: 1 });
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
    // Empreinte, aux autres seulement.
    const e = new Uint8Array(8);
    new DataView(e.buffer).setBigUint64(0, 0x0123456789abcdefn, true);
    hote.envoyer(TYPE.EMPREINTE, 500, e, DEST_AUTRES);
    const me = await invite.attendreType("binaire");
    assert.equal(me.type, TYPE.EMPREINTE);
    assert.equal(new DataView(me.donnees.buffer, me.donnees.byteOffset).getBigUint64(0, true), 0x0123456789abcdefn);
    // Demande de sauvegarde, à l'hôte seulement ; chat et ping réservés mais relayés.
    invite.envoyer(TYPE.SAUVEGARDE_DEMANDE, 0, undefined, 0);
    invite.envoyer(TYPE.CHAT, 0, new TextEncoder().encode("salut"), DEST_TOUS);
    invite.envoyer(TYPE.PING, 0, new Uint8Array([9]), 1);
    const m1 = await hote.attendreType("binaire");
    assert.equal(m1.type, TYPE.SAUVEGARDE_DEMANDE);
    assert.equal(m1.joueur, 1);
    assert.equal((await hote.attendreType("binaire")).type, TYPE.CHAT);
    assert.equal((await invite.attendreType("binaire")).type, TYPE.CHAT);
    assert.equal((await invite.attendreType("binaire")).type, TYPE.PING);
    // L'hôte n'a reçu ni l'empreinte qu'il a envoyée aux autres, ni le ping de l'invité à lui-même.
    await attendreMs(200);
    assert.equal(hote.file.filter((m) => m.type === "binaire").length, 0);
    assert.equal(invite.file.filter((m) => m.type === "binaire").length, 0);
  });

  test("ping texte : pong automatique", async () => {
    const { hote } = await salonADeux();
    const pong = new Promise((r) => hote.ws.addEventListener("message", (e) => e.data === "pong" && r()));
    hote.ws.send("ping");
    await pong;
  });

  test("sauvegarde de 100 Ko découpée, réservée à l'hôte", async () => {
    const { hote, invite } = await salonADeux();
    const sauvegarde = new Uint8Array(100 * 1024);
    for (let i = 0; i < sauvegarde.length; i++) sauvegarde[i] = (i * 7 + 3) & 0xff;
    const morceaux = morceauxSauvegarde(sauvegarde, 4242, 1);
    assert.equal(morceaux.length, 2);
    for (const m of morceaux) hote.envoyerBrut(m);
    const reception = new Reception();
    let complete = null;
    while (!complete) {
      const m = await invite.attendreType("binaire");
      assert.equal(m.type, TYPE.SAUVEGARDE);
      assert.equal(m.tick, 4242);
      complete = reception.ajouter(m.donnees);
    }
    assert.deepEqual(complete, sauvegarde);

    // Un invité ne peut pas envoyer de sauvegarde (la connexion reste ouverte).
    invite.envoyerBrut(morceauxSauvegarde(new Uint8Array(10), 1, 2)[0]);
    assert.equal((await invite.attendreType("erreur")).code, "reserve-a-l-hote");
    invite.envoyer(TYPE.TOUR, 1);
    assert.equal((await hote.attendreType("binaire")).type, TYPE.TOUR);
  });

  test("message invalide signalé sans fermer", async () => {
    const { hote } = await salonADeux();
    hote.envoyerBrut(new Uint8Array([1, 0, 0]));
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.envoyerBrut(encoder(0x7e, 0));
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.envoyer(TYPE.TOUR, 0, undefined, 3); // joueur 3 : hors d'un salon à 2
    assert.equal((await hote.attendreType("erreur")).code, "message-invalide");
    hote.ws.send("bonjour");
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
    assert.equal(invite.file.filter((m) => m.type === "binaire").length, 0);
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

  test("code inconnu", async () => {
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "ZZZZZZ")), "salon-inconnu", 4001);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "ABC")), "salon-inconnu", 4001);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, "AB0O1I")), "salon-inconnu", 4001);
  });

  test("mauvaise version", async () => {
    await refusAttendu(client(ClientRelais.creer(relais.base, { version: 2 })), "version", 4000);
    const { code } = await salonADeux();
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { version: 0 })), "version", 4000);
  });

  test("origine", async () => {
    const ok = client(ClientRelais.creer(relais.base, { origine: "http://localhost:8000" }));
    await ok.bienvenue();
    const prod = client(ClientRelais.creer(relais.base, { origine: "https://brutus.ivwi.fr" }));
    await prod.bienvenue();
    const mauvais = client(ClientRelais.creer(relais.base, { origine: "https://exemple.com" }));
    await assert.rejects(mauvais.ouvert);
    const r = await fetch(new URL("/creer?version=1", relais.http), { headers: { Origin: "https://brutus.ivwi.fr.exemple.com" } });
    assert.equal(r.status, 403);
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

    // Reconnexion alors que l'ancienne connexion est encore ouverte : l'ancienne est fermée.
    const double = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    assert.equal((await double.bienvenue()).joueur, 1);
    await refusAttendu(retour, "remplace", 4005);
    assert.equal((await hote.attendreType("joueur-revenu")).joueur, 1);
    await attendreMs(200);
    assert.equal(hote.file.filter((m) => m.type === "joueur-parti").length, 0);
  });

  test("l'hôte part : pas remplacé, il revient à sa place", async () => {
    const { hote, invite, code } = await salonADeux();
    const jeton = hote.jeton;
    await hote.fermer();
    assert.equal((await invite.attendreType("joueur-parti")).joueur, 0);
    const retour = client(ClientRelais.rejoindre(relais.base, code, { jeton }));
    assert.equal((await retour.bienvenue()).joueur, 0);
    // L'hôte revenu peut de nouveau envoyer la sauvegarde.
    for (const m of morceauxSauvegarde(new Uint8Array(5), 10, 3)) retour.envoyerBrut(m);
    assert.equal((await invite.attendreType("binaire")).type, TYPE.SAUVEGARDE);
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
    await attendreMs(2500);
    await refusAttendu(client(ClientRelais.rejoindre(relais.base, code, { jeton })), "salon-inconnu", 4001);
  });
});
