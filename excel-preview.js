// A browser view of the original workbook. Export still patches the source ZIP;
// rendering never converts or resaves drawings, styles, or worksheet structure.
const el=(tag,cls)=>{const node=document.createElement(tag);if(cls)node.className=cls;return node;};
const nodes=(root,name)=>Array.from(root.getElementsByTagNameNS('*',name));
const first=(root,name)=>nodes(root,name)[0];
const xml=text=>new DOMParser().parseFromString(text,'application/xml');
const resolve=(base,target)=>decodeURIComponent(new URL(target,'https://xlsx/'+base).pathname.slice(1));
const relPath=path=>path.replace(/([^/]+)$/, '_rels/$1.rels');
const colName=n=>{let out='';for(;n;n=Math.floor((n-1)/26))out=String.fromCharCode(65+(n-1)%26)+out;return out;};
const colNumber=s=>[...s.replace(/\d/g,'')].reduce((n,c)=>n*26+c.charCodeAt(0)-64,0);
const plain=value=>value===null||value===undefined?'':typeof value==='object'?(value.richText?value.richText.map(r=>r.text).join(''):value.text??value.result??''):value;
const pixels=width=>width<1?Math.floor(width*12+.5):Math.floor(width*7+5);
const borderWidths={hair:.5,thin:1,medium:2,thick:3,double:3,dotted:1,dashed:1,mediumDashed:2,dashDot:1,mediumDashDot:2,dashDotDot:1,mediumDashDotDot:2,slantDashDot:2};

