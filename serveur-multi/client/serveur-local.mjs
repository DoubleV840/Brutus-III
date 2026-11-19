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
  const http = await worker.url;
  const base = new URL(http);
  base.protocol = base.protocol === "https:" ? "wss:" : "ws:";
  return { base: base.href, http, arreter: () => worker.dispose() };
}
