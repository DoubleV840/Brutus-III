// Brutus III : réassemblage des sauvegardes (client de référence), sans relais ni port.
import { describe, test } from "node:test";
import assert from "node:assert/strict";

import {
  ENTETE_MORCEAU, MORCEAU_MAX, Reception, SAUVEGARDE_MAX, decoder, morceauxSauvegarde, nouvelIdentifiant,
} from "../client/client.mjs";

function sauvegardeDe(taille) {
  const s = new Uint8Array(taille);
  for (let i = 0; i < taille; i++) s[i] = (i * 13 + 5) & 0xff;
  return s;
}

/** Données (sans en-tête de 8 octets) des messages d'une sauvegarde. */
function donneesDe(sauvegarde, identifiant, taille) {
  return morceauxSauvegarde(sauvegarde, 0, identifiant, undefined, taille).map((m) => decoder(m).donnees);
}

/** Compare des octets sans laisser assert calculer le diff de deux gros tableaux. */
function memes(recu, attendu) {
  assert.ok(recu instanceof Uint8Array, "sauvegarde non reçue");
  assert.ok(Buffer.from(recu).equals(Buffer.from(attendu)), "sauvegarde différente");
}

/** Morceau fabriqué à la main. */
function morceau(id, total, pos, octets) {
  const d = new Uint8Array(ENTETE_MORCEAU + octets.length);
  const v = new DataView(d.buffer);
  v.setUint32(0, id, true);
  v.setUint32(4, total, true);
  v.setUint32(8, pos, true);
  d.set(octets, ENTETE_MORCEAU);
  return d;
}

describe("réassemblage des sauvegardes", () => {
  test("identifiant tiré au hasard, jamais nul", () => {
    const ids = new Set(Array.from({ length: 50 }, nouvelIdentifiant));
    assert.ok(ids.size > 45);
    assert.ok(!ids.has(0));
  });

  test("3 morceaux, puis un second transfert avec le même identifiant", () => {
    const r = new Reception();
    const s1 = sauvegardeDe(150000);
    const d1 = donneesDe(s1, 7);
    assert.equal(d1.length, 3);
    assert.equal(r.ajouter(d1[0]), null);
    assert.equal(r.ajouter(d1[1]), null);
    memes(r.ajouter(d1[2]), s1);
    // État remis à zéro : un nouvel envoi (même identifiant, hôte redémarré) repart proprement.
    const s2 = sauvegardeDe(70000).reverse();
    const d2 = donneesDe(s2, 7);
    assert.equal(r.ajouter(d2[0]), null);
    memes(r.ajouter(d2[1]), s2);
    assert.equal(r.erreur, null);
  });

  test("morceau en double : transfert abandonné, pas de sauvegarde tronquée", () => {
    const r = new Reception();
    const d = donneesDe(sauvegardeDe(200000), 9);
    assert.equal(d.length, 4);
    assert.equal(r.ajouter(d[0]), null);
    assert.equal(r.ajouter(d[1]), null);
    // Avant : la somme des tailles atteignait le total avec ce doublon, sauvegarde fausse.
    assert.equal(r.ajouter(d[1]), null);
    assert.equal(r.erreur, "morceau hors séquence (doublon ou trou)");
    // La suite est ignorée tant qu'un nouveau transfert ne commence pas.
    assert.equal(r.ajouter(d[2]), null);
    assert.equal(r.ajouter(d[3]), null);
    // Un trou (morceau sauté) est refusé de la même façon.
    r.ajouter(d[0]);
    assert.equal(r.ajouter(d[2]), null);
    assert.equal(r.erreur, "morceau hors séquence (doublon ou trou)");
  });

  test("arrivée en cours de transfert : la suite est ignorée, le transfert suivant passe", () => {
    const r = new Reception();
    const d = donneesDe(sauvegardeDe(130000), 11);
    assert.equal(r.ajouter(d[1]), null);
    assert.match(r.erreur, /début du transfert manquant/);
    assert.equal(r.ajouter(d[2]), null);
    const s = sauvegardeDe(1000);
    memes(r.ajouter(donneesDe(s, 12)[0]), s);
  });

  test("un nouveau transfert remplace celui en cours", () => {
    const r = new Reception();
    const a = donneesDe(sauvegardeDe(130000), 1);
    const s = sauvegardeDe(90000);
    const b = donneesDe(s, 2);
    r.ajouter(a[0]);
    r.ajouter(b[0]);
    assert.equal(r.ajouter(a[1]), null); // ancien transfert : ignoré
    memes(r.ajouter(b[1]), s);
  });

  test("tailles absurdes refusées sans allouer ni lever d'exception", () => {
    const r = new Reception();
    assert.equal(r.ajouter(morceau(5, 0xffffffff, 0, new Uint8Array(10))), null);
    assert.equal(r.erreur, "sauvegarde trop grosse");
    assert.equal(r.ajouter(morceau(5, 100, 0, new Uint8Array(150))), null);
    assert.equal(r.erreur, "morceau hors bornes");
    assert.equal(r.ajouter(new Uint8Array(5)), null);
    assert.equal(r.erreur, "morceau trop court");
    assert.equal(r.ajouter(morceau(0, 10, 0, new Uint8Array(10))), null);
    assert.equal(r.erreur, "identifiant nul");
    r.ajouter(morceau(6, 100, 0, new Uint8Array(40)));
    assert.equal(r.ajouter(morceau(6, 100, 40, new Uint8Array(0))), null);
    assert.equal(r.erreur, "morceau vide");
    // La limite exacte passe.
    assert.equal(r.ajouter(morceau(8, SAUVEGARDE_MAX, 0, new Uint8Array(10))), null);
    assert.equal(r.erreur, null);
  });

  test("sauvegarde vide, découpage borné", () => {
    const r = new Reception();
    const d = donneesDe(new Uint8Array(0), 3);
    assert.equal(d.length, 1);
    assert.deepEqual(r.ajouter(d[0]), new Uint8Array(0));
    assert.throws(() => morceauxSauvegarde(new Uint8Array(SAUVEGARDE_MAX + 1), 0));
    assert.throws(() => morceauxSauvegarde(new Uint8Array(10), 0, 1, undefined, MORCEAU_MAX + 1));
  });
});
