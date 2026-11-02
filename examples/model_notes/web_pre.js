// In the browser there is no disk to browse, so the build bundles PRISM's own sources under
// /PRISM (see meson.build) and this starts the app there: the tree roots at the current directory.
Module['preRun'] = (Module['preRun'] || []).concat(function () {
  FS.mkdirTree('/PRISM');
  FS.chdir('/PRISM');
});
