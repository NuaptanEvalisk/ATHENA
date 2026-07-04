
window.onload=function(){
  initExplorers();
  initOutline();
  initGlobalSearch();
  initQuickSwitcher();
  installWindow('vault');
  installWindow('namespaces');
  installWindow('outline');
  installWindow('global-search');
  installWindow('quick-switcher');
  installWindow('viewer');
  byId('doc-back').onclick=athenaDocBack;
  byId('doc-forward').onclick=athenaDocForward;
  byId('doc-standalone').onclick=athenaOpenStandaloneDoc;
  var state=athenaLoadState();
  if(!athenaApplySavedState(state)) athenaDefaultLayout();
  athenaOpenInitialDoc((state && state.currentDoc) ||
    ((window.ATHENA_SITE_DATA && window.ATHENA_SITE_DATA.entry) || 'about:blank'),
    state);
  athenaBooting=false;
  athenaSaveState();
};
