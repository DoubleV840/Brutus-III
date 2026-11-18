// Test sans navigateur de build-web/augustus.html : exécute la page avec un faux DOM
// et une IndexedDB simulée (fake-indexeddb). Vérifie IDBFS, la copie du dossier,
// la persistance des sauvegardes après « rechargement », le changement de dossier
// et la remise à zéro. Le jeu lui-même (callMain) n'est pas lancé.
//
//   bash outils/build-web.sh && cd web/test && npm install && npm test
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { indexedDB, IDBKeyRange } = require('fake-indexeddb');

// BRUTUS_PAGE : autre page à tester (la CI teste build-pages/index.html, la page publiée).
const html = fs.readFileSync(process.env.BRUTUS_PAGE || path.join(__dirname, '..', '..', 'build-web', 'augustus.html'), 'utf8');
const scripts = [];
const re = /<script[^>]*>([\s\S]*?)<\/script>/g;
let m;
while ((m = re.exec(html))) scripts.push(m[1]);

let failures = 0;
function check(cond, label) {
  console.log((cond ? 'OK     ' : 'ÉCHEC  ') + label);
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(fn, label, ms = 20000) {
  const t = Date.now();
  while (Date.now() - t < ms) {
    if (fn()) return true;
    await sleep(20);
  }
  check(false, 'délai dépassé : ' + label);
  return false;
}

function element(id) {
  const listeners = {};
  return {
    id, style: {}, innerHTML: '', value: '', webkitdirectory: true,
    classList: { add() {}, remove() {} },
    addEventListener(t, f) { listeners[t] = f; },
    removeEventListener() {},
    removeAttribute() {},
    click() {}, focus() {},
    getContext() { return null; },
    getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 600 }; },
    listeners,
  };
}

function loadPage(options = {}) {
  const elements = {};
  const errors = [];
  const document = {
    getElementById(id) { return (elements[id] = elements[id] || element(id)); },
    addEventListener() {}, removeEventListener() {},
    querySelector() { return null; },
    createElement(t) { return element(t); },
    currentScript: { src: 'http://localhost:8000/augustus.html' },
    visibilityState: 'visible',
  };
  const ctx = {
    document, navigator: { userAgent: 'node-test' }, location: { href: 'http://localhost:8000/augustus.html' },
    indexedDB: options.noIndexedDB ? undefined : indexedDB, IDBKeyRange, console, setTimeout, clearTimeout, setInterval, clearInterval,
    TextDecoder, TextEncoder, WebAssembly, performance, URL, Blob, fetch, atob, btoa, queueMicrotask,
    requestAnimationFrame: (f) => setTimeout(f, 16),
    FileReader: function FileReader() {},
    addEventListener() {}, removeEventListener() {},
  };
  ctx.window = ctx;
  ctx.self = ctx;
  ctx.globalThis = ctx;
  vm.createContext(ctx);
  for (const s of scripts) {
    try {
      vm.runInContext(s, ctx);
    } catch (e) {
      errors.push(e);
    }
  }
  const Module = vm.runInContext('Module', ctx);
  Module.calls = [];
  Module.callMain = (args) => Module.calls.push(args);
  ctx.onerror = null;
  return { ctx, Module, elements, errors, FS: () => vm.runInContext('FS', ctx) };
}

const fakeFile = (text) => ({ arrayBuffer: async () => new TextEncoder().encode(text).buffer });
const shown = (p, id) => p.elements[id] && p.elements[id].style.display === 'block';
const exists = (p, f) => { try { p.FS().stat(f); return true; } catch (e) { return false; } };

