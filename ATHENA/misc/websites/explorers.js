
function athenaSimpleTreeList(items, makeHref){
  var ul=document.createElement('ul'); ul.className='tree';
  items.forEach(function(item){
    var li=document.createElement('li'); var a=document.createElement('a');
    a.href='#'; a.textContent=item.title||item.name||item.path;
    a.onclick=function(ev){ev.preventDefault(); athenaOpenDoc(makeHref(item));};
    li.appendChild(a); ul.appendChild(li);
  });
  return ul;
}

function athenaVaultBasename(path){
  var parts=String(path||'').split('/').filter(Boolean);
  return parts.length ? parts[parts.length-1] : path;
}

function athenaMakeVaultTree(files){
  var root={dirs:{},docs:[]};
  files.forEach(function(file){
    var parts=String(file.path||'').split('/').filter(Boolean);
    if(!parts.length) return;
    var node=root;
    for(var i=0;i<parts.length-1;i++){
      var name=parts[i];
      if(!node.dirs[name]) node.dirs[name]={name:name,dirs:{},docs:[]};
      node=node.dirs[name];
    }
    node.docs.push(file);
  });
  return root;
}

function athenaSortedKeys(object){
  return Object.keys(object).sort(function(a,b){
    return a.localeCompare(b,undefined,{numeric:true,sensitivity:'base'});
  });
}

function athenaSortedDocs(docs){
  return docs.slice().sort(function(a,b){
    return String(a.path||'').localeCompare(String(b.path||''),undefined,
      {numeric:true,sensitivity:'base'});
  });
}

function athenaRenderVaultNode(node){
  var ul=document.createElement('ul'); ul.className='tree vault-tree';
  athenaSortedKeys(node.dirs).forEach(function(name){
    var child=node.dirs[name];
    var li=document.createElement('li'); li.className='tree-dir';
    var row=document.createElement('div'); row.className='tree-row';
    var toggle=document.createElement('button');
    toggle.className='tree-toggle';
    toggle.type='button';
    toggle.setAttribute('aria-label','Collapse '+name);
    toggle.setAttribute('aria-expanded','true');
    toggle.onclick=function(){
      var collapsed=!li.classList.contains('collapsed');
      li.classList.toggle('collapsed',collapsed);
      toggle.setAttribute('aria-expanded',collapsed?'false':'true');
      toggle.setAttribute('aria-label',(collapsed?'Expand ':'Collapse ')+name);
    };
    var label=document.createElement('span');
    label.className='tree-folder';
    label.textContent=name;
    row.appendChild(toggle);
    row.appendChild(label);
    li.appendChild(row);
    li.appendChild(athenaRenderVaultNode(child));
    ul.appendChild(li);
  });
  athenaSortedDocs(node.docs).forEach(function(file){
    var li=document.createElement('li'); li.className='tree-doc';
    var row=document.createElement('div'); row.className='tree-row';
    var spacer=document.createElement('span'); spacer.className='tree-spacer';
    var a=document.createElement('a');
    a.href='#';
    a.textContent=athenaVaultBasename(file.path);
    a.title=(file.title&&file.title!==file.path) ? file.title+' - '+file.path :
      file.path;
    a.onclick=function(ev){ev.preventDefault(); athenaOpenDoc(file.html);};
    row.appendChild(spacer);
    row.appendChild(a);
    li.appendChild(row);
    ul.appendChild(li);
  });
  return ul;
}

function athenaInitVaultExplorer(data){
  var container=byId('vault-content');
  container.innerHTML='';
  container.appendChild(athenaRenderVaultNode(athenaMakeVaultTree(data.files||[])));
}

function athenaInitNamespaceExplorer(data){
  var container=byId('namespace-content');
  container.innerHTML='';
  container.appendChild(athenaSimpleTreeList(data.namespaces||[],function(x){
    return x.homepage;
  }));
}

function initExplorers(){
  var d=window.ATHENA_SITE_DATA;
  athenaInitVaultExplorer(d||{});
  athenaInitNamespaceExplorer(d||{});
}