export async function createExcelPreview(host,buffer,{onEdit,onAdd,actionState,context={},showActions=true}={}){
 const zip=await JSZip.loadAsync(buffer),workbook=new ExcelJS.Workbook();await workbook.xlsx.load(buffer);
 // Only supply columns that genuinely do not exist in the source, using the
 // same positions as original-format export and copying the neighbouring style.
 context.previewColumns={};
 for(const layout of context.layouts||[]){const sheet=workbook.getWorksheet(layout.sheet);if(!sheet)continue;const columns={...layout.columns};let last=sheet.columnCount;
  for(const field of ['price','amount'])if(columns[field]===undefined){const col=++last;columns[field]=col-1;sheet.getColumn(col).width=16;for(let r=layout.headerRow;r<=sheet.rowCount;r++)sheet.getCell(r,col).style=JSON.parse(JSON.stringify(sheet.getCell(r,Math.max(1,col-1)).style));sheet.getCell(layout.headerRow,col).value=field==='price'?'单价':'金额';}
  context.previewColumns[layout.sheet]=columns;
 }
 const themeFile=zip.file('xl/theme/theme1.xml'),theme=themeFile?first(xml(await themeFile.async('string')),'clrScheme'):null;
 // Excel theme indices are light1, dark1, light2, dark2, then accents.
 const scheme=theme?Array.from(theme.children).map(node=>first(node,'srgbClr')?.getAttribute('val')||first(node,'sysClr')?.getAttribute('lastClr')||'000000'):[];
 const colors=[scheme[1]||'FFFFFF',scheme[0]||'000000',scheme[3]||'EEECE1',scheme[2]||'1F497D',...scheme.slice(4)];
 function color(value,fallback='#000000'){
  if(!value)return fallback;let rgb=value.argb?.slice(-6)||colors[value.theme];
  if(!rgb&&value.indexed!==undefined)rgb=['000000','FFFFFF','FF0000','00FF00','0000FF','FFFF00','FF00FF','00FFFF'][value.indexed%8];
  if(!rgb)return fallback;
  if(value.tint){rgb=rgb.match(/../g).map(c=>{const n=parseInt(c,16),t=value.tint;return Math.round(t<0?n*(1+t):n+(255-n)*t).toString(16).padStart(2,'0');}).join('');}
  return '#'+rgb;
 }
 function setFont(node,font={}){
  node.style.fontFamily='"'+(font.name||'Calibri').replace(/"/g,'')+'", "Microsoft YaHei", sans-serif';node.style.fontSize=(font.size||11)+'pt';node.style.fontWeight=font.bold?'bold':'normal';node.style.fontStyle=font.italic?'italic':'normal';node.style.color=color(font.color);
  node.style.textDecoration=[font.underline?'underline':'',font.strike?'line-through':''].filter(Boolean).join(' ');
 }
 function format(value,cell){
  if(value===null||value===undefined)return '';
  if(value instanceof Date){value=(value.getTime()-Date.UTC(1899,11,30))/86400000+(workbook.properties.date1904?-1462:0);}
  try{if(typeof value==='number'&&window.SSF)return SSF.format(cell.numFmt||'General',value,{date1904:!!workbook.properties.date1904});}catch{}
  return String(value);
 }
 const blobURLs=new Map(),sheets=new Map();let disposed=false,activeName;
 host.replaceChildren();const tabs=el('div','excel-tabs'),scroller=el('div','excel-scroll');scroller.tabIndex=0;scroller.setAttribute('aria-label','原表格预览，可横向和纵向滚动');host.append(tabs,scroller);
 const wbDoc=xml(await zip.file('xl/workbook.xml').async('string')),wbRels=xml(await zip.file('xl/_rels/workbook.xml.rels').async('string'));
 async function relationships(path){const file=zip.file(relPath(path));return file?new Map(nodes(xml(await file.async('string')),'Relationship').map(r=>[r.getAttribute('Id'),resolve(path,r.getAttribute('Target'))])):new Map();}
 async function imageURL(path){if(blobURLs.has(path))return blobURLs.get(path);const file=zip.file(path);if(!file)return null;const ext=path.split('.').pop().toLowerCase(),type=({png:'image/png',jpg:'image/jpeg',jpeg:'image/jpeg',gif:'image/gif',svg:'image/svg+xml',bmp:'image/bmp',webp:'image/webp'})[ext]||'application/octet-stream';const url=URL.createObjectURL(new Blob([await file.async('uint8array')],{type}));blobURLs.set(path,url);return url;}
 for(const sheet of workbook.worksheets){
  if(sheet.state==='veryHidden'||sheet.state==='hidden')continue;
  const record=nodes(wbDoc,'sheet').find(n=>n.getAttribute('name')===sheet.name),rid=record?.getAttribute('r:id');
  const relation=nodes(wbRels,'Relationship').find(n=>n.getAttribute('Id')===rid);if(!relation)continue;
  const path=resolve('xl/workbook.xml',relation.getAttribute('Target')),sheetDoc=xml(await zip.file(path).async('string'));
  const rawRows=new Map(nodes(sheetDoc,'row').map(n=>[Number(n.getAttribute('r')),n]));
  const rawCols=nodes(sheetDoc,'col'),defaultRow=Number(first(sheetDoc,'sheetFormatPr')?.getAttribute('defaultRowHeight')||15);
  const countCols=Math.max(sheet.columnCount,1),countRows=Math.max(sheet.rowCount,1),xs=[0],ys=[0];if(countCols*countRows>150000)throw Error('原表范围过大，请先在Excel中删除多余空白行列后重新导入。');
  for(let c=1;c<=countCols;c++){const raw=rawCols.find(n=>Number(n.getAttribute('min'))<=c&&Number(n.getAttribute('max'))>=c);xs.push(xs.at(-1)+(sheet.getColumn(c).hidden?0:pixels(Number(raw?.getAttribute('width')||sheet.getColumn(c).width||sheet.properties.defaultColWidth||8.43))));}
  for(let r=1;r<=countRows;r++){const raw=rawRows.get(r);ys.push(ys.at(-1)+(sheet.getRow(r).hidden?0:Number(raw?.getAttribute('ht')||defaultRow)*96/72));}
  const canvas=el('div','excel-canvas');canvas.dataset.sheet=sheet.name;canvas.style.width=xs.at(-1)+(showActions?176:0)+'px';canvas.style.height=ys.at(-1)+'px';canvas.style.setProperty('--sheet-width',xs.at(-1)+'px');
  const cells=new Map(),buttons=new Map(),merged=new Map(),covered=new Set();
  for(const range of sheet.model.merges||[]){const [a,b=a]=range.split(':'),c1=colNumber(a),r1=Number(a.match(/\d+/)[0]),c2=colNumber(b),r2=Number(b.match(/\d+/)[0]);merged.set(a,{c2,r2});for(let r=r1;r<=r2;r++)for(let c=c1;c<=c2;c++)if(r!==r1||c!==c1)covered.add(colName(c)+r);}
  for(let r=1;r<=countRows;r++)for(let c=1;c<=countCols;c++){
   const address=colName(c)+r;if(covered.has(address)||xs[c]===xs[c-1]||ys[r]===ys[r-1])continue;
   const cell=sheet.getCell(address),end=merged.get(address)||{c2:c,r2:r},outer=el('div','excel-cell'),input=el('div','excel-value');
   outer.dataset.address=address;outer.dataset.sheet=sheet.name;if(merged.has(address))outer.dataset.merged='true';
   Object.assign(outer.style,{left:xs[c-1]+'px',top:ys[r-1]+'px',width:xs[end.c2]-xs[c-1]+'px',height:ys[end.r2]-ys[r-1]+'px'});
   setFont(input,cell.font);const align=cell.alignment||{};outer.style.alignItems=align.vertical==='top'?'flex-start':align.vertical==='bottom'?'flex-end':'center';
   const sourceValue=plain(cell.value);input.style.textAlign=align.horizontal==='centerContinuous'?'center':align.horizontal||((typeof sourceValue==='number'||sourceValue instanceof Date)?'right':'left');input.style.whiteSpace=align.wrapText?'pre-wrap':'pre';if(align.indent)input.style.paddingLeft=align.indent*10+'px';
   if(align.textRotation&&Number.isFinite(Number(align.textRotation)))input.style.transform=`rotate(${-align.textRotation}deg)`;
   if(cell.fill?.type==='pattern'&&cell.fill.pattern==='solid')outer.style.background=color(cell.fill.fgColor,'#fff');
   for(const side of ['top','bottom','left','right']){const edge=side==='right'?sheet.getCell(r,end.c2):side==='bottom'?sheet.getCell(end.r2,c):cell;const b=edge.border?.[side]||cell.border?.[side];if(b?.style)outer.style['border'+side[0].toUpperCase()+side.slice(1)]=(borderWidths[b.style]||1)+'px '+(b.style==='double'?'double':/dash/i.test(b.style)?'dashed':b.style==='dotted'?'dotted':'solid')+' '+color(b.color);}
   input.contentEditable='true';input.spellcheck=false;input.setAttribute('role','textbox');input.setAttribute('aria-label',sheet.name+' '+address);input.dataset.address=address;input.dataset.sheet=sheet.name;
   let current=sourceValue,editing=false,startValue='';
   function display(value){current=value;if(editing)return;input.replaceChildren();if(cell.value?.richText&&String(value)===String(sourceValue)){for(const run of cell.value.richText){const span=el('span');span.textContent=run.text;setFont(span,{...cell.font,...run.font});input.append(span);}}else input.textContent=format(value,cell);}
   input.onfocus=()=>{editing=true;startValue=current===null?'':String(current instanceof Date?format(current,cell):current??'');input.textContent=startValue;outer.classList.add('editing');};
   input.onblur=()=>{const text=input.textContent.replace(/\u00a0/g,' ');editing=false;outer.classList.remove('editing');if(text!==startValue){try{onEdit?.({sheet:sheet.name,address,text,cell});}catch(error){display(current);throw error;}}else display(current);};
   input.onkeydown=event=>{if(event.key==='Escape'){event.preventDefault();input.textContent=startValue;input.blur();}if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();input.blur();}};
   input.onpaste=event=>{event.preventDefault();const text=event.clipboardData.getData('text/plain'),selection=window.getSelection();if(!selection.rangeCount)return;const range=selection.getRangeAt(0);range.deleteContents();const fragment=document.createTextNode(text);range.insertNode(fragment);range.setStartAfter(fragment);range.collapse(true);selection.removeAllRanges();selection.addRange(range);};
   display(sourceValue);outer.append(input);canvas.append(outer);cells.set(address,{display,sourceValue,cell,input});
  }
  const sheetRelations=await relationships(path);
  for(const drawingRef of nodes(sheetDoc,'drawing')){
   const drawingPath=sheetRelations.get(drawingRef.getAttribute('r:id'));if(!drawingPath||!zip.file(drawingPath))continue;
   const drawing=xml(await zip.file(drawingPath).async('string')),drawingRels=await relationships(drawingPath);
   const coordinate=(anchor,name)=>{const node=Array.from(anchor.children).find(n=>n.localName===name);if(!node)return null;const col=Number(first(node,'col')?.textContent||0),row=Number(first(node,'row')?.textContent||0);return {x:(xs[col]??xs.at(-1))+Number(first(node,'colOff')?.textContent||0)/9525,y:(ys[row]??ys.at(-1))+Number(first(node,'rowOff')?.textContent||0)/9525};};
   for(const anchor of Array.from(drawing.documentElement.children))for(const pic of nodes(anchor,'pic')){
    const blip=first(pic,'blip'),media=drawingRels.get(blip?.getAttribute('r:embed'));if(!media)continue;const url=await imageURL(media);if(!url)continue;
    const from=coordinate(anchor,'from'),to=coordinate(anchor,'to'),ext=Array.from(anchor.children).find(n=>n.localName==='ext'),pos=Array.from(anchor.children).find(n=>n.localName==='pos');
    const x=from?.x??Number(pos?.getAttribute('x')||0)/9525,y=from?.y??Number(pos?.getAttribute('y')||0)/9525,w=to?to.x-x:Number(ext?.getAttribute('cx')||0)/9525,h=to?to.y-y:Number(ext?.getAttribute('cy')||0)/9525;if(w<=0||h<=0)continue;
    const frame=el('div','excel-picture'),img=el('img');frame.dataset.media=media;Object.assign(frame.style,{left:x+'px',top:y+'px',width:w+'px',height:h+'px'});img.src=url;img.alt=first(pic,'cNvPr')?.getAttribute('descr')||'原表图片';img.draggable=false;
    const crop=first(pic,'srcRect'),l=Number(crop?.getAttribute('l')||0)/100000,t=Number(crop?.getAttribute('t')||0)/100000,rr=Number(crop?.getAttribute('r')||0)/100000,b=Number(crop?.getAttribute('b')||0)/100000;
    Object.assign(img.style,{width:100/Math.max(.001,1-l-rr)+'%',height:100/Math.max(.001,1-t-b)+'%',left:-100*l/Math.max(.001,1-l-rr)+'%',top:-100*t/Math.max(.001,1-t-b)+'%'});
    const xfrm=first(pic,'xfrm'),rotation=Number(xfrm?.getAttribute('rot')||0)/60000;if(rotation)frame.style.transform=`rotate(${rotation}deg)`;img.style.transform=`scale(${xfrm?.getAttribute('flipH')==='1'?-1:1},${xfrm?.getAttribute('flipV')==='1'?-1:1})`;
    frame.append(img);canvas.append(frame);
   }
  }
  let contentColumn=1;sheet.eachRow(row=>row.eachCell(cell=>{if(plain(cell.value)!=='')contentColumn=Math.max(contentColumn,cell.col);}));const actionX=xs[contentColumn]??xs.at(-1);
  const state={sheet,canvas,cells,buttons,xs,ys,actionX};sheets.set(sheet.name,state);
  const tab=el('button','secondary');tab.textContent=sheet.name;tab.onclick=()=>activate(sheet.name);tabs.append(tab);state.tab=tab;
 }
 function activate(name){activeName=name;for(const [key,state]of sheets){state.canvas.hidden=key!==name;state.tab.setAttribute('aria-selected',String(key===name));}scroller.scrollTop=0;scroller.scrollLeft=0;}
 for(const state of sheets.values())scroller.append(state.canvas);tabs.hidden=sheets.size<2;activate(sheets.keys().next().value);
 function update(rows,cellValues={}){
  if(disposed)return;
  for(const [name,state]of sheets){
   const values=cellValues[name]||{};for(const [address,view]of state.cells)view.display(Object.hasOwn(values,address)?values[address]:view.sourceValue);
   const wanted=new Set();for(const row of (showActions?rows:[]).filter(r=>r._origin?.sheet===name)){
    const number=row._origin.row;wanted.add(number);const priceColumn=row._origin.columns.price;if(priceColumn!==undefined){const view=state.cells.get(colName(priceColumn+1)+number);if(view)view.input.title=[row.status,row.source,row.quoteCount>1?row.quoteCount+'条报价，平均'+row.averagePrice+'元':'',row.matchedSpec?'采用规格：'+row.matchedSpec:''].filter(Boolean).join('；');}let button=state.buttons.get(number);if(!button){button=el('button','excel-bank secondary');button.style.left=state.actionX+10+'px';button.style.top=(state.ys[number-1]||0)+2+'px';button.style.height=Math.max(16,Math.min(28,state.ys[number]-state.ys[number-1]-4))+'px';button.dataset.row=number;button.dataset.sheet=name;state.canvas.append(button);state.buttons.set(number,button);}
    const action=actionState?.(row)||{};button.textContent=action.label||'一键加入新品库';button.disabled=!!action.disabled;button.title=action.title||row.name;button.onclick=()=>onAdd?.(row);
   }
   for(const [number,button]of state.buttons)if(!wanted.has(number)){button.remove();state.buttons.delete(number);}
  }
 }
 return {update,dispose(){disposed=true;for(const url of blobURLs.values())URL.revokeObjectURL(url);host.replaceChildren();},get activeSheet(){return activeName;}};
}
