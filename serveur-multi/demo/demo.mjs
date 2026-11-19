// Brutus III : démo du relais, deux joueurs qui échangent en local.
//
//   cd serveur-multi && npm run demo                          (démarre son propre relais local)
//   cd serveur-multi && npm run demo -- ws://127.0.0.1:8787   (relais déjà lancé par npm run dev)
//
// L'hôte crée un salon, l'invité le rejoint avec le code, l'hôte envoie sa sauvegarde
// (fausse, 100 Ko), puis 10 tours de lockstep : chacun envoie son paquet à chaque tour
// (vide s'il n'a rien fait), et un tour n'est « joué » qu'avec les paquets de tous.
import {
  ClientRelais, Reception, TYPE, morceauxSauvegarde,
} from "../client/client.mjs";

const argument = process.argv[2];
let relais = null;
let base = argument;
if (!base) {
  const { demarrerRelais } = await import("../client/serveur-local.mjs");
  relais = await demarrerRelais();
  base = relais.base;
}
console.log(`Relais : ${base}`);

const hote = ClientRelais.creer(base);
const bh = await hote.bienvenue();
console.log(`Hôte   : salon ${bh.code} créé, je suis le joueur ${bh.joueur}`);

const invite = ClientRelais.rejoindre(base, bh.code);
const bi = await invite.bienvenue();
console.log(`Invité : salon ${bi.code} rejoint, je suis le joueur ${bi.joueur}`);
await hote.attendreType("joueur-arrive");
console.log(`Hôte   : le joueur ${bi.joueur} est arrivé, j'envoie ma sauvegarde`);

const sauvegarde = new Uint8Array(100 * 1024).map((_, i) => i & 0xff);
for (const m of morceauxSauvegarde(sauvegarde, 0)) hote.envoyerBrut(m);
const reception = new Reception();
let recue = null;
while (!recue) recue = reception.ajouter((await invite.attendreType("binaire")).donnees);
console.log(`Invité : sauvegarde reçue (${recue.length} octets)`);

// Lockstep : à chaque tour, chacun envoie ses commandes (une commande factice de temps en temps).
const commande = (texte) => new TextEncoder().encode(texte);
const TOURS = 10;
for (let tour = 0; tour < TOURS; tour++) {
  hote.envoyer(TYPE.TOUR, tour, tour === 3 ? commande("construire route") : undefined);
  invite.envoyer(TYPE.TOUR, tour, tour === 6 ? commande("impots +1") : undefined);
}
for (const [nom, c] of [["Hôte  ", hote], ["Invité", invite]]) {
  const paquets = new Map();
  for (let i = 0; i < 2 * TOURS; i++) {
    const m = await c.attendreType("binaire");
    if (!paquets.has(m.tick)) paquets.set(m.tick, []);
    paquets.get(m.tick).push(m);
  }
  const lignes = [];
  for (let tour = 0; tour < TOURS; tour++) {
    const p = paquets.get(tour);
    if (p.length < 2) throw new Error(`tour ${tour} incomplet`);
    const faits = p.filter((m) => m.donnees.length).map((m) => `j${m.joueur} « ${new TextDecoder().decode(m.donnees)} »`);
    lignes.push(`${tour}${faits.length ? " " + faits.join(", ") : ""}`);
  }
  console.log(`${nom} : tours joués ${lignes.join(" | ")}`);
}

await invite.fermer();
const parti = await hote.attendreType("joueur-parti");
console.log(`Hôte   : le joueur ${parti.joueur} est parti (la partie attendrait ici)`);
const retour = ClientRelais.rejoindre(base, bh.code, { jeton: invite.jeton });
const br = await retour.bienvenue();
console.log(`Invité : reconnecté avec mon jeton, toujours joueur ${br.joueur}`);
await hote.attendreType("joueur-revenu");

await retour.fermer();
await hote.fermer();
await relais?.arreter();
console.log("Démo terminée.");
