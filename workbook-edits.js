import {calculateFormula,columnName,columnNumber} from './formulas.js?v=auto4';
const NS='http://schemas.openxmlformats.org/spreadsheetml/2006/main';
const nodes=(root,name)=>Array.from(root.getElementsByTagNameNS('*',name));
const child=(root,name)=>Array.from(root.children).find(n=>n.localName===name);
const xml=text=>{const doc=new DOMParser().parseFromString(text,'application/xml');if(nodes(doc,'parsererror').length)throw Error('原文件XML无效');return doc;};
// Patch cells in the original ZIP. Pictures, drawings, styles, row/column sizes,
// print setup and merges are never reserialized by a spreadsheet library.
export async function editWorkbook(buffer,edits=[]){
 const zip=await JSZip.loadAsync(buffer),wb=xml(await zip.file('xl/workbook.xml').async('string')),rels=xml(await zip.file('xl/_rels/workbook.xml.rels').async('string'));
 const shared=zip.file('xl/sharedStrings.xml')?nodes(xml(await zip.file('xl/sharedStrings.xml').async('string')),'si').map(si=>nodes(si,'t').map(n=>n.textContent).join('')):[],books=new Map(),warnings=[];
 for(const sheet of nodes(wb,'sheet')){const rel=nodes(rels,'Relationship').find(r=>r.getAttribute('Id')===sheet.getAttribute('r:id'));if(!rel)continue;const path=decodeURIComponent(new URL(rel.getAttribute('Target'),'https://xlsx/xl/workbook.xml').pathname.slice(1)),doc=xml(await zip.file(path).async('string'));books.set(sheet.getAttribute('name'),{doc,path,cells:new Map(nodes(doc,'c').map(c=>[c.getAttribute('r'),c]))});}
 const make=(doc,name)=>doc.createElementNS(NS,name);
 const textValue=cell=>{if(!cell)return '';if(cell.getAttribute('t')==='s')return shared[Number(child(cell,'v')?.textContent)]||'';if(cell.getAttribute('t')==='inlineStr')return nodes(cell,'t').map(n=>n.textContent).join('');return child(cell,'v')?.textContent??'';};
 // Shared formulas must be expanded before replacing any group master.
 for(const book of books.values()){
  const masters=new Map([...book.cells.values()].filter(c=>child(c,'f')?.getAttribute('t')==='shared'&&child(c,'f').textContent).map(c=>[child(c,'f').getAttribute('si'),c]));
  for(const cell of book.cells.values()){const f=child(cell,'f');if(f?.getAttribute('t')!=='shared')continue;const master=masters.get(f.getAttribute('si'));if(!master)continue;
   if(!f.textContent){const a=master.getAttribute('r'),b=cell.getAttribute('r'),dc=columnNumber(b)-columnNumber(a),dr=Number(b.match(/\d+/)[0])-Number(a.match(/\d+/)[0]);f.textContent=child(master,'f').textContent.split(/("(?:[^"]|"")*")/).map((part,i)=>i%2?part:part.replace(/((?:'(?:[^']|'')+'|[A-Za-z_][A-Za-z0-9_]*)!)?(\$?)([A-Z]+)(\$?)(\d+)/g,(_,prefix,absCol,col,absRow,row)=>(prefix||'')+absCol+columnName(columnNumber(col)+(absCol?0:dc))+absRow+(Number(row)+(absRow?0:dr)))).join('');}
  }
  for(const cell of book.cells.values()){const f=child(cell,'f');if(f?.getAttribute('t')==='shared'&&f.textContent)for(const attr of ['t','si','ref'])f.removeAttribute(attr);}
 }
 for(const edit of edits){const book=books.get(edit.sheet);if(!book||!/^[A-Z]+[1-9]\d*$/.test(edit.address))throw Error('原表编辑位置无效');let cell=book.cells.get(edit.address);
  if(!cell){const number=Number(edit.address.match(/\d+/)[0]),data=nodes(book.doc,'sheetData')[0];let row=Array.from(data.children).find(n=>Number(n.getAttribute('r'))===number);if(!row){row=make(book.doc,'row');row.setAttribute('r',number);data.insertBefore(row,Array.from(data.children).find(n=>Number(n.getAttribute('r'))>number)||null);}cell=make(book.doc,'c');cell.setAttribute('r',edit.address);row.insertBefore(cell,Array.from(row.children).find(n=>n.localName==='c'&&columnNumber(n.getAttribute('r'))>columnNumber(edit.address))||null);book.cells.set(edit.address,cell);}
  for(const node of Array.from(cell.children))if(['v','f','is'].includes(node.localName))node.remove();cell.removeAttribute('t');
  if(edit.formula){const f=make(book.doc,'f');f.textContent=edit.formula;cell.append(f);}else if(edit.value!==''&&edit.value!==null&&edit.value!==undefined){if(typeof edit.value==='number'){if(!Number.isFinite(edit.value))throw Error('单元格数值无效');const v=make(book.doc,'v');v.textContent=String(edit.value);cell.append(v);}else{cell.setAttribute('t','inlineStr');const is=make(book.doc,'is'),t=make(book.doc,'t');t.setAttribute('xml:space','preserve');t.textContent=String(edit.value);is.append(t);cell.append(is);}}
 }
 const cache=new Map(),busy=new Set(),cellValues={};
 function read(sheet,address){const key=sheet+'!'+address;if(cache.has(key))return cache.get(key);if(busy.has(key))throw Error('循环引用');const cell=books.get(sheet)?.cells.get(address);if(!cell)return '';const f=child(cell,'f');if(!f){const text=textValue(cell);return ['s','str','inlineStr','e'].includes(cell.getAttribute('t'))?text:text!==''&&Number.isFinite(Number(text))?Number(text):text;}busy.add(key);try{const value=calculateFormula(f.textContent,sheet,read);cache.set(key,value);return value;}finally{busy.delete(key);}}
 for(const [name,book]of books){cellValues[name]={};for(const [address,cell]of book.cells){try{const value=read(name,address);cellValues[name][address]=value;if(child(cell,'f')){child(cell,'v')?.remove();cell.removeAttribute('t');if(value!==null&&value!==''){const v=make(book.doc,'v');v.textContent=typeof value==='boolean'?Number(value):String(value);cell.append(v);if(typeof value==='string')cell.setAttribute('t','str');else if(typeof value==='boolean')cell.setAttribute('t','b');}}}catch{child(cell,'v')?.remove();cellValues[name][address]=null;warnings.push(name+'!'+address+'需Excel重算');}}
  zip.file(book.path,new XMLSerializer().serializeToString(book.doc));
 }
 let calc=nodes(wb,'calcPr')[0];if(!calc){calc=make(wb,'calcPr');const after=Array.from(wb.documentElement.children).find(node=>['oleSize','customWorkbookViews','pivotCaches','smartTagPr','smartTagTypes','webPublishing','fileRecoveryPr','webPublishObjects','extLst'].includes(node.localName));wb.documentElement.insertBefore(calc,after||null);}calc.setAttribute('calcMode','auto');calc.setAttribute('fullCalcOnLoad','1');calc.setAttribute('forceFullCalc','1');
 for(const rel of nodes(rels,'Relationship'))if(rel.getAttribute('Type').endsWith('/calcChain')){zip.remove(new URL(rel.getAttribute('Target'),'https://xlsx/xl/workbook.xml').pathname.slice(1));rel.remove();}
 const types=xml(await zip.file('[Content_Types].xml').async('string'));for(const node of nodes(types,'Override'))if(node.getAttribute('PartName').endsWith('/calcChain.xml'))node.remove();
 zip.file('xl/workbook.xml',new XMLSerializer().serializeToString(wb));zip.file('xl/_rels/workbook.xml.rels',new XMLSerializer().serializeToString(rels));zip.file('[Content_Types].xml',new XMLSerializer().serializeToString(types));
 return {buffer:await zip.generateAsync({type:'arraybuffer'}),cellValues,warnings};
}
