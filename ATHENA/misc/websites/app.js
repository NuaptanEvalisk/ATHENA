
window.onload=function(){
  initExplorers();
  initGlobalSearch();
  initQuickSwitcher();
  installWindow('vault');
  installWindow('namespaces');
  installWindow('global-search');
  installWindow('quick-switcher');
  installWindow('viewer');
  byId('doc-back').onclick=athenaDocBack;
  byId('doc-forward').onclick=athenaDocForward;
  var state=athenaLoadState();
  if(!athenaApplySavedState(state)) athenaDefaultLayout();
  athenaOpenInitialDoc((state && state.currentDoc) ||
    ((window.ATHENA_SITE_DATA && window.ATHENA_SITE_DATA.entry) || 'about:blank'),
    state);
  athenaBooting=false;
  athenaSaveState();
};
