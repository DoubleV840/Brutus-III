// Brutus III : préambule de la variante wasm du rejouage sous Node (option CMake BRUTUS_REJOUAGE_NODE).
// - les fichiers de l'ordinateur sont visibles sous /hote (NODEFS), les assets restent embarqués dans /assets ;
// - pas d'affichage ni de son ;
// - pas de page : les appels du moteur à la page (syncFS, quitGame) ne font rien.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  FS.mkdir('/hote');
  FS.mount(NODEFS, { root: '/' }, '/hote');
  ENV.SDL_VIDEODRIVER = 'dummy';
  ENV.SDL_AUDIODRIVER = 'dummy';
});
Module.syncFS = function () {};
Module.quitGame = function () {};
