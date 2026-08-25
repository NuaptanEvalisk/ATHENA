(function(){
  'use strict';

  var siteRoot=new URL(window.ATHENA_SITE_ROOT||'./',window.location.href);
  var manifest=null;
  var overlay=null;
  var activeTool=null;
  var headingCounter=0;

  function iconUrl(name){return new URL('icons/'+name+'.svg',siteRoot).href;}
  function el(tag,className,text){
    var node=document.createElement(tag);
    if(className) node.className=className;
    if(text!==undefined) node.textContent=text;
    return node;
  }
  function iconButton(name,label,handler){
    var button=el('button','athena-site-tool');
    button.type='button';
    button.title=label;
    button.setAttribute('aria-label',label);
    button.setAttribute('aria-expanded','false');
    var image=document.createElement('img');
    image.src=iconUrl(name);
    image.alt='';
    button.appendChild(image);
    button.addEventListener('click',function(){handler(button);});
    return button;
  }
  function prepareExternalLinks(){
    Array.prototype.forEach.call(document.querySelectorAll('a[href]'),function(link){
      var href=String(link.getAttribute('href')||'').trim();
      if(!/^(?:https?:)?\/\//i.test(href)) return;
      link.target='_blank';
      var rel=String(link.rel||'').split(/\s+/).filter(Boolean);
      ['noopener','noreferrer'].forEach(function(value){
        if(rel.indexOf(value)<0) rel.push(value);
      });
      link.rel=rel.join(' ');
    });
  }
  function encodePath(path){
    return String(path).split('/').map(function(segment){
      try{return encodeURIComponent(decodeURIComponent(segment));}
      catch(e){return encodeURIComponent(segment);}
    }).join('/');
  }
  function documentUrl(path){
    path=String(path||'');
    if(!path||path==='about:blank') return '';
    if(/^[a-z][a-z0-9+.-]*:/i.test(path)) return path;
    if(path.charAt(0)==='#') return path;
    var hash='';
    var split=path.indexOf('#');
    if(split>=0){hash=path.substring(split);path=path.substring(0,split);}
    var target=new URL(encodePath(path),siteRoot);
    if(/^https?:$/i.test(target.protocol)&&/\.html$/i.test(target.pathname))
      target.pathname=target.pathname.substring(0,target.pathname.length-5);
    target.hash=hash;
    return target.href;
  }
  function openDocument(path){
    var target=documentUrl(path);
    if(target) window.location.href=target;
  }
  window.athenaOpenDoc=openDocument;

  function closeOverlay(){
    if(!overlay) return;
    overlay.remove();
    overlay=null;
    if(activeTool){
      activeTool.setAttribute('aria-expanded','false');
      activeTool.focus();
    }
    activeTool=null;
  }
  function openPanel(title,tool,build){
    closeOverlay();
    activeTool=tool||null;
    if(activeTool) activeTool.setAttribute('aria-expanded','true');
    overlay=el('div','athena-site-overlay');
    overlay.setAttribute('role','presentation');
    var dialog=el('section','athena-site-dialog');
    dialog.setAttribute('role','dialog');
    dialog.setAttribute('aria-modal','true');
    dialog.setAttribute('aria-labelledby','athena-site-dialog-title');
    var header=el('header','athena-site-dialog-header');
    var heading=el('h2','athena-site-dialog-title',title);
    heading.id='athena-site-dialog-title';
    var close=el('button','athena-site-close');
    close.type='button';
    close.title='Close';
    close.setAttribute('aria-label','Close');
    var closeIcon=document.createElement('img');
    closeIcon.src=iconUrl('close');
    closeIcon.alt='';
    close.appendChild(closeIcon);
    close.onclick=closeOverlay;
    header.appendChild(heading);
    header.appendChild(close);
    var body=el('div','athena-site-dialog-body');
    dialog.appendChild(header);
    dialog.appendChild(body);
    overlay.appendChild(dialog);
    overlay.addEventListener('mousedown',function(event){
      if(event.target===overlay) closeOverlay();
    });
    document.body.appendChild(overlay);
    build(body);
    var focus=body.querySelector('input,button,a,[tabindex]')||close;
    if(focus) focus.focus();
  }
  function showMessage(message){
    openPanel('ATHENA',null,function(body){
      body.appendChild(el('p','athena-site-empty',message));
    });
  }
  window.athenaMissingTarget=function(target){
    showMessage('Destination is not in the exported site: '+String(target));
  };

  function basename(path){
    var parts=String(path||'').split('/').filter(Boolean);
    return parts.length?parts[parts.length-1]:path;
  }
  function sortedKeys(object){
    return Object.keys(object).sort(function(a,b){
      return a.localeCompare(b,undefined,{numeric:true,sensitivity:'base'});
    });
  }
  function vaultTree(files){
    var root={dirs:{},docs:[]};
    files.forEach(function(file){
      var parts=String(file.path||'').split('/').filter(Boolean);
      if(!parts.length) return;
      var node=root;
      for(var i=0;i<parts.length-1;i++){
        if(!node.dirs[parts[i]]) node.dirs[parts[i]]={dirs:{},docs:[]};
        node=node.dirs[parts[i]];
      }
      node.docs.push(file);
    });
    return root;
  }
  function renderVaultNode(node){
    var list=el('ul','athena-site-tree');
    sortedKeys(node.dirs).forEach(function(name){
      var item=el('li','athena-site-tree-dir');
      var row=el('div','athena-site-tree-row');
      var toggle=el('button','athena-site-tree-toggle');
      toggle.type='button';
      toggle.setAttribute('aria-expanded','true');
      toggle.setAttribute('aria-label','Collapse '+name);
      toggle.onclick=function(){
        var collapsed=!item.classList.contains('collapsed');
        item.classList.toggle('collapsed',collapsed);
        toggle.setAttribute('aria-expanded',collapsed?'false':'true');
        toggle.setAttribute('aria-label',(collapsed?'Expand ':'Collapse ')+name);
      };
      row.appendChild(toggle);
      row.appendChild(el('span','athena-site-tree-folder',name));
      item.appendChild(row);
      item.appendChild(renderVaultNode(node.dirs[name]));
      list.appendChild(item);
    });
    node.docs.slice().sort(function(a,b){
      return String(a.path).localeCompare(String(b.path));
    }).forEach(function(file){
      var item=el('li');
      var row=el('div','athena-site-tree-row');
      row.appendChild(el('span','athena-site-tree-spacer'));
      var link=el('a','',basename(file.path));
      link.href=documentUrl(file.html);
      link.title=file.displayTitle||file.path;
      row.appendChild(link);
      item.appendChild(row);
      list.appendChild(item);
    });
    return list;
  }
  function openVault(tool){
    openPanel('Vault Explorer',tool,function(body){
      body.appendChild(renderVaultNode(vaultTree(manifest.files||[])));
    });
  }
  function openNamespaces(tool){
    openPanel('Namespace Explorer',tool,function(body){
      var list=el('ul','athena-site-tree');
      (manifest.namespaces||[]).slice().sort(function(a,b){
        return a.name.localeCompare(b.name);
      }).forEach(function(namespace){
        var item=el('li');
        var row=el('div','athena-site-tree-row');
        row.appendChild(el('span','athena-site-tree-spacer'));
        var link=el('a','',namespace.name);
        link.href=documentUrl(namespace.homepage);
        row.appendChild(link);
        item.appendChild(row);
        list.appendChild(item);
      });
      body.appendChild(list);
    });
  }
  function headingId(node){
    if(node.id) return node.id;
    headingCounter+=1;
    node.id='athena-heading-'+headingCounter;
    return node.id;
  }
  function headings(){
    headingCounter=0;
    return Array.prototype.slice.call(document.querySelectorAll('h1,h2,h3,h4,h5,h6'))
      .map(function(node){
        return {
          node:node,
          id:headingId(node),
          level:Number(String(node.tagName).substring(1))||1,
          text:(node.textContent||'').replace(/\s+/g,' ').trim()
        };
      }).filter(function(item){return item.text;});
  }
  function openOutline(tool){
    openPanel('Outline',tool,function(body){
      var items=headings();
      if(!items.length){
        body.appendChild(el('p','athena-site-empty','No headings in this document.'));
        return;
      }
      var list=el('ul','athena-site-outline');
      items.forEach(function(item){
        var li=el('li','level-'+item.level);
        var link=el('a','',item.text);
        link.href='#'+encodeURIComponent(item.id);
        link.onclick=function(event){
          event.preventDefault();
          closeOverlay();
          item.node.scrollIntoView({block:'start',inline:'nearest'});
          history.replaceState(null,'','#'+encodeURIComponent(item.id));
        };
        li.appendChild(link);
        list.appendChild(li);
      });
      body.appendChild(list);
    });
  }
  function renderResults(container,items){
    container.innerHTML='';
    var list=el('ul','athena-site-results');
    items.forEach(function(item,index){
      var li=el('li',index===0?'active':'');
      li.tabIndex=0;
      li.dataset.href=item.html||item.homepage;
      li.onclick=function(){openDocument(li.dataset.href);};
      li.onkeydown=function(event){
        if(event.key==='Enter'||event.key===' '){
          event.preventDefault();
          li.click();
        }
      };
      li.appendChild(el('div','athena-site-result-title',
        item.title||item.name||item.path));
      li.appendChild(el('div','athena-site-result-path',
        item.path||item.name||''));
      if(item.snippet)
        li.appendChild(el('div','athena-site-result-snippet',item.snippet));
      list.appendChild(li);
    });
    container.appendChild(list);
  }
  function installResultKeys(input,results){
    input.addEventListener('keydown',function(event){
      var items=Array.prototype.slice.call(results.querySelectorAll('li'));
      var active=results.querySelector('li.active');
      var index=Math.max(0,items.indexOf(active));
      if((event.key==='ArrowDown'||event.key==='ArrowUp')&&items.length){
        if(active) active.classList.remove('active');
        index=event.key==='ArrowDown'?
          Math.min(items.length-1,index+1):Math.max(0,index-1);
        items[index].classList.add('active');
        items[index].scrollIntoView({block:'nearest'});
        event.preventDefault();
      }
      else if(event.key==='Enter'&&active){
        event.preventDefault();
        active.click();
      }
    });
  }
  function openSearch(tool){
    openPanel('Global Search',tool,function(body){
      var input=el('input','athena-site-query');
      input.type='search';
      input.placeholder='Search exported documents';
      input.autocomplete='off';
      var results=el('div');
      body.appendChild(input);
      body.appendChild(results);
      function update(){
        var query=input.value.toLowerCase().trim();
        var items=(manifest.files||[]).map(function(file){
          var hay=((file.displayTitle||'')+' '+(file.path||'')+' '+
            (file.searchText||'')).toLowerCase();
          if(query&&hay.indexOf(query)<0) return null;
          var text=file.searchText||'';
          var position=query?text.toLowerCase().indexOf(query):-1;
          var snippet=position<0?'':text.substring(
            Math.max(0,position-70),Math.min(text.length,position+180));
          return Object.assign({},file,{
            title:file.displayTitle||file.stemTitle,
            snippet:snippet
          });
        }).filter(Boolean).slice(0,50);
        renderResults(results,items);
      }
      input.oninput=update;
      installResultKeys(input,results);
      update();
      input.focus();
    });
  }
  function quickItems(query){
    var files=(manifest.files||[]).map(function(file){
      return {
        title:file.stemTitle||basename(file.path),
        path:file.path,
        html:file.html,
        kind:'file'
      };
    });
    var namespaces=(manifest.namespaces||[]).map(function(namespace){
      return {
        title:namespace.name,
        path:namespace.name,
        html:namespace.homepage,
        kind:'namespace'
      };
    });
    var all=files.concat(namespaces);
    query=query.toLowerCase().trim();
    if(!query) return all.slice(0,40);
    return all.filter(function(item){
      return (item.title+' '+item.path+' '+item.kind).toLowerCase()
        .indexOf(query)>=0;
    }).slice(0,40);
  }
  function openQuickSwitcher(tool){
    openPanel('Quick Switcher',tool,function(body){
      var input=el('input','athena-site-query');
      input.type='search';
      input.placeholder='Open a document or namespace';
      input.autocomplete='off';
      var results=el('div');
      body.appendChild(input);
      body.appendChild(results);
      function update(){renderResults(results,quickItems(input.value));}
      input.oninput=update;
      installResultKeys(input,results);
      update();
      input.focus();
    });
  }
  function addToolbar(){
    var toolbar=el('nav','athena-site-toolbar');
    toolbar.setAttribute('aria-label','Website navigation');
    toolbar.appendChild(iconButton('vault','Vault Explorer',openVault));
    toolbar.appendChild(iconButton('namespace','Namespace Explorer',openNamespaces));
    toolbar.appendChild(iconButton('outline','Outline',openOutline));
    toolbar.appendChild(el('span','athena-site-divider'));
    toolbar.appendChild(iconButton('search','Global Search',openSearch));
    toolbar.appendChild(iconButton('switcher','Quick Switcher',openQuickSwitcher));
    if(window.ATHENA_DOCUMENT_PDF){
      toolbar.appendChild(el('span','athena-site-divider'));
      var pdf=el('a','athena-site-tool athena-site-tool-pdf');
      pdf.href=String(window.ATHENA_DOCUMENT_PDF);
      pdf.download='';
      pdf.title='Download PDF';
      pdf.setAttribute('aria-label','Download PDF');
      var image=document.createElement('img');
      image.src=iconUrl('pdf');
      image.alt='';
      pdf.appendChild(image);
      toolbar.appendChild(pdf);
    }
    document.body.appendChild(toolbar);
  }
  function editableTarget(target){
    return target&&(/^(INPUT|TEXTAREA|SELECT)$/i.test(target.tagName)||
      target.isContentEditable);
  }
  function installKeyboard(){
    document.addEventListener('keydown',function(event){
      if(event.key==='Escape'&&overlay){
        event.preventDefault();
        closeOverlay();
        return;
      }
      if(!event.ctrlKey&&!event.metaKey&&!event.altKey&&!event.shiftKey&&
         event.key.toLowerCase()==='o'&&!editableTarget(event.target)){
        event.preventDefault();
        openQuickSwitcher(null);
      }
    });
  }
  function initialize(data){
    manifest=data||{files:[],namespaces:[]};
    window.ATHENA_SITE_DATA=manifest;
    prepareExternalLinks();
    addToolbar();
    installKeyboard();
  }
  function start(){
    if(window.ATHENA_SITE_DATA){
      initialize(window.ATHENA_SITE_DATA);
      return;
    }
    fetch(new URL('site-manifest.json',siteRoot),{cache:'no-store'})
      .then(function(response){
        if(!response.ok) throw new Error(String(response.status));
        return response.json();
      })
      .then(initialize)
      .catch(function(){initialize({files:[],namespaces:[]});});
  }
  if(document.readyState==='loading')
    document.addEventListener('DOMContentLoaded',start);
  else start();
})();
