#!/usr/bin/env node
// Brutus III : bot de test reproductible (phase 2b).
//
// Fabrique un journal de commandes (format de docs/commandes.md) à partir de la carte texte
// écrite par le moteur (`--info-carte`). Même graine + même carte = même journal.
//
//   node outils/bot-rejouage.mjs <carte.txt> <journal.txt> [--graine 42] [--ticks 150000]
//
// Le bot ne voit pas la partie : il tient une carte d'occupation approximative et choisit
// des actions plausibles (routes, maisons, services, démolitions, impôts, main-d'œuvre,
// ordres d'entrepôt et de grenier…). Une partie des commandes échoue dans le jeu (terrain,
// argent) : c'est voulu, l'échec doit lui aussi être déterministe.

import { readFileSync, writeFileSync } from 'node:fs';

const args = process.argv.slice(2);
if (args.length < 2) {
  console.error('usage : node outils/bot-rejouage.mjs <carte.txt> <journal.txt> [--graine N] [--ticks N]');
  process.exit(2);
}
const [cheminCarte, cheminJournal] = args;
const option = (nom, defaut) => {
  const i = args.indexOf(nom);
  return i >= 0 && i + 1 < args.length ? Number(args[i + 1]) : defaut;
};
const GRAINE = option('--graine', 42);
const TICKS = option('--ticks', 150000);

