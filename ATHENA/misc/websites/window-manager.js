
var athenaTopZ=20;
var athenaDocHistory=[];
var athenaDocIndex=-1;
var athenaBooting=true;
var athenaRestoringState=false;
var athenaPopupReturnFocus=null;

function byId(id){return document.getElementById(id);}
function athenaTaskId(id){return 'task-'+id;}
function athenaTaskFor(id){return byId(athenaTaskId(id));}
function athenaManagedWindows(){
  return ['vault','namespaces','outline','global-search','quick-switcher','viewer'];
}
function athenaAuxiliaryWindows(){
  return ['vault','namespaces','outline','global-search','quick-switcher'];
}
function athenaWindowLabel(id){
  var win=byId(id), caption=win&&win.querySelector('.caption');
  return caption ? caption.textContent : id;
}
function athenaStorageKey(){
  var data=window.ATHENA_SITE_DATA || {};
  var mode=athenaIsMobileLayout() ? ':mobile' : ':desktop';
  return (data.storageKey || ('athena-website:'+location.pathname)) + mode;
}
function athenaIsMobileLayout(){
  return window.matchMedia &&
    window.matchMedia('(max-width:720px), (pointer:coarse)').matches;
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
      version:2,
      currentDoc:current || '',
      docHistory:athenaDocHistory,
      docIndex:athenaDocIndex,
      topZ:athenaTopZ,
      windows:windows
    }));
  }
  catch(e){}
}
function athenaSetWindowVisibility(win){
  if(!win) return;
  var unavailable=win.classList.contains('closed') ||
    win.classList.contains('minimized');
  win.setAttribute('aria-hidden',unavailable ? 'true' : 'false');
}
function athenaSyncTask(id){
  var win=byId(id), tasks=byId('task-buttons');
  if(!win || !tasks) return null;
  var task=athenaTaskFor(id);
  if(win.classList.contains('closed')){
    if(task) task.remove();
    athenaSetWindowVisibility(win);
    return null;
  }
  if(!task){
    task=document.createElement('button');
    task.type='button';
    task.id=athenaTaskId(id);
    task.className='task';
    task.setAttribute('aria-controls',id);
    task.onclick=function(){
      if(win.classList.contains('minimized')) win.athenaShow();
      else athenaFocusWindow(win);
    };
    task.oncontextmenu=function(ev){
      ev.preventDefault();
      athenaShowWindowContext(id,ev.clientX,ev.clientY,task);
    };
    task.onkeydown=function(ev){
      if(ev.key==='ContextMenu' || (ev.shiftKey && ev.key==='F10')){
        var rect=task.getBoundingClientRect();
        athenaShowWindowContext(id,rect.left,rect.top,task);
        ev.preventDefault();
      }
    };
    tasks.appendChild(task);
  }
  task.textContent=athenaWindowLabel(id);
  task.setAttribute('aria-label',athenaWindowLabel(id));
  task.classList.toggle('active',!win.classList.contains('minimized') &&
    Number(win.style.zIndex||0)===athenaTopZ);
  task.setAttribute('aria-pressed',task.classList.contains('active') ?
    'true' : 'false');
  athenaSetWindowVisibility(win);
  return task;
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
  athenaSyncTask(id);
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
  athenaManagedWindows().forEach(function(id){
    var win=byId(id);
    if(win && win.athenaReset) win.athenaReset({silent:true});
  });
  athenaAuxiliaryWindows().forEach(function(id){
    var win=byId(id);
    if(win && win.athenaClose) win.athenaClose({silent:true});
  });
  var viewer=byId('viewer');
  if(viewer && viewer.athenaMaximize) viewer.athenaMaximize({silent:true});
  athenaFocusWindow(viewer);
}
function athenaResetLayout(){
  athenaDefaultLayout();
  athenaSaveState();
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
  if(!win || win.classList.contains('closed') ||
     win.classList.contains('minimized')) return;
  athenaTopZ+=1;
  win.style.zIndex=athenaTopZ;
  document.querySelectorAll('.task').forEach(function(task){
    task.classList.remove('active');
    task.setAttribute('aria-pressed','false');
  });
  var task=athenaSyncTask(win.id);
  if(task){
    task.classList.add('active');
    task.setAttribute('aria-pressed','true');
  }
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
  athenaSyncTask('viewer');
}
function athenaPdfForDoc(path){
  var clean=String(path||'').split('#')[0].split('?')[0];
  var files=(window.ATHENA_SITE_DATA&&window.ATHENA_SITE_DATA.files)||[];
  for(var i=0;i<files.length;i++)
    if(files[i].html===clean) return files[i].pdf||'';
  return '';
}
function athenaUpdateDocNav(){
  var back=byId('doc-back'), forward=byId('doc-forward');
  var standalone=byId('doc-standalone'), pdf=byId('doc-pdf');
  if(back) back.disabled=athenaDocIndex<=0;
  if(forward) forward.disabled=athenaDocIndex<0 ||
    athenaDocIndex>=athenaDocHistory.length-1;
  if(standalone){
    var current=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
      (byId('docframe') ? byId('docframe').getAttribute('src') : '');
    standalone.disabled=!current || current==='about:blank';
  }
  if(pdf){
    pdf.hidden=!(window.ATHENA_SITE_DATA&&
      window.ATHENA_SITE_DATA.generatePdfs);
    var pdfCurrent=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
      (byId('docframe') ? byId('docframe').getAttribute('src') : '');
    pdf.disabled=!athenaPdfForDoc(pdfCurrent);
  }
}
function openDoc(path,options){
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
  if(!options.preserveViewerState && viewer && viewer.athenaShow)
    viewer.athenaShow();
  athenaSaveState();
}
function athenaOpenDoc(path){openDoc(path);}
function athenaOpenPathInNewTab(path){
  if(!path || path==='about:blank') return;
  var tab=window.open(path,'_blank','noopener');
  if(tab) tab.opener=null;
}
function athenaOpenStandaloneDoc(){
  var path=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
    (byId('docframe') ? byId('docframe').getAttribute('src') : '');
  athenaOpenPathInNewTab(path);
}
function athenaDownloadCurrentPdf(){
  var path=athenaDocIndex>=0 ? athenaDocHistory[athenaDocIndex] :
    (byId('docframe') ? byId('docframe').getAttribute('src') : '');
  var pdf=athenaPdfForDoc(path);
  if(!pdf) return;
  var link=document.createElement('a');
  link.href=pdf;
  link.download='';
  document.body.appendChild(link);
  link.click();
  link.remove();
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
  var modal=byId('missing-modal');
  byId('missing-text').textContent='Destination is not in the exported site: '+target;
  modal.hidden=false;
  modal.style.zIndex=++athenaTopZ;
  var button=modal.querySelector('button');
  if(button) button.focus();
}
function closeMissing(){byId('missing-modal').hidden=true;}

