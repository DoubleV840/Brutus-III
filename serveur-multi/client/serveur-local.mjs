// Brutus III : démarre le relais en local (workerd, comme `wrangler dev`) pour les tests et la démo.
// Aucun compte Cloudflare : tout tourne sur la machine, sur un port libre de 127.0.0.1.
import path from "node:path";

const racine = path.resolve(import.meta.dirname, "..");

// Wrangler écrit ses journaux et sa config globale dans le dossier de l'utilisateur ; on les
// garde dans serveur-multi/.wrangler/ (ignoré par git), sans télémétrie.
process.env.XDG_CONFIG_HOME ??= path.join(racine, ".wrangler", "config");
process.env.WRANGLER_LOG_PATH ??= path.join(racine, ".wrangler", "logs");
process.env.WRANGLER_SEND_METRICS ??= "false";

/**
 * Démarre le relais. `vars` remplace des variables de wrangler.jsonc (ex. DELAI_FERMETURE_S).
 * Retourne { base: "ws://127.0.0.1:port", http: URL, arreter() }.
 */
export async function demarrerRelais(vars = {}) {
  const { unstable_startWorker } = await import("wrangler");
  const bindings = {};
  for (const [nom, valeur] of Object.entries(vars)) {
    bindings[nom] = { type: "plain_text", value: String(valeur) };
  }
  const worker = await unstable_startWorker({
    config: path.join(racine, "wrangler.jsonc"),
    bindings,
    dev: {
      server: { hostname: "127.0.0.1", port: 0 },
      inspector: false,
      persist: false,
      watch: false,
      logLevel: "warn",
    },
  });
  // Si workerd ne peut pas ouvrir son port (bac à sable : « listen EPERM »), wrangler ne
  // lève pas d'erreur et attend indéfiniment : on abandonne avec un message clair.
  let minuteur;
  const delai = new Promise((_, rejeter) => {
    minuteur = setTimeout(() => rejeter(new Error(
      "Le relais local n'a pas démarré en 30 s. Voir le dernier journal dans " +
      path.join(racine, ".wrangler", "logs") + " : « listen EPERM » = port local refusé (bac à sable) ; " +
      "« not of type ExportedHandler » = export nommé dans src/index.js.")), 30000);
  });
  let http;
  try {
    http = await Promise.race([worker.url, delai]);
  } catch (e) {
    worker.dispose().catch(() => {});
    throw e;
  } finally {
    clearTimeout(minuteur);
  }
  const base = new URL(http);
  base.protocol = base.protocol === "https:" ? "wss:" : "ws:";
  return { base: base.href, http, arreter: () => worker.dispose() };
}