// --- Aléatoire reproductible (mulberry32) ---
let etatAlea = GRAINE >>> 0;
function alea() {
  etatAlea = (etatAlea + 0x6d2b79f5) >>> 0;
  let t = etatAlea;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const entier = (min, max) => min + Math.floor(alea() * (max - min + 1)); // bornes incluses
const choisir = (liste) => liste[Math.floor(alea() * liste.length)];

// --- Carte ---
const lignes = readFileSync(cheminCarte, 'utf8').split('\n');
const info = {};
let rangees = [];
for (let i = 0; i < lignes.length; i++) {
  if (lignes[i] === 'carte') {
    rangees = lignes.slice(i + 1).filter((l) => l.length > 0);
    break;
  }
  const [cle, ...valeurs] = lignes[i].split(' ');
  if (cle) info[cle] = valeurs.map(Number);
}
const L = info.largeur[0];
const H = info.hauteur[0];
const TICK0 = info.tick[0];
const [ex, ey] = info.entree;
if (rangees.length !== H) {
  console.error(`carte illisible : ${rangees.length} rangées au lieu de ${H}`);
  process.exit(2);
}
const sol = rangees.map((r) => r.split(''));
const occupe = sol.map((r) => r.map((c) => c !== '.' && c !== 'm'));
const dedans = (x, y) => x >= 1 && y >= 1 && x < L - 1 && y < H - 1;
const libre = (x, y, taille) => {
  for (let j = 0; j < taille; j++) {
    for (let i = 0; i < taille; i++) {
      if (!dedans(x + i, y + j) || occupe[y + j][x + i]) return false;
    }
  }
  return true;
};
const marquer = (x0, y0, x1, y1) => {
  for (let y = Math.min(y0, y1); y <= Math.max(y0, y1); y++) {
    for (let x = Math.min(x0, x1); x <= Math.max(x0, x1); x++) {
      if (dedans(x, y)) occupe[y][x] = true;
    }
  }
};

// Centre de la ville : la zone 25 x 25 la plus dégagée, parmi des points tirés au hasard,
// en préférant ceux proches de l'entrée.
function compterLibres(cx, cy, r) {
  let n = 0;
  for (let y = cy - r; y <= cy + r; y++) {
    for (let x = cx - r; x <= cx + r; x++) {
      if (dedans(x, y) && (sol[y][x] === '.' || sol[y][x] === 'm')) n++;
    }
  }
  return n;
}
let centre = [Math.floor(L / 2), Math.floor(H / 2)];
let meilleur = -Infinity;
for (let essai = 0; essai < 400; essai++) {
  const cx = entier(14, L - 15);
  const cy = entier(14, H - 15);
  const score = compterLibres(cx, cy, 12) - 0.5 * Math.hypot(cx - ex, cy - ey);
  if (score > meilleur) {
    meilleur = score;
    centre = [cx, cy];
  }
}
const [CX, CY] = centre;

// Route existante la plus proche du centre (souvent la route entrée-sortie de la carte).
let routeProche = [ex, ey];
let distRoute = Infinity;
for (let y = 0; y < H; y++) {
  for (let x = 0; x < L; x++) {
    if (sol[y][x] === 'r') {
      const d = Math.hypot(x - CX, y - CY);
      if (d < distRoute) {
        distRoute = d;
        routeProche = [x, y];
      }
    }
  }
}

// --- Journal ---
const B = {
  ROUTE: 5, MUR: 6, AQUEDUC: 8, DEGAGER: 9, MAISON: 10, THEATRE: 31, ACTEURS: 36, PLACE: 38, JARDINS: 39,
  MEDECIN: 46, BAINS: 48, BARBIER: 49, ECOLE: 51, PREFECTURE: 55, TOUR: 59, TEMPLE_CERES: 60,
  MARCHE: 70, GRENIER: 71, ENTREPOT: 72, INGENIEURS: 81, FORUM: 86, RESERVOIR: 90, FONTAINE: 91,
  PUITS: 92, BLE: 100, LEGUMES: 101, BOIS: 108, ARGILE: 109, POTERIE: 114,
};
const TAILLE = {
  [B.THEATRE]: 2, [B.ACTEURS]: 3, [B.MEDECIN]: 1, [B.BAINS]: 2, [B.BARBIER]: 1, [B.ECOLE]: 2,
  [B.PREFECTURE]: 1, [B.TEMPLE_CERES]: 2, [B.MARCHE]: 2, [B.GRENIER]: 3, [B.ENTREPOT]: 3,
  [B.INGENIEURS]: 1, [B.FORUM]: 2, [B.RESERVOIR]: 3, [B.FONTAINE]: 1, [B.PUITS]: 1, [B.BLE]: 3,
  [B.LEGUMES]: 3, [B.BOIS]: 2, [B.ARGILE]: 2, [B.POTERIE]: 2,
};

const commandes = [];
let orientation = 0;
const compteur = {};
function ajouter(t, nom, ...params) {
  commandes.push({ t: TICK0 + t, ligne: `${TICK0 + t} 0 ${nom} ${params.join(' ')}`.trimEnd() });
  compteur[nom] = (compteur[nom] || 0) + 1;
}
const construire = (t, type, x0, y0, x1 = x0, y1 = y0) =>
  ajouter(t, 'construire', type, x0, y0, x1, y1, 0, 0, 1, orientation);

const stockages = []; // [x, y, est_entrepot]
const batiments = []; // [x, y] des bâtiments posés (cibles de mise en sommeil, démolition)
const lignesRoutes = [];

function poserBatiment(t, type, rayon) {
  const taille = TAILLE[type] || 1;
  for (let essai = 0; essai < 60; essai++) {
    const x = entier(CX - rayon, CX + rayon);
    const y = entier(CY - rayon, CY + rayon);
    if (!libre(x, y, taille)) continue;
    if (type === B.BLE || type === B.LEGUMES) {
      if (sol[y][x] !== 'm' && alea() < 0.7) continue; // les fermes veulent de la prairie
    }
    construire(t, type, x, y);
    marquer(x, y, x + taille - 1, y + taille - 1);
    batiments.push([x, y]);
    if (type === B.ENTREPOT || type === B.GRENIER) stockages.push([x, y, type === B.ENTREPOT]);
    return true;
  }
  return false;
}

// 1) Relier la ville à la route existante, puis tracer un quadrillage.
let t = 5;
construire(t, B.ROUTE, routeProche[0], routeProche[1], CX, CY);
marquer(CX, CY, CX, CY);
for (const dy of [-10, -5, 0, 5, 10]) {
  t += entier(40, 120);
  const y = CY + dy;
  construire(t, B.ROUTE, CX - 12, y, CX + 12, y);
  lignesRoutes.push(['h', y]);
  marquer(CX - 12, y, CX + 12, y);
}
for (const dx of [-12, 0, 12]) {
  t += entier(40, 120);
  const x = CX + dx;
  construire(t, B.ROUTE, x, CY - 10, x, CY + 10);
  lignesRoutes.push(['v', x]);
  marquer(x, CY - 10, x, CY + 10);
}

// 2) Premières maisons et services de base.
for (const dy of [-10, -5, 0, 5]) {
  t += entier(30, 90);
  const y = CY + dy + 1;
  construire(t, B.MAISON, CX - 11, y, CX - 1, y + 1);
  marquer(CX - 11, y, CX - 1, y + 1);
}
for (const type of [B.PUITS, B.PUITS, B.PREFECTURE, B.INGENIEURS, B.MARCHE, B.GRENIER, B.BLE, B.BLE,
  B.ENTREPOT, B.TEMPLE_CERES]) {
  t += entier(30, 90);
  poserBatiment(t, type, 13);
}

// 3) Le reste de la partie : actions au hasard, pondérées.
const services = [B.PUITS, B.PUITS, B.FONTAINE, B.PREFECTURE, B.INGENIEURS, B.MARCHE, B.TEMPLE_CERES,
  B.ECOLE, B.MEDECIN, B.BARBIER, B.BAINS, B.THEATRE, B.ACTEURS, B.GRENIER, B.ENTREPOT, B.BLE, B.LEGUMES,
  B.BOIS, B.ARGILE, B.POTERIE, B.FORUM, B.JARDINS];
const actions = [
  [22, 'maisons'], [20, 'service'], [6, 'route'], [6, 'demolition'], [6, 'impots'], [3, 'salaires'],
  [6, 'priorite'], [12, 'stockage'], [2, 'sommeil'], [1, 'fete'], [1, 'aqueduc'], [1, 'mur'],
  [2, 'distribution'], [1, 'annuler'], [1, 'legion'], [4, 'divers'],
];
const totalPoids = actions.reduce((s, [p]) => s + p, 0);
function tirerAction() {
  let r = alea() * totalPoids;
  for (const [p, nom] of actions) {
    if ((r -= p) < 0) return nom;
  }
  return actions[0][1];
}
let rotationsFaites = 0;
let rayon = 13;
while (t < TICKS - 50) {
  t += entier(60, 260);
  if (t >= TICKS - 50) break;
  if (rayon < 22 && alea() < 0.01) rayon++;
  // Deux rotations de la vue au cours de la partie (elles touchent la simulation).
  if (rotationsFaites === 0 && t > TICKS * 0.4) {
    ajouter(t, 'rotation-vue', 0);
    orientation = (orientation + 2) % 8;
    rotationsFaites++;
    continue;
  }
  if (rotationsFaites === 1 && t > TICKS * 0.6) {
    ajouter(t, 'rotation-vue', 2);
    orientation = 0;
    rotationsFaites++;
    continue;
  }
  switch (tirerAction()) {
    case 'maisons': {
      // Le plus souvent le long d'une route du quadrillage (les maisons ont besoin d'une route).
      const l = entier(2, 6);
      let x;
      let y;
      if (alea() < 0.75) {
        const [sens, pos] = choisir(lignesRoutes);
        if (sens === 'h') {
          x = entier(CX - 11, CX + 11 - l);
          y = alea() < 0.5 ? pos + 1 : pos - 2;
        } else {
          x = alea() < 0.5 ? pos + 1 : pos - 2;
          y = entier(CY - 9, CY + 9 - l);
          construire(t, B.MAISON, x, y, x + 1, y + l);
          marquer(x, y, x + 1, y + l);
          break;
        }
      } else {
        x = entier(CX - rayon, CX + rayon - 4);
        y = entier(CY - rayon, CY + rayon - 2);
      }
      construire(t, B.MAISON, x, y, x + l, y + 1);
      marquer(x, y, x + l, y + 1);
      break;
    }
    case 'service':
      poserBatiment(t, choisir(services), rayon);
      break;
    case 'route': {
      const x0 = entier(CX - rayon, CX + rayon);
      const y0 = entier(CY - rayon, CY + rayon);
      const horizontal = alea() < 0.5;
      const l = entier(3, 10);
      const [x1, y1] = horizontal ? [x0 + l, y0] : [x0, y0 + l];
      construire(t, B.ROUTE, x0, y0, x1, y1);
      marquer(x0, y0, x1, y1);
      break;
    }
    case 'demolition': {
      if (batiments.length && alea() < 0.5) {
        const [x, y] = batiments.splice(entier(0, batiments.length - 1), 1)[0];
        construire(t, B.DEGAGER, x, y);
      } else {
        const x = entier(CX - rayon, CX + rayon);
        const y = entier(CY - rayon, CY + rayon);
        construire(t, B.DEGAGER, x, y, x + entier(0, 2), y + entier(0, 2));
      }
      break;
    }
    case 'impots':
      ajouter(t, 'impots', alea() < 0.5 ? 1 : -1);
      break;
    case 'salaires':
      ajouter(t, 'salaires', alea() < 0.5 ? 1 : -1);
      break;
    case 'priorite':
      ajouter(t, 'priorite-travail', entier(0, 8), entier(0, 3));
      break;
    case 'stockage': {
      if (!stockages.length) break;
      const [x, y, entrepot] = choisir(stockages);
      const ressource = entrepot ? entier(1, 20) : entier(1, 5);
      const r = alea();
      if (r < 0.55) {
        ajouter(t, 'stockage-etat', x, y, ressource, alea() < 0.3 ? 1 : 0, alea() < 0.25 ? 1 : 0);
      } else if (r < 0.7) {
        ajouter(t, 'stockage-tout', x, y, alea() < 0.6 ? 1 : 0);
      } else if (r < 0.9) {
        ajouter(t, 'stockage-permission', x, y, entier(0, 4), alea() < 0.5 ? -1 : entier(0, 1));
      } else {
        ajouter(t, 'stockage-vider', x, y);
      }
      break;
    }
    case 'sommeil':
      if (batiments.length) {
        const [x, y] = choisir(batiments);
        ajouter(t, 'mise-en-sommeil', x, y);
      }
      break;
    case 'fete':
      ajouter(t, 'fete', entier(0, 4), entier(1, 2));
      break;
    case 'aqueduc': {
      if (alea() < 0.4) {
        poserBatiment(t, B.RESERVOIR, rayon);
      } else {
        const x0 = entier(CX - rayon, CX + rayon);
        const y0 = entier(CY - rayon, CY + rayon);
        construire(t, B.AQUEDUC, x0, y0, x0 + entier(-8, 8), y0 + entier(-8, 8));
      }
      break;
    }
    case 'mur': {
      const x0 = entier(CX - rayon - 4, CX + rayon + 4);
      const y0 = entier(CY - rayon - 4, CY + rayon + 4);
      construire(t, B.MUR, x0, y0, x0 + entier(0, 8), y0);
      break;
    }
    case 'distribution':
      if (batiments.length) {
        const [x, y] = choisir(batiments);
        ajouter(t, 'distribution-bien', x, y, entier(1, 5));
      }
      break;
    case 'annuler':
      ajouter(t, 'annuler');
      break;
    case 'legion':
      ajouter(t, 'legion-rentrer', 0);
      break;
    case 'divers': {
      // Commandes rares : commerce, César, pari, réparation, légion. Beaucoup sont refusées
      // (pas d'épargne, pas de légion, pas de demande en cours) : la validation est exercée aussi.
      const r = entier(0, 10);
      if (r === 0) ajouter(t, 'commerce-statut', entier(1, 20), entier(1, 2));
      else if (r === 1) ajouter(t, 'commerce-seuil', entier(1, 20), entier(0, 1), alea() < 0.5 ? 1 : -1);
      else if (r === 2) ajouter(t, 'ressource-reserve', entier(1, 20));
      else if (r === 3) ajouter(t, 'ressource-sommeil', entier(1, 20));
      else if (r === 4) ajouter(t, 'salaire-gouverneur', entier(0, 4));
      else if (r === 5) ajouter(t, 'don-cesar', entier(0, 2));
      else if (r === 6) ajouter(t, 'pari-course', entier(1, 4), 10 * entier(1, 5));
      else if (r === 7) ajouter(t, 'demande-cesar', entier(0, 3));
      else if (r === 8) ajouter(t, 'legion-formation', entier(1, 3), entier(0, 6));
      else if (r === 9) {
        const x = entier(CX - rayon, CX + rayon);
        const y = entier(CY - rayon, CY + rayon);
        ajouter(t, 'reparer-confirme', x, y, x + entier(0, 3), y + entier(0, 3));
      } else if (batiments.length) {
        const [x, y] = choisir(batiments);
        ajouter(t, 'distribution-tout', x, y, entier(0, 1));
      }
      break;
    }
  }
}

commandes.sort((a, b) => a.t - b.t); // déjà croissant ; tri stable par sécurité
const entete = [
  '# brutus-journal 1',
  `# bot outils/bot-rejouage.mjs graine ${GRAINE} ticks ${TICKS} carte ${L}x${H} centre ${CX} ${CY}`,
  `# tick de départ ${TICK0} ; une ligne = <tick> <joueur> <commande> <paramètres...>`,
];
writeFileSync(cheminJournal, entete.concat(commandes.map((c) => c.ligne)).join('\n') + '\n');
const resume = Object.entries(compteur).sort().map(([k, v]) => `${k}=${v}`).join(' ');
console.log(`bot : ${commandes.length} commandes sur ${TICKS} ticks (graine ${GRAINE}) : ${resume}`);
