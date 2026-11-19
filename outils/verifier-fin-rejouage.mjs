// Brutus III : contrôles des fichiers d'empreintes écrits par --rejouer (voir outils/test-rejouage.sh).
//
//   node outils/verifier-fin-rejouage.mjs <empreintes.txt>
//     Vérifie la ligne « # fin » : toutes les commandes du journal (après le tick de départ)
//     appliquées, aucune restante, aucune perdue. Affiche le nombre de commandes refusées.
//
//   node outils/verifier-fin-rejouage.mjs --comparer <reference.txt> <reprise.txt> [libellé]
//     Vérifie que chaque empreinte de <reprise.txt> (rejouage repris depuis une sauvegarde, ou
//     rejouage avec interface ouverte) est identique à celle du même tick dans <reference.txt>.
import { readFileSync } from 'node:fs';

function champs(ligne) {
  const mots = ligne.replace(/^#\s*/, '').trim().split(/\s+/);
  const resultat = {};
  for (let i = 0; i + 1 < mots.length; i += 2) {
    resultat[mots[i]] = Number(mots[i + 1]);
  }
  return resultat;
}

function lire(chemin) {
  const lignes = readFileSync(chemin, 'utf8').split('\n').filter((l) => l.length);
  const entete = lignes.find((l) => l.startsWith('# tick_depart'));
  const fin = lignes.find((l) => l.startsWith('# fin'));
  const empreintes = new Map();
  for (const l of lignes) {
    if (l.startsWith('#')) continue;
    const [tick, valeur] = l.split(' ');
    empreintes.set(tick, valeur);
  }
  return { entete: entete && champs(entete), fin: fin && champs(fin.replace(/^# fin/, '')), empreintes };
}

function echec(message) {
  console.log(`ÉCHEC : ${message}`);
  process.exit(1);
}

const args = process.argv.slice(2);
if (args[0] === '--comparer') {
  const reference = lire(args[1]);
  const reprise = lire(args[2]);
  const libelle = args[3] || 'rechargement';
  if (!reprise.empreintes.size) echec(`aucune empreinte dans ${args[2]}`);
  let communes = 0;
  for (const [tick, valeur] of reprise.empreintes) {
    if (!reference.empreintes.has(tick)) echec(`tick ${tick} absent de la référence ${args[1]}`);
    if (reference.empreintes.get(tick) !== valeur) {
      echec(`${libelle} : empreinte différente au tick ${tick} (${valeur} au lieu de ${reference.empreintes.get(tick)})`);
    }
    communes++;
  }
  console.log(`OK : ${libelle} identique au rejouage de référence (${communes} empreintes comparées).`);
  process.exit(0);
}

const fichier = lire(args[0]);
if (!fichier.entete || !fichier.fin) echec(`ligne « # tick_depart » ou « # fin » absente de ${args[0]}`);
const attendues = fichier.entete.commandes_journal - (fichier.entete.commandes_avant_depart || 0);
const { commandes_appliquees: appliquees, commandes_restantes: restantes, commandes_perdues: perdues } = fichier.fin;
if (appliquees !== attendues) echec(`${appliquees} commandes appliquées sur ${attendues} attendues (${args[0]})`);
if (restantes !== 0) echec(`${restantes} commandes du journal non appliquées (${args[0]})`);
if (perdues) echec(`${perdues} commandes perdues (file pleine) (${args[0]})`);
console.log(`   commandes : ${appliquees}/${attendues} appliquées, dont ${fichier.fin.commandes_refusees} refusées par la validation`);
