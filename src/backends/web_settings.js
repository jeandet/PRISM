// Linked into every PRISM web page (--pre-js, src/meson.build): keeps what the app writes under
// /libsdl -- where SDL_GetPrefPath puts settings in the browser -- in the page's IndexedDB, so
// settings survive a reload. Before main starts, the stored files are loaded; after that, every
// file written there is saved back (IDBFS autoPersist). Without IndexedDB (node, a private
// window that refuses it) the folder stays in memory, as before.
Module['preRun'] = [].concat(Module['preRun'] || [], function () {
  if (typeof indexedDB === 'undefined') return;
  FS.mkdirTree('/libsdl');
  FS.mount(IDBFS, { autoPersist: true }, '/libsdl');
  addRunDependency('prism-settings');
  FS.syncfs(true, function (err) {
    if (err) console.warn('PRISM: stored settings could not be read:', err);
    removeRunDependency('prism-settings');
  });
});