(async () => {
  // 1. Premier chargement : rien en stockage -> on demande le dossier.
  let p = loadPage();
  check(p.errors.length === 0, 'scripts exécutés sans erreur ' + p.errors.map(String).join(' | '));
  check(!!p.Module.IDBFS, 'Module.IDBFS exporté');
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran initial');
  check(!/IDBFS is not defined/.test(p.elements.status.innerHTML), 'pas de « IDBFS is not defined »');
  check(shown(p, 'chooseFolder'), '1er chargement : choix du dossier affiché');

  // 2. Copie d'un dossier (on choisit le parent « app » : la racine est trouvée via C3.sg2).
  p.Module.loadGameFiles(Promise.resolve([
    { path: 'app/jeu1/C3.sg2', file: fakeFile('sg2') },
    { path: 'app/jeu1/c3.eng', file: fakeFile('eng') },
    { path: 'app/jeu1/c3_model.txt', file: fakeFile('model') },
    { path: 'app/jeu1/Readme.txt', file: fakeFile('x') },
    { path: 'app/jeu1/c3.exe', file: fakeFile('x') },
    { path: 'app/jeu1/.DS_Store', file: fakeFile('x') },
    { path: 'app/jeu1/wavs/Rome.WAV', file: fakeFile('wav') },
    { path: 'app/autre.bin', file: fakeFile('x') },
  ]));
  await until(() => p.Module.calls.length === 1, 'lancement après copie');
  check(JSON.stringify(p.Module.calls[0]) === '["/C3"]', 'callMain(["/C3"]) appelé après la copie');
  p.Module.running = true;

  // 3. Le moteur écrit deux sauvegardes coup sur coup (2e pendant la synchro de la 1re).
  p.FS().writeFile('/C3/Rome.sav', 'partie1');
  p.Module.syncFS();
  p.FS().writeFile('/C3/Rome2.svx', 'partie2');
  p.Module.syncFS();
  check(p.Module.syncPending === true, 'synchro en attente mémorisée pendant une synchro');
  await until(() => !p.Module.syncingFS && !p.Module.syncPending, 'fin des synchros');

  // 4. Rechargement : le dossier n'est plus demandé, tout est retrouvé.
  p = loadPage();
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran après rechargement');
  check(shown(p, 'startOptions'), 'rechargement : bouton Jouer, pas de nouvelle demande de dossier');
  check(exists(p, '/C3/C3.sg2') && exists(p, '/C3/c3.eng'), 'fichiers du jeu retrouvés (casse conservée)');
  check(exists(p, '/C3/wavs/Rome.WAV'), 'sous-dossier retrouvé');
  check(exists(p, '/C3/c3_model.txt'), 'c3_model.txt copié');
  check(!exists(p, '/C3/c3.exe') && !exists(p, '/C3/Readme.txt') && !exists(p, '/C3/.DS_Store'), 'fichiers inutiles ignorés');
  check(!exists(p, '/C3/autre.bin') && !exists(p, '/C3/jeu1'), 'seule la racine du jeu est copiée');
  check(exists(p, '/C3/Rome.sav'), 'sauvegarde 1 retrouvée après rechargement');
  check(exists(p, '/C3/Rome2.svx'), 'sauvegarde 2 (écrite pendant une synchro) retrouvée');

  // 5. Changer de dossier : les sauvegardes restent, l'ancien jeu part.
  p.Module.loadGameFiles(Promise.resolve([
    { path: 'C3/c3.SG2', file: fakeFile('sg2-bis') },
  ]));
  await until(() => p.Module.calls.length === 1, 'lancement après changement de dossier');
  p = loadPage();
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran après changement');
  check(exists(p, '/C3/c3.SG2') && !exists(p, '/C3/C3.sg2') && !exists(p, '/C3/wavs'), 'changement de dossier : ancien jeu remplacé');
  check(exists(p, '/C3/Rome.sav') && exists(p, '/C3/Rome2.svx'), 'changement de dossier : sauvegardes gardées');

  // 5b. Échec d'enregistrement en jeu : le joueur est prévenu.
  p.Module.running = true;
  const realSyncfs = p.FS().syncfs;
  p.FS().syncfs = (populate, cb) => { p.FS().syncfs = realSyncfs; cb(new Error('quota dépassé')); };
  p.Module.syncFS();
  check(/Échec de l'enregistrement/.test(p.elements.OSDStatus.innerHTML) && p.elements.OSD.style.display === 'block',
    'échec d\'enregistrement affiché en jeu');
  p.Module.running = false;

  // 5c. Échec WebGL/SDL (code -2) : message, rien n'est effacé.
  p.Module.quitGame(-2);
  check(/WebGL/.test(p.elements.status.innerHTML), 'code -2 : message WebGL');
  await sleep(300);
  p = loadPage();
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran après code -2');
  check(exists(p, '/C3/c3.SG2') && exists(p, '/C3/Rome.sav'), 'code -2 : fichiers du jeu et sauvegardes gardés');

  // 5d. Erreur de stockage : la base n'est plus supprimée.
  p.Module.handleFSError(new Error('erreur simulée'));
  check(/pas été effacées/.test(p.elements.status.innerHTML), 'erreur de stockage : message clair');
  await sleep(300);
  p = loadPage();
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran après erreur de stockage');
  check(exists(p, '/C3/c3.SG2') && exists(p, '/C3/Rome.sav'), 'erreur de stockage : base IndexedDB gardée');

  // 5e. Données invalides (code 1) : le jeu est retiré, les sauvegardes restent.
  p.Module.quitGame(1);
  await until(() => shown(p, 'chooseFolder'), 'code 1 : dossier redemandé');
  check(!exists(p, '/C3/c3.SG2') && exists(p, '/C3/Rome.sav'), 'code 1 : jeu retiré, sauvegardes gardées');

  // 5f. Navigateur sans IndexedDB : message clair, pas d'« Erreur inattendue ».
  const noDb = loadPage({ noIndexedDB: true });
  await until(() => /stockage local|Erreur/.test(noDb.elements.status.innerHTML), 'message sans IndexedDB');
  check(noDb.errors.length === 0 && /stockage local/.test(noDb.elements.status.innerHTML),
    'sans IndexedDB : message « stockage local »');

  // 6. Dossier invalide : message, rien n'est effacé.
  p.Module.loadGameFiles(Promise.resolve([{ path: 'x/rien.txt', file: fakeFile('x') }]));
  await until(() => shown(p, 'chooseFolder'), 'message dossier invalide');
  check(/introuvable/.test(p.elements.chooseMessage.innerHTML), 'dossier sans C3.sg2 : message en français');

  // 7. Tout effacer.
  p.Module.clearFS();
  await until(() => /effacé/.test(p.elements.chooseMessage.innerHTML), 'effacement');
  p = loadPage();
  await until(() => shown(p, 'chooseFolder') || shown(p, 'startOptions'), 'écran après effacement');
  check(shown(p, 'chooseFolder') && !exists(p, '/C3/Rome.sav'), 'tout effacer : dossier redemandé, sauvegardes supprimées');

  console.log(failures === 0 ? '\nTOUT OK' : '\n' + failures + ' ÉCHEC(S)');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
