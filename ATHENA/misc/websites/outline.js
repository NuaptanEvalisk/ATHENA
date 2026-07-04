var athenaOutlineHeadingCount=0;

function athenaHeadingLevel(node){
  var match=String(node.tagName||'').match(/^H([1-6])$/i);
  return match ? Number(match[1]) : 1;
}

function athenaHeadingId(node){
  if(node.id) return node.id;
  athenaOutlineHeadingCount+=1;
  node.id='athena-heading-'+athenaOutlineHeadingCount;
  return node.id;
}

function athenaOutlineEntryText(node){
  return (node.textContent||'').replace(/\s+/g,' ').trim();
}

function athenaScrollDocumentToHeading(id){
  var frame=byId('docframe');
  if(!frame || !frame.contentWindow) return;
  var doc=null;
  try{
    doc=frame.contentDocument;
  }
  catch(e){}
  if(doc){
    var target=doc.getElementById(id);
    if(target){
      try{frame.contentWindow.location.hash=id;}catch(e){}
      target.scrollIntoView({block:'start',inline:'nearest'});
      return;
    }
  }
  frame.contentWindow.postMessage({type:'athena-scroll-heading',id:id},'*');
}

function athenaRenderOutline(headings){
  var container=byId('outline-content');
  if(!container) return;
  container.innerHTML='';
  if(!headings.length){
    var empty=document.createElement('div');
    empty.className='outline-empty';
    empty.textContent='No headings in this document.';
    container.appendChild(empty);
    return;
  }
  var ul=document.createElement('ul');
  ul.className='outline-list';
  headings.forEach(function(item){
    var li=document.createElement('li');
    li.className='outline-level-'+item.level;
    var a=document.createElement('a');
    a.href='#';
    a.textContent=item.text;
    a.onclick=function(ev){
      ev.preventDefault();
      athenaScrollDocumentToHeading(item.id);
    };
    li.appendChild(a);
    ul.appendChild(li);
  });
  container.appendChild(ul);
}

function athenaRefreshOutline(){
  var frame=byId('docframe');
  if(!frame){
    athenaRenderOutline([]);
    return;
  }
  var doc=null;
  try{
    doc=frame.contentDocument;
  }
  catch(e){
    doc=null;
  }
  if(!doc){
    athenaRenderOutline([]);
    return;
  }
  athenaOutlineHeadingCount=0;
  var headings=Array.prototype.slice.call(doc.querySelectorAll('h1,h2,h3,h4,h5,h6'))
    .map(function(node){
      return {
        id:athenaHeadingId(node),
        level:athenaHeadingLevel(node),
        text:athenaOutlineEntryText(node)
      };
    })
    .filter(function(item){return item.text.length>0;});
  athenaRenderOutline(headings);
}

function athenaReceiveOutline(headings){
  athenaRenderOutline(Array.isArray(headings) ? headings : []);
}

function initOutline(){
  var frame=byId('docframe');
  if(frame) frame.addEventListener('load',function(){
    athenaRefreshOutline();
    try{
      frame.contentWindow.postMessage({type:'athena-request-outline'},'*');
    }
    catch(e){}
  });
  athenaRefreshOutline();
}