function athenaHidePopup(menu,restoreFocus){
  if(!menu || menu.hidden) return;
  menu.hidden=true;
  menu.innerHTML='';
  if(restoreFocus && athenaPopupReturnFocus && athenaPopupReturnFocus.focus)
    athenaPopupReturnFocus.focus();
  athenaPopupReturnFocus=null;
  var start=byId('start-button');
  if(start) start.setAttribute('aria-expanded','false');
}
function athenaHideMenus(restoreFocus){
  athenaHidePopup(byId('start-menu'),restoreFocus);
  athenaHidePopup(byId('context-menu'),restoreFocus);
}
function athenaMenuButton(item,menu){
  if(item.separator){
    var separator=document.createElement('div');
    separator.className='menu-separator';
    separator.setAttribute('role','separator');
    return separator;
  }
  var button=document.createElement('button');
  button.type='button';
  button.className='menu-item';
  button.setAttribute('role','menuitem');
  button.textContent=item.label;
  button.disabled=!!item.disabled;
  button.onclick=function(){
    athenaHidePopup(menu,false);
    item.action();
  };
  return button;
}
function athenaPopulateMenu(menu,items){
  menu.innerHTML='';
  items.forEach(function(item){menu.appendChild(athenaMenuButton(item,menu));});
  menu.onkeydown=function(ev){
    var buttons=Array.prototype.slice.call(menu.querySelectorAll('.menu-item:not(:disabled)'));
    var index=buttons.indexOf(document.activeElement);
    if(ev.key==='ArrowDown' && buttons.length){
      buttons[(index+1+buttons.length)%buttons.length].focus(); ev.preventDefault();
    }
    else if(ev.key==='ArrowUp' && buttons.length){
      buttons[(index-1+buttons.length)%buttons.length].focus(); ev.preventDefault();
    }
    else if(ev.key==='Home' && buttons.length){buttons[0].focus();ev.preventDefault();}
    else if(ev.key==='End' && buttons.length){buttons[buttons.length-1].focus();ev.preventDefault();}
    else if(ev.key==='Escape'){athenaHidePopup(menu,true);ev.preventDefault();}
  };
}
function athenaShowPopup(menu,items,x,y,opener){
  athenaHideMenus(false);
  athenaPopupReturnFocus=opener || document.activeElement;
  athenaPopulateMenu(menu,items);
  menu.hidden=false;
  if(typeof x==='number'){
    menu.style.left=Math.max(4,Math.min(x,window.innerWidth-menu.offsetWidth-4))+'px';
    menu.style.top=Math.max(4,Math.min(y,window.innerHeight-menu.offsetHeight-4))+'px';
  }
  var first=menu.querySelector('.menu-item:not(:disabled)');
  if(first) first.focus();
}
function athenaWindowMenuItems(id){
  var win=byId(id), closed=win.classList.contains('closed');
  var minimized=win.classList.contains('minimized');
  var maximized=win.classList.contains('maximized');
  return [
    {label:'Open',disabled:!closed&&!minimized,action:function(){win.athenaShow();}},
    {label:'Restore',disabled:closed||!maximized,action:function(){win.athenaRestore();}},
    {label:'Minimize',disabled:closed||minimized,action:function(){win.athenaMinimize();}},
    {label:'Maximize',disabled:closed||maximized,action:function(){win.athenaMaximize();}},
    {separator:true},
    {label:'Close',disabled:closed,action:function(){win.athenaClose();}}
  ];
}
function athenaShowWindowContext(id,x,y,opener){
  athenaShowPopup(byId('context-menu'),athenaWindowMenuItems(id),x,y,opener);
}
function athenaShowDocumentContext(path,x,y,opener){
  athenaShowPopup(byId('context-menu'),[
    {label:'Open',action:function(){athenaOpenDoc(path);}},
    {label:'Open in new tab',action:function(){athenaOpenPathInNewTab(path);}}
  ],x,y,opener);
}
function athenaInstallDocumentContext(element,path){
  if(!element || !path) return;
  element.setAttribute('data-athena-document',path);
  element.oncontextmenu=function(ev){
    ev.preventDefault();
    athenaShowDocumentContext(path,ev.clientX,ev.clientY,element);
  };
  element.addEventListener('keydown',function(ev){
    if(ev.key==='ContextMenu' || (ev.shiftKey && ev.key==='F10')){
      var rect=element.getBoundingClientRect();
      athenaShowDocumentContext(path,rect.left,rect.bottom,element);
      ev.preventDefault();
    }
  });
}
function athenaStartItems(){
  var items=athenaManagedWindows().map(function(id){
    return {label:athenaWindowLabel(id),action:function(){byId(id).athenaShow();}};
  });
  items.push({separator:true});
  items.push({label:'Reset window layout',action:athenaResetLayout});
  return items;
}
function athenaShowStartMenu(){
  var menu=byId('start-menu'), start=byId('start-button');
  if(!menu.hidden){athenaHidePopup(menu,true);return;}
  athenaShowPopup(menu,athenaStartItems(),null,null,start);
  start.setAttribute('aria-expanded','true');
}
function athenaShowDesktopContext(x,y,opener){
  athenaShowPopup(byId('context-menu'),athenaStartItems(),x,y,opener);
}
function athenaInitShellMenus(){
  var start=byId('start-button'), desktop=byId('desktop');
  start.onclick=athenaShowStartMenu;
  start.onkeydown=function(ev){
    if(ev.key==='ArrowUp' || ev.key==='ArrowDown'){
      athenaShowStartMenu(); ev.preventDefault();
    }
  };
  desktop.oncontextmenu=function(ev){
    if(ev.target!==desktop) return;
    ev.preventDefault();
    athenaShowDesktopContext(ev.clientX,ev.clientY,desktop);
  };
  desktop.onkeydown=function(ev){
    if(ev.target===desktop && (ev.key==='ContextMenu' ||
       (ev.shiftKey && ev.key==='F10'))){
      athenaShowDesktopContext(12,window.innerHeight-220,desktop);
      ev.preventDefault();
    }
  };
  document.addEventListener('mousedown',function(ev){
    if(!ev.target.closest('.popup-menu') && ev.target!==start)
      athenaHideMenus(false);
  });
  document.addEventListener('keydown',function(ev){
    if(ev.key==='Escape') athenaHideMenus(true);
  });
}

