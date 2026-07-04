
var athenaTopZ=20;
var athenaDocHistory=[];
var athenaDocIndex=-1;
var athenaBooting=true;
var athenaRestoringState=false;
function byId(id){return document.getElementById(id);}
function athenaTaskId(id){return 'task-'+id;}
function athenaTaskFor(id){
  return byId(athenaTaskId(id));
}
function athenaManagedWindows(){
  return ['vault','namespaces','outline','global-search','quick-switcher','viewer'];
}
function athenaAuxiliaryWindows(){
  return ['vault','namespaces','outline','global-search','quick-switcher'];
}
function athenaStorageKey(){
  var data=window.ATHENA_SITE_DATA || {};
  return data.storageKey || ('athena-website:'+location.pathname);
}
function athenaLoadState(){
  try{
    var raw=localStorage.getItem(athenaStorageKey());
    return raw ? JSON.parse(raw) : null;
  }
  catch(e){return null;}
}
function athenaWindowState(win){
  return {
    left:win.style.left,
    top:win.style.top,
    width:win.style.width,
    height:win.style.height,
    zIndex:win.style.zIndex || '',
    closed:win.classList.contains('closed'),
    minimized:win.classList.contains('minimized'),
    maximized:win.classList.contains('maximized')
  };
}
function athenaSaveState(){
  if(athenaBooting || athenaRestoringState) return;
  var windows={};
  athenaManagedWindows().forEach(function(id){
    var win=byId(id);
    if(win) windows[id]=athenaWindowState(win);
  });
  var current=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
    (byId('docframe') ? byId('docframe').getAttribute('src') : '');
  try{
    localStorage.setItem(athenaStorageKey(),JSON.stringify({
      version:1,
      currentDoc:current || '',
      docHistory:athenaDocHistory,
      docIndex:athenaDocIndex,
      topZ:athenaTopZ,
      windows:windows
    }));
  }
  catch(e){}
}
function athenaApplyWindowState(id,state){
  var win=byId(id);
  if(!win || !state) return;
  ['left','top','width','height','zIndex'].forEach(function(name){
    if(state[name]) win.style[name]=state[name];
  });
  win.classList.toggle('closed',!!state.closed);
  win.classList.toggle('minimized',!!state.minimized);
  win.classList.toggle('maximized',!!state.maximized);
  var task=athenaTaskFor(id);
  if(task) task.classList.toggle('active',!state.closed && !state.minimized);
}
function athenaApplySavedState(state){
  if(!state || !state.windows) return false;
  athenaRestoringState=true;
  athenaTopZ=state.topZ || athenaTopZ;
  Object.keys(state.windows).forEach(function(id){
    athenaApplyWindowState(id,state.windows[id]);
  });
  athenaAuxiliaryWindows().forEach(function(id){
    if(state.windows[id]) return;
    var win=byId(id);
    if(win && win.athenaClose) win.athenaClose({silent:true});
  });
  athenaDocHistory=Array.isArray(state.docHistory) ? state.docHistory.slice() : [];
  athenaDocIndex=typeof state.docIndex==='number' ? state.docIndex : -1;
  athenaRestoringState=false;
  return true;
}
function athenaDefaultLayout(){
  athenaAuxiliaryWindows().forEach(function(id){
    var win=byId(id);
    if(win && win.athenaClose) win.athenaClose({silent:true});
  });
  var viewer=byId('viewer');
  if(viewer && viewer.athenaMaximize) viewer.athenaMaximize({silent:true});
  athenaFocusWindow(viewer);
}
function athenaOpenInitialDoc(path,state){
  if(state && Array.isArray(state.docHistory) && state.docHistory.length){
    athenaDocHistory=state.docHistory.slice();
    athenaDocIndex=typeof state.docIndex==='number' ? state.docIndex :
      athenaDocHistory.indexOf(path);
    if(athenaDocIndex<0 || athenaDocIndex>=athenaDocHistory.length)
      athenaDocIndex=athenaDocHistory.length-1;
    openDoc(athenaDocHistory[athenaDocIndex],{
      noHistory:true,
      preserveViewerState:true
    });
  }
  else openDoc(path || 'about:blank');
}
function athenaFocusWindow(win){
  if(!win || win.classList.contains('closed')) return;
  athenaTopZ+=1;
  win.style.zIndex=athenaTopZ;
  document.querySelectorAll('.task').forEach(function(t){t.classList.remove('active');});
  var task=athenaTaskFor(win.id);
  if(task) task.classList.add('active');
  athenaSaveState();
}
function athenaDocStem(path){
  if(!path || path==='about:blank') return 'Document';
  var clean=String(path).split('#')[0].split('?')[0];
  var data=window.ATHENA_SITE_DATA || {};
  var files=data.files || [];
  for(var i=0;i<files.length;i++){
    if(files[i].html===clean)
      return files[i].displayTitle || files[i].title ||
        files[i].stemTitle || athenaDocStem(files[i].path);
  }
  var name=clean.substring(clean.lastIndexOf('/')+1);
  var dot=name.lastIndexOf('.');
  if(dot>0) name=name.substring(0,dot);
  return name || 'Document';
}
function athenaSetDocTitle(path){
  var title=athenaDocStem(path);
  var cap=byId('viewer-title');
  if(cap) cap.textContent=title;
  var task=athenaTaskFor('viewer');
  if(task) task.textContent=title;
}
function athenaUpdateDocNav(){
  var back=byId('doc-back'), forward=byId('doc-forward');
  var standalone=byId('doc-standalone');
  if(back) back.disabled=athenaDocIndex<=0;
  if(forward) forward.disabled=athenaDocIndex<0 ||
    athenaDocIndex>=athenaDocHistory.length-1;
  if(standalone){
    var current=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
      (byId('docframe') ? byId('docframe').getAttribute('src') : '');
    standalone.disabled=!current || current==='about:blank';
  }
}
function openDoc(path, options){
  options=options||{};
  byId('docframe').src=path;
  if(!options.noHistory){
    if(athenaDocIndex<athenaDocHistory.length-1)
      athenaDocHistory=athenaDocHistory.slice(0,athenaDocIndex+1);
    if(athenaDocHistory[athenaDocHistory.length-1]!==path){
      athenaDocHistory.push(path);
      athenaDocIndex=athenaDocHistory.length-1;
    }
  }
  athenaSetDocTitle(path);
  athenaUpdateDocNav();
  var viewer=byId('viewer');
  if(!options.preserveViewerState){
    if(viewer){
      viewer.classList.remove('closed');
      viewer.classList.remove('minimized');
      var task=athenaTaskFor('viewer');
      if(task) task.classList.add('active');
    }
    athenaFocusWindow(viewer);
  }
  athenaSaveState();
}
function athenaOpenDoc(path){openDoc(path);}
function athenaOpenStandaloneDoc(){
  var path=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
    (byId('docframe') ? byId('docframe').getAttribute('src') : '');
  if(!path || path==='about:blank') return;
  var tab=window.open(path,'_blank','noopener');
  if(tab) tab.opener=null;
}
function athenaDocBack(){
  if(athenaDocIndex<=0) return;
  athenaDocIndex-=1;
  openDoc(athenaDocHistory[athenaDocIndex],{noHistory:true});
}
function athenaDocForward(){
  if(athenaDocIndex>=athenaDocHistory.length-1) return;
  athenaDocIndex+=1;
  openDoc(athenaDocHistory[athenaDocIndex],{noHistory:true});
}
function athenaMissingTarget(target){
  var m=byId('missing-modal');
  byId('missing-text').textContent='Destination is not in the exported site: '+target;
  m.style.display='block';
  m.style.zIndex=++athenaTopZ;
}
function closeMissing(){byId('missing-modal').style.display='none';}
window.addEventListener('message',function(ev){
  var data=ev.data || {};
  if(data.type==='athena-missing-target') athenaMissingTarget(data.target || '');
  else if(data.type==='athena-open-doc') athenaOpenDoc(data.path || 'about:blank');
  else if(data.type==='athena-outline') athenaReceiveOutline(data.headings || []);
});
function installWindow(id){
  var win=byId(id), title=win.querySelector('.title'), resizing=false, moving=false;
  var sx=0, sy=0, ox=0, oy=0, ow=0, oh=0, restore=null;
  var defaultRect={
    left:win.style.left,
    top:win.style.top,
    width:win.style.width,
    height:win.style.height
  };
  function taskButton(){
    var bar=byId('taskbar'), btn=byId('task-'+id);
    if(btn) return btn;
    btn=document.createElement('button');
    btn.id='task-'+id; btn.className='task active';
    btn.textContent=title.querySelector('.caption').textContent;
    btn.onclick=function(){restoreWindow();};
    bar.appendChild(btn);
    return btn;
  }
  function activateTask(active){taskButton().classList.toggle('active',active);}
  function restoreWindow(){
    win.classList.remove('closed');
    win.classList.remove('minimized');
    activateTask(true);
    if(win.classList.contains('maximized')){
      var rect=restore || defaultRect;
      win.classList.remove('maximized');
      win.style.left=rect.left; win.style.top=rect.top;
      win.style.width=rect.width; win.style.height=rect.height;
      restore=null;
    }
    athenaFocusWindow(win);
    athenaSaveState();
  }
  function maximizeWindow(options){
    options=options||{};
    if(win.classList.contains('maximized')) return;
    restore={left:win.style.left,top:win.style.top,width:win.style.width,height:win.style.height};
    win.classList.add('maximized'); win.classList.remove('minimized');
    win.classList.remove('closed');
    win.style.left='8px'; win.style.top='8px';
    win.style.width='calc(100% - 24px)'; win.style.height='calc(100% - 24px)';
    activateTask(true);
    athenaFocusWindow(win);
    if(!options.silent) athenaSaveState();
  }
  function minimizeWindow(){
    win.classList.add('minimized');
    activateTask(false);
    athenaSaveState();
  }
  function closeWindow(options){
    options=options||{};
    win.classList.add('closed');
    win.classList.remove('minimized');
    activateTask(false);
    if(!options.silent) athenaSaveState();
  }
  win.athenaRestore=restoreWindow;
  win.athenaMaximize=maximizeWindow;
  win.athenaMinimize=minimizeWindow;
  win.athenaClose=closeWindow;
  title.onmousedown=function(e){
    if(e.target.closest('button')) return;
    if(win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    moving=true;sx=e.clientX;sy=e.clientY;ox=win.offsetLeft;oy=win.offsetTop;e.preventDefault();
  };
  win.onmousedown=function(){athenaFocusWindow(win);};
  win.querySelector('.min').onclick=minimizeWindow;
  win.querySelector('.max').onclick=maximizeWindow;
  win.querySelector('.restore').onclick=restoreWindow;
  win.querySelector('.close').onclick=closeWindow;
  win.querySelector('.resize').onmousedown=function(e){
    if(win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    resizing=true;sx=e.clientX;sy=e.clientY;ow=win.offsetWidth;oh=win.offsetHeight;e.preventDefault();
  };
  taskButton();
  athenaFocusWindow(win);
  document.addEventListener('mousemove',function(e){
    if(moving){win.style.left=(ox+e.clientX-sx)+'px';win.style.top=(oy+e.clientY-sy)+'px';}
    if(resizing){win.style.width=Math.max(220,ow+e.clientX-sx)+'px';win.style.height=Math.max(120,oh+e.clientY-sy)+'px';}
  });
  document.addEventListener('mouseup',function(){
    if(moving || resizing) athenaSaveState();
    moving=false;resizing=false;
  });
}
