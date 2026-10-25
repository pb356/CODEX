// Separate iframe realms isolate uploaded files, rows, pending requests, DOM,
// settings and object URLs. Both price stores use separate keys in the child.
const modes=['outdoor','indoor'];
function switchMode(mode){
 if(!modes.includes(mode))mode='outdoor';
 for(const name of modes){
  const selected=name===mode,button=document.getElementById('mode-'+name),panel=document.getElementById('panel-'+name),frame=document.getElementById('frame-'+name);
  button.setAttribute('aria-selected',String(selected));button.tabIndex=selected?0:-1;panel.hidden=!selected;
  if(selected){if(!frame.getAttribute('src'))frame.src='workspace.html?mode='+name+'&v=catalog9';else frame.contentWindow.postMessage({type:'plant-workspace-measure'},location.origin);}
 }
 document.getElementById('mode-hint').textContent=(mode==='indoor'?'室内租赁':'室外绿化')+'工作区：价格库、清单、计价设置与下载结果分别隔离，切换会保留当前进度。';
 history.replaceState(null,'','#'+mode);
}
for(const name of modes){
 const button=document.getElementById('mode-'+name);button.onclick=()=>switchMode(name);
 button.onkeydown=event=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){event.preventDefault();const target=event.key==='Home'?'outdoor':event.key==='End'?'indoor':modes[1-modes.indexOf(name)];switchMode(target);document.getElementById('mode-'+target).focus();}};
}
window.addEventListener('hashchange',()=>switchMode(location.hash.slice(1)));
window.addEventListener('message',event=>{
 if(event.origin!==location.origin||event.data?.type!=='plant-workspace-height')return;
 for(const name of modes){const frame=document.getElementById('frame-'+name);if(event.source===frame.contentWindow){const height=Math.max(800,Math.min(50000,Number(event.data.height)||800));if(Math.abs((parseInt(frame.style.height)||0)-height)>2)frame.style.height=height+'px';}}
});
switchMode(location.hash.slice(1)||'outdoor');
