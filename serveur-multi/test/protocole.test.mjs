// Brutus III : règles statiques du relais, sans runtime ni port.
import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

import * as client from "../client/client.mjs";
import * as protocole from "../src/protocole.js";

describe("protocole (sans port)", () => {
  test("src/index.js n'exporte que default et Salon (sinon workerd refuse le Worker)", async () => {
    const source = await readFile(new URL("../src/index.js", import.meta.url), "utf8");
    const exports = source.split("\n").filter((l) => /^\s*export\b/.test(l)).map((l) => l.trim());
    assert.equal(exports.length, 2, exports.join(" | "));
    assert.ok(exports.some((l) => l.startsWith("export default {")));
    assert.ok(exports.some((l) => l.startsWith("export class Salon ")));
  });

  test("client de référence et serveur ont les mêmes constantes", () => {
    assert.equal(client.VERSION_PROTOCOLE, protocole.VERSION_PROTOCOLE);
    assert.deepEqual(client.TYPE, protocole.TYPE);
    assert.equal(client.DEST_TOUS, protocole.DEST_TOUS);
    assert.equal(client.DEST_AUTRES, protocole.DEST_AUTRES);
    assert.equal(client.TAILLE_ENTETE, protocole.TAILLE_ENTETE);
    assert.equal(client.TAILLE_MAX_MESSAGE, protocole.TAILLE_MAX_MESSAGE);
  });

  test("origines", () => {
    for (const o of [null, "https://brutus.ivwi.fr", "http://localhost", "http://localhost:8000", "https://127.0.0.1:5173"]) {
      assert.ok(protocole.origineAcceptee(o), String(o));
    }
    for (const o of ["https://exemple.com", "https://brutus.ivwi.fr.exemple.com", "http://localhost.exemple.com",
      "http://brutus.ivwi.fr", "https://brutus.ivwi.fr:8443", "null"]) {
      assert.ok(!protocole.origineAcceptee(o), o);
    }
  });

  test("codes de salon", () => {
    assert.ok(protocole.codeValide("K7QXM3"));
    for (const c of ["", "ABC", "ABCDEFG", "AB0DEF", "ABODEF", "AB1DEF", "ABIDEF", "ABLDEF", "abcdef"]) {
      assert.ok(!protocole.codeValide(c), c);
    }
  });
});
