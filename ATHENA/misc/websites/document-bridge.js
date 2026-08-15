(function(){
  function hasShellParent(){
    return window.parent&&window.parent!==window;
  }
  function send(type,payload){
    if(hasShellParent())
      window.parent.postMessage(Object.assign({type:type},payload),'*');
  }
  function ensureStandaloneModal(){
    var existing=document.getElementById('athena-standalone-missing-modal');
    if(existing) return existing;
    var modal=document.createElement('div');
    modal.id='athena-standalone-missing-modal';
    modal.setAttribute('role','dialog');
    modal.setAttribute('aria-modal','true');
    modal.style.cssText='display:none;position:fixed;left:50%;top:20%;transform:translateX(-50%);z-index:2147483647;min-width:320px;max-width:560px;background:#f0f0f0;color:#000;border:2px outset #c0c0c0;box-shadow:4px 4px 0 rgba(0,0,0,.35);font:14px sans-serif;';
    modal.innerHTML='<div style="background:#000080;color:#fff;padding:3px 8px;font-weight:bold">ATHENA</div><div style="padding:14px 16px"><p id="athena-standalone-missing-text" style="margin:0 0 14px 0"></p><button id="athena-standalone-missing-ok" type="button" style="float:right;min-width:72px">OK</button><div style="clear:both"></div></div>';
    document.body.appendChild(modal);
    document.getElementById('athena-standalone-missing-ok').onclick=function(){
      modal.style.display='none';
    };
    return modal;
  }
  function showStandaloneMissing(target){
    var modal=ensureStandaloneModal();
    var text=document.getElementById('athena-standalone-missing-text');
    if(text)
      text.textContent='Destination is not in the exported site: '+String(target);
    modal.style.display='block';
  }
  function encodePathSegment(segment){
    try{return encodeURIComponent(decodeURIComponent(segment));}
    catch(e){return encodeURIComponent(segment);}
  }
  function encodePath(path){
    return String(path).split('/').map(encodePathSegment).join('/');
  }
  function currentSitePrefix(rootRelativePath){
    var first=String(rootRelativePath).split('/')[0]||'';
    if(!first) return '/';
    var marker='/'+encodePathSegment(first)+'/';
    var pathname=window.location.pathname;
    var index=pathname.indexOf(marker);
    return index>=0 ? pathname.substring(0,index+1) : '/';
  }
  function standaloneDocUrl(path){
    path=String(path||'');
    if(!path||path==='about:blank'||/^[a-z][a-z0-9+.-]*:/i.test(path)||
       path.charAt(0)==='#')
      return path;
    var hash='';
    var hashIndex=path.indexOf('#');
    if(hashIndex>=0){
      hash=path.substring(hashIndex);
      path=path.substring(0,hashIndex);
    }
    var absolute=path.charAt(0)==='/';
    var encoded=encodePath(absolute ? path.substring(1) : path);
    if(/^https?:$/i.test(window.location.protocol)&&/\.html$/i.test(encoded))
      encoded=encoded.substring(0,encoded.length-5);
    var prefix=absolute ? '/' : currentSitePrefix(path);
    return prefix+encoded+hash;
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
  function prepareExternalWebLinks(){
    Array.prototype.forEach.call(document.querySelectorAll('a[href]'),function(link){
      var href=String(link.getAttribute('href')||'').trim();
      if(!/^(?:https?:)?\/\//i.test(href)) return;
      link.setAttribute('target','_blank');
      var rel=String(link.getAttribute('rel')||'').split(/\s+/)
        .filter(function(value){return value.length>0;});
      ['noopener','noreferrer'].forEach(function(value){
        if(rel.indexOf(value)<0) rel.push(value);
      });
      link.setAttribute('rel',rel.join(' '));
    });
  }
  function initializeDocumentBridge(){
    prepareExternalWebLinks();
    if(window.ATHENA_DOCUMENT_PDF){
      var download=document.createElement('a');
      download.className='athena-standalone-pdf-download';
      download.href=String(window.ATHENA_DOCUMENT_PDF);
      download.download='';
      download.textContent='PDF';
      download.setAttribute('aria-label','Download document PDF');
      document.body.appendChild(download);
    }
    sendOutline();
  }
  window.athenaMissingTarget=function(target){
    if(hasShellParent()) send('athena-missing-target',{target:String(target)});
    else showStandaloneMissing(target);
  };
  window.athenaOpenDoc=function(path){
    path=String(path);
    if(hasShellParent()) send('athena-open-doc',{path:path});
    else if(path&&path!=='about:blank') window.location.href=standaloneDocUrl(path);
  };
  window.addEventListener('message',function(ev){
    var data=ev.data||{};
    if(data.type==='athena-scroll-heading') scrollHeading(data.id);
    else if(data.type==='athena-request-outline') sendOutline();
  });
  if(document.readyState==='loading')
    document.addEventListener('DOMContentLoaded',initializeDocumentBridge);
  else initializeDocumentBridge();
  window.addEventListener('load',sendOutline);
  setTimeout(sendOutline,250);
})();
