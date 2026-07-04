(function(){
  function send(type,payload){
    if(window.parent&&window.parent!==window)
      window.parent.postMessage(Object.assign({type:type},payload),'*');
  }
  var headingCounter=0;
  function headingText(node){
    return (node.textContent||'').replace(/\s+/g,' ').trim();
  }
  function headingId(node){
    if(node.id) return node.id;
    headingCounter+=1;
    node.id='athena-heading-'+headingCounter;
    return node.id;
  }
  function headingLevel(node){
    var match=String(node.tagName||'').match(/^H([1-6])$/i);
    return match ? Number(match[1]) : 1;
  }
  function sendOutline(){
    headingCounter=0;
    var headings=Array.prototype.slice.call(document.querySelectorAll('h1,h2,h3,h4,h5,h6'))
      .map(function(node){
        return {id:headingId(node),level:headingLevel(node),text:headingText(node)};
      })
      .filter(function(item){return item.text.length>0;});
    send('athena-outline',{href:String(location.href),headings:headings});
  }
  function scrollHeading(id){
    var target=document.getElementById(String(id||''));
    if(!target) return;
    try{location.hash=target.id;}catch(e){}
    target.scrollIntoView({block:'start',inline:'nearest'});
  }
  window.athenaMissingTarget=function(target){
    send('athena-missing-target',{target:String(target)});
  };
  window.athenaOpenDoc=function(path){
    send('athena-open-doc',{path:String(path)});
  };
  window.addEventListener('message',function(ev){
    var data=ev.data||{};
    if(data.type==='athena-scroll-heading') scrollHeading(data.id);
    else if(data.type==='athena-request-outline') sendOutline();
  });
  if(document.readyState==='loading')
    document.addEventListener('DOMContentLoaded',sendOutline);
  else sendOutline();
  window.addEventListener('load',sendOutline);
  setTimeout(sendOutline,250);
})();