window.addEventListener('message',function(ev){
  var data=ev.data || {};
  if(data.type==='athena-missing-target') athenaMissingTarget(data.target || '');
  else if(data.type==='athena-open-doc') athenaOpenDoc(data.path || 'about:blank');
  else if(data.type==='athena-outline') athenaReceiveOutline(data.headings || []);
});

function installWindow(id){
  var win=byId(id), title=win.querySelector('.title');
  var resizing=false, moving=false;
  var sx=0, sy=0, ox=0, oy=0, ow=0, oh=0, restore=null;
  var defaultRect={
    left:win.style.left,
    top:win.style.top,
    width:win.style.width,
    height:win.style.height
  };
  function showWindow(options){
    options=options||{};
    win.classList.remove('closed');
    win.classList.remove('minimized');
    athenaSyncTask(id);
    athenaFocusWindow(win);
    if(!options.silent) athenaSaveState();
  }
  function restoreWindow(options){
    options=options||{};
    if(win.classList.contains('maximized')){
      var rect=restore || defaultRect;
      win.classList.remove('maximized');
      win.style.left=rect.left; win.style.top=rect.top;
      win.style.width=rect.width; win.style.height=rect.height;
      restore=null;
    }
    showWindow(options);
  }
  function maximizeWindow(options){
    options=options||{};
    if(!win.classList.contains('maximized')){
      restore={left:win.style.left,top:win.style.top,
        width:win.style.width,height:win.style.height};
      win.classList.add('maximized');
      win.style.left='8px'; win.style.top='8px';
      win.style.width='calc(100% - 24px)';
      win.style.height='calc(100% - 24px)';
    }
    showWindow(options);
  }
  function minimizeWindow(options){
    options=options||{};
    if(win.classList.contains('closed')) return;
    win.classList.add('minimized');
    athenaSyncTask(id);
    if(!options.silent) athenaSaveState();
  }
  function closeWindow(options){
    options=options||{};
    win.classList.add('closed');
    win.classList.remove('minimized');
    athenaSyncTask(id);
    if(!options.silent) athenaSaveState();
  }
  function resetWindow(options){
    options=options||{};
    win.classList.remove('maximized','minimized','closed');
    win.style.left=defaultRect.left; win.style.top=defaultRect.top;
    win.style.width=defaultRect.width; win.style.height=defaultRect.height;
    restore=null;
    athenaSyncTask(id);
    if(!options.silent) athenaSaveState();
  }
  win.athenaShow=showWindow;
  win.athenaRestore=restoreWindow;
  win.athenaMaximize=maximizeWindow;
  win.athenaMinimize=minimizeWindow;
  win.athenaClose=closeWindow;
  win.athenaReset=resetWindow;
  title.onmousedown=function(ev){
    if(ev.target.closest('button') || win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    moving=true; sx=ev.clientX; sy=ev.clientY;
    ox=win.offsetLeft; oy=win.offsetTop; ev.preventDefault();
  };
  title.ondblclick=function(ev){
    if(ev.target.closest('button')) return;
    if(win.classList.contains('maximized')) restoreWindow();
    else maximizeWindow();
  };
  title.oncontextmenu=function(ev){
    ev.preventDefault();
    athenaShowWindowContext(id,ev.clientX,ev.clientY,title);
  };
  title.onkeydown=function(ev){
    if(ev.key==='Enter' || ev.key===' '){athenaFocusWindow(win);ev.preventDefault();}
    else if(ev.key==='ContextMenu' || (ev.shiftKey && ev.key==='F10')){
      var rect=title.getBoundingClientRect();
      athenaShowWindowContext(id,rect.left,rect.bottom,title);
      ev.preventDefault();
    }
  };
  win.onmousedown=function(){athenaFocusWindow(win);};
  win.querySelector('.min').onclick=function(){minimizeWindow();};
  win.querySelector('.max').onclick=function(){maximizeWindow();};
  win.querySelector('.restore').onclick=function(){restoreWindow();};
  win.querySelector('.close').onclick=function(){closeWindow();};
  win.querySelectorAll('button[aria-label]').forEach(function(button){
    button.title=button.getAttribute('aria-label');
  });
  win.querySelector('.resize').onmousedown=function(ev){
    if(win.classList.contains('maximized')) return;
    athenaFocusWindow(win);
    resizing=true; sx=ev.clientX; sy=ev.clientY;
    ow=win.offsetWidth; oh=win.offsetHeight; ev.preventDefault();
  };
  athenaSyncTask(id);
  document.addEventListener('mousemove',function(ev){
    if(moving){
      win.style.left=(ox+ev.clientX-sx)+'px';
      win.style.top=(oy+ev.clientY-sy)+'px';
    }
    if(resizing){
      win.style.width=Math.max(220,ow+ev.clientX-sx)+'px';
      win.style.height=Math.max(120,oh+ev.clientY-sy)+'px';
    }
  });
  document.addEventListener('mouseup',function(){
    if(moving || resizing) athenaSaveState();
    moving=false; resizing=false;
  });
}
