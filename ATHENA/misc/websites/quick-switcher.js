
function quickItems(query){
  query=query.toLowerCase().trim();
  var files=(window.ATHENA_SITE_DATA.files||[]).map(function(f){
    return {title:f.stemTitle||athenaVaultBasename(f.path)||f.path,
      path:f.path,html:f.html,kind:'file'};
  });
  var namespaces=(window.ATHENA_SITE_DATA.namespaces||[]).map(function(n){
    return {title:n.name,path:n.name,html:n.homepage,kind:'namespace'};
  });
  var all=files.concat(namespaces);
  if(!query) return all.slice(0,40);
  return all.filter(function(x){
    return (x.title+' '+x.path+' '+x.kind).toLowerCase().indexOf(query)>=0;
  }).slice(0,40);
}
function initQuickSwitcher(){
  var input=byId('quick-switcher-input');
  var results=byId('quick-switcher-results');
  function update(){renderResults(results,quickItems(input.value));}
  input.oninput=update;
  input.onkeydown=function(ev){
    var items=Array.prototype.slice.call(results.querySelectorAll('li'));
    var active=results.querySelector('li.active');
    var index=items.indexOf(active);
    if(ev.key==='ArrowDown' && items.length){
      if(active) active.classList.remove('active');
      items[Math.min(items.length-1,index+1)].classList.add('active');
      ev.preventDefault();
    }
    else if(ev.key==='ArrowUp' && items.length){
      if(active) active.classList.remove('active');
      items[Math.max(0,index-1)].classList.add('active');
      ev.preventDefault();
    }
    else if(ev.key==='Enter' && active){
      active.click(); ev.preventDefault();
    }
  };
  update();
}
