(function(){
  function send(type,payload){
    if(window.parent&&window.parent!==window)
      window.parent.postMessage(Object.assign({type:type},payload),'*');
  }
  window.athenaMissingTarget=function(target){
    send('athena-missing-target',{target:String(target)});
  };
  window.athenaOpenDoc=function(path){
    send('athena-open-doc',{path:String(path)});
  };
})();
