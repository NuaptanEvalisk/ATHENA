
function renderResults(container, items){
  container.innerHTML='';
  var ul=document.createElement('ul'); ul.className='results';
  items.forEach(function(item,index){
    var li=document.createElement('li');
    if(index===0) li.className='active';
    li.tabIndex=0;
    li.onclick=function(){athenaOpenDoc(item.html||item.homepage);};
    var title=document.createElement('div'); title.className='result-title';
    title.textContent=item.title||item.name||item.path;
    var path=document.createElement('div'); path.className='result-path';
    path.textContent=item.path||item.name;
    li.appendChild(title); li.appendChild(path);
    if(item.snippet){
      var snip=document.createElement('div'); snip.className='result-snippet';
      snip.textContent=item.snippet; li.appendChild(snip);
    }
    ul.appendChild(li);
  });
  container.appendChild(ul);
}
function searchFiles(query){
  query=query.toLowerCase().trim();
  var files=window.ATHENA_SITE_DATA.files || [];
  if(!query) return files.slice(0,25).map(function(f){return Object.assign({},f,{snippet:''});});
  return files.map(function(f){
    var hay=((f.title||'')+' '+(f.path||'')+' '+(f.searchText||'')).toLowerCase();
    var hit=hay.indexOf(query);
    if(hit<0) return null;
    var text=f.searchText||'';
    var p=text.toLowerCase().indexOf(query);
    var snippet=p<0?'':text.substring(Math.max(0,p-60),Math.min(text.length,p+160));
    return Object.assign({},f,{snippet:snippet});
  }).filter(Boolean).slice(0,50);
}
function initGlobalSearch(){
  var input=byId('global-search-input');
  var results=byId('global-search-results');
  function update(){renderResults(results,searchFiles(input.value));}
  input.oninput=update;
  input.onkeydown=function(ev){
    if(ev.key==='Enter'){
      var first=results.querySelector('li');
      if(first){first.click(); ev.preventDefault();}
    }
  };
  update();
}
