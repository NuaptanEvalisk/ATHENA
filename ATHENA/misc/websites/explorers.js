
function treeList(items, makeHref){
  var ul=document.createElement('ul'); ul.className='tree';
  items.forEach(function(item){
    var li=document.createElement('li'); var a=document.createElement('a');
    a.href='#'; a.textContent=item.title||item.name||item.path;
    a.onclick=function(ev){ev.preventDefault(); athenaOpenDoc(makeHref(item));};
    li.appendChild(a); ul.appendChild(li);
  });
  return ul;
}
function initExplorers(){
  var d=window.ATHENA_SITE_DATA;
  byId('vault-content').appendChild(treeList(d.files,function(x){return x.html;}));
  byId('namespace-content').appendChild(treeList(d.namespaces,function(x){return x.homepage;}));
}
