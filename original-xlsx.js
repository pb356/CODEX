import {calculateFormula,columnName,columnNumber} from './formulas.js?v=auto4';
const NS='http://schemas.openxmlformats.org/spreadsheetml/2006/main';
const REL='http://schemas.openxmlformats.org/package/2006/relationships';
const esc=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
const xml=text=>{const doc=new DOMParser().parseFromString(text,'application/xml');if(doc.getElementsByTagName('parsererror').length)throw Error('Excel内部XML格式无效');return doc;};
const nodes=(root,name)=>Array.from(root.getElementsByTagNameNS('*',name));
const child=(root,name)=>Array.from(root.children).find(node=>node.localName===name);
const serialize=doc=>new XMLSerializer().serializeToString(doc);
const number=value=>{const text=String(value??'').replace(/[,￥¥]/g,'').trim();return text!==''&&Number.isFinite(Number(text))?Number(text):null;};
const money=value=>Math.round((value+Number.EPSILON)*100)/100;
export async function fillOriginalXlsx(buffer,rows,context,{taxMode='original',taxRate=null,discountMode='preserve'}={}){
 if(rows.some(row=>!row._origin))throw Error('新增行没有原表位置，请先在原Excel中添加对应行，或选择“标准表”导出。');
 if(taxMode==='add'&&(taxRate===null||!Number.isFinite(taxRate)||taxRate<0||taxRate>100))throw Error('请指定有效税率。');
 const zip=await JSZip.loadAsync(buffer),warnings=[],workbook=xml(await zip.file('xl/workbook.xml').async('string'));
 const relationships=xml(await zip.file('xl/_rels/workbook.xml.rels').async('string'));
 const shared=zip.file('xl/sharedStrings.xml')?nodes(xml(await zip.file('xl/sharedStrings.xml').async('string')),'si').map(si=>nodes(si,'t').map(t=>t.textContent).join('')):[];
 const stylesDoc=xml(await zip.file('xl/styles.xml').async('string'));
 const customFormats=new Map(nodes(stylesDoc,'numFmt').map(node=>[Number(node.getAttribute('numFmtId')),node.getAttribute('formatCode')]));
 const cellFormats=nodes(stylesDoc,'cellXfs')[0]?Array.from(nodes(stylesDoc,'cellXfs')[0].children):[];
 const percentageStyle=cell=>{const format=Number(cellFormats[Number(cell?.getAttribute('s')||0)]?.getAttribute('numFmtId'));return format===9||format===10||/%/.test(customFormats.get(format)||'');};
 const books=new Map();
 for(const sheet of nodes(workbook,'sheet')){
  const id=sheet.getAttributeNS('http://schemas.openxmlformats.org/officeDocument/2006/relationships','id')||sheet.getAttribute('r:id');
  const relation=nodes(relationships,'Relationship').find(r=>r.getAttribute('Id')===id);
  const path=new URL(relation.getAttribute('Target'),'https://xlsx/xl/workbook.xml').pathname.slice(1);
  const doc=xml(await zip.file(path).async('string')),cells=new Map(nodes(doc,'c').map(cell=>[cell.getAttribute('r'),cell]));
  books.set(sheet.getAttribute('name'),{doc,path,cells,changed:false});
 }
 const make=(doc,name)=>doc.createElementNS(NS,name);
 const textValue=cell=>{if(!cell)return '';const value=child(cell,'v');if(cell.getAttribute('t')==='s')return shared[Number(value?.textContent)]||'';if(cell.getAttribute('t')==='inlineStr')return nodes(cell,'t').map(t=>t.textContent).join('');return value?.textContent??'';};
 function getCell(book,address,style){
  if(book.cells.has(address))return book.cells.get(address);
  const rowNumber=Number(address.match(/\d+/)[0]),data=nodes(book.doc,'sheetData')[0];
  let row=Array.from(data.children).find(node=>Number(node.getAttribute('r'))===rowNumber);
  if(!row){row=make(book.doc,'row');row.setAttribute('r',rowNumber);const next=Array.from(data.children).find(node=>Number(node.getAttribute('r'))>rowNumber);data.insertBefore(row,next||null);}
  const cell=make(book.doc,'c');cell.setAttribute('r',address);if(style)cell.setAttribute('s',style);
  const next=Array.from(row.children).find(node=>node.localName==='c'&&columnNumber(node.getAttribute('r'))>columnNumber(address));row.insertBefore(cell,next||null);book.cells.set(address,cell);return cell;
 }
 function write(book,address,value,{formula=null,style=null}={}){
  const cell=getCell(book,address,style);for(const node of Array.from(cell.children))if(['f','v','is'].includes(node.localName))cell.removeChild(node);
  cell.removeAttribute('t');
  if(formula!==null){const f=make(book.doc,'f');f.textContent=formula;cell.append(f);}
  if(value!==''&&value!==null&&value!==undefined){if(typeof value==='number'){const v=make(book.doc,'v');v.textContent=String(value);cell.append(v);}else{cell.setAttribute('t','inlineStr');const is=make(book.doc,'is'),t=make(book.doc,'t');t.textContent=String(value);is.append(t);cell.append(is);}}
  book.changed=true;return cell;
 }
 function formulaFor(book,cell){
  const f=child(cell,'f');if(!f)return null;if(f.textContent)return f.textContent;
  if(f.getAttribute('t')!=='shared')return null;
  const master=Array.from(book.cells.values()).find(c=>{const candidate=child(c,'f');return candidate&&candidate.getAttribute('si')===f.getAttribute('si')&&candidate.textContent;});
  if(!master)return null;
  const original=master.getAttribute('r'),current=cell.getAttribute('r'),dr=Number(current.match(/\d+/)[0])-Number(original.match(/\d+/)[0]),dc=columnNumber(current)-columnNumber(original);
  return child(master,'f').textContent.split(/("(?:[^"]|"")*")/).map((part,index)=>index%2?part:part.replace(/((?:'(?:[^']|'')+'|[A-Za-z_][A-Za-z0-9_]*)!)?(\$?)([A-Z]+)(\$?)(\d+)/g,(_,prefix,absCol,col,absRow,row)=>(prefix||'')+absCol+columnName(columnNumber(col)+(absCol?0:dc))+absRow+(Number(row)+(absRow?0:dr)))).join('');
 }
 // Expand shared formula groups before replacing their master cells, retaining
 // every row's original relative references and discounts.
 for(const book of books.values()){
  const sharedFormulas=Array.from(book.cells.values()).filter(cell=>child(cell,'f')?.getAttribute('t')==='shared').map(cell=>({cell,formula:formulaFor(book,cell)}));
  for(const {cell,formula} of sharedFormulas)if(formula){const f=child(cell,'f');f.textContent=formula;for(const attribute of ['t','si','ref'])f.removeAttribute(attribute);book.changed=true;}
 }
 const originKeys=new Set(rows.map(row=>row._origin.sheet+'!'+row._origin.row));
 let missing=0;const summaries=[];
 for(const layout of context.layouts){
  const book=books.get(layout.sheet);if(!book)throw Error('无法定位原工作表：'+layout.sheet);
  const columns={...layout.columns};let maxColumn=Math.max(...Array.from(book.cells.keys()).map(columnNumber),1);
  for(const field of ['price','amount'])if(columns[field]===undefined){columns[field]=maxColumn++;write(book,columnName(columns[field]+1)+layout.headerRow,field==='price'?'单价':'金额');warnings.push(`${layout.sheet}原表无${field==='price'?'单价':'金额'}列，已在右侧添加，原打印区域不变。`);}
  const at=(field,r)=>columnName(columns[field]+1)+r;
  const styles=book.cells.get(at('price',layout.headerRow+1))?.getAttribute('s');
  const active=rows.filter(row=>row._origin.sheet===layout.sheet);
  for(const row of active){
   const origin=row._origin,r=origin.row;
   for(const field of ['name','spec','unit','quantity'])if(columns[field]!==undefined&&String(row[field]??'')!==String(origin.original[field]??''))write(book,at(field,r),field==='quantity'?(number(row[field])??''):row[field]);
   const price=number(row.price),quantity=number(row.quantity);
   if(price===null||quantity===null){missing++;write(book,at('price',r),price??'',{style:styles});write(book,at('amount',r),'');continue;}
   write(book,at('price',r),price,{style:styles});
   const old=book.cells.get(at('amount',r)),f=old?formulaFor(book,old):null;
   write(book,at('amount',r),money(price*quantity),{formula:f||(columns.quantity!==undefined?`ROUND(${at('quantity',r)}*${at('price',r)},2)`:String(money(price*quantity))),style:old?.getAttribute('s')||styles});
  }
  for(const original of context.items.filter(row=>row._origin.sheet===layout.sheet))if(!originKeys.has(layout.sheet+'!'+original._origin.row)){
   write(book,at('price',original._origin.row),'');write(book,at('amount',original._origin.row),'');warnings.push(`${layout.sheet}第${original._origin.row}行已在网页移除：保留原表行位置，但清空单价和金额。`);
  }
  const activeNumbers=new Set(active.map(row=>row._origin.row));
  const originalNumbers=new Set(context.items.filter(row=>row._origin.sheet===layout.sheet).map(row=>row._origin.row));
  const lastRow=Math.max(...Array.from(book.cells.keys()).map(address=>Number(address.match(/\d+/)[0])),layout.headerRow);
  const labelFor=r=>Array.from(book.cells).filter(([address])=>Number(address.match(/\d+/)[0])===r&&!['amount','quantity','price'].some(field=>columns[field]!==undefined&&address===at(field,r))).map(([,cell])=>textValue(cell)).join(' ');
  const allText=Array.from(book.cells.values()).map(textValue).join(' ');
  const rateHit=allText.match(/(?:税率|税额|税金|税费|增值税)[^\d%]{0,8}(\d+(?:\.\d+)?)\s*%/),reverseHit=allText.match(/(\d+(?:\.\d+)?)\s*%\s*(?:增值税|税率)/);
  let originalRate=rateHit||reverseHit?Number((rateHit||reverseHit)[1])/100:null;
  if(originalRate===null)for(const [address,cell] of book.cells){
   const label=textValue(cell).replace(/\s/g,'');if(!/^(?:增值税)?税率(?:[（(]%[）)])?$/.test(label))continue;
   const r=Number(address.match(/\d+/)[0]),c=columnNumber(address);
   for(const target of [columnName(c+1)+r,columnName(c)+(r+1)]){
    const candidate=book.cells.get(target),value=number(textValue(candidate));
    if(value!==null&&percentageStyle(candidate)){originalRate=value;break;}
    if(value!==null&&/%/.test(label)){originalRate=value/100;break;}
    if(value!==null&&value>=0&&value<1){originalRate=value;break;}
   }
   if(originalRate!==null)break;
  }
  const financialLabel=r=>labelFor(r).replace(/\s/g,'').replace(/^\d+[.、：:]?/,'');
  const hasOriginalTax=Array.from({length:lastRow-layout.headerRow},(_,i)=>financialLabel(i+layout.headerRow+1)).some(label=>/^(税额|税金|税费)/.test(label));
  const inclusive=/(?<!不)含税/.test(allText);
  const extraRate=taxMode==='add'?taxRate/100:taxMode==='original'&&!hasOriginalTax&&!inclusive?(originalRate??(taxRate===null?0:taxRate/100)):0;
  let pending=[],components=[],taxes=[],totalAddress=null,taxSeen=false;
  const sum=refs=>refs.length?`SUM(${refs.join(',')})`:'0';
  for(let r=layout.headerRow+1;r<=lastRow;r++){
   const label=financialLabel(r),amountAddress=at('amount',r),cell=book.cells.get(amountAddress),existing=cell?formulaFor(book,cell):null;
   const subtotal=/^(?:本项|分项|项目)?小计/.test(label),total=!subtotal&&/^(?:合计|总计|含税合计|价税合计|项目总价|总报价|含税总价|未税合计|不含税合计)/.test(label),tax=!subtotal&&!total&&/^(?:税额|税金|税费)/.test(label);
   if(subtotal){
    if(/折后|包干/.test(label)&&!existing&&discountMode==='preserve'){if(number(textValue(cell))===null)throw Error('原表固定折后金额为空，请选择按新行金额求和，或先在Excel明确折后金额。');warnings.push(`${layout.sheet}第${r}行是固定折后/包干小计，保留原金额，需人工核对折扣。`);}
    else if(pending.length)write(book,amountAddress,null,{formula:existing||sum(pending)});
    if(cell||pending.length)components.push(amountAddress);pending=[];continue;
   }
   if(tax){
    taxSeen=true;
    if(taxMode==='add')write(book,amountAddress,null,{formula:`ROUND((${sum([...components,...pending])})*${taxRate/100},2)`});
    else if(taxMode==='included'){const rate=originalRate??(taxRate===null?null:taxRate/100);if(rate!==null)write(book,amountAddress,null,{formula:`ROUND((${sum([...components,...pending])})*${rate}/(1+${rate}),2)`});else warnings.push(`${layout.sheet}为含税库价，税额仅作价内拆分且不再叠加，请核对原表税额。`);}
    else if(!existing&&originalRate!==null)write(book,amountAddress,null,{formula:`ROUND((${sum([...components,...pending])})*${originalRate},2)`});
    else if(!existing)warnings.push(`${layout.sheet}第${r}行税费未找到明确税率，保留原固定税费，请核对。`);
    taxes.push(amountAddress);continue;
   }
   if(total){
    let formula=existing||sum([...components,...pending,...taxes]);
    if(!existing&&extraRate&&!taxSeen)formula=`ROUND((${sum([...components,...pending])})*(1+${extraRate}),2)`;
    if(taxMode==='add')formula=taxSeen?sum([...components,...pending,...taxes]):`ROUND((${sum([...components,...pending])})*(1+${taxRate/100}),2)`;
    if(taxMode==='included')formula=sum([...components,...pending]);
    write(book,amountAddress,null,{formula});totalAddress=amountAddress;continue;
   }
   if(originalNumbers.has(r)){
    if(activeNumbers.has(r))pending.push(amountAddress);
    continue;
   }
   // Keep non-plant fees such as pots, labour and freight in their original rows.
   const price=columns.price!==undefined?number(textValue(book.cells.get(at('price',r)))):null,quantity=columns.quantity!==undefined?number(textValue(book.cells.get(at('quantity',r)))):null;
   if(price!==null&&quantity!==null){write(book,amountAddress,null,{formula:existing||`ROUND(${at('quantity',r)}*${at('price',r)},2)`});pending.push(amountAddress);}
   else if(number(textValue(cell))!==null&&label&&!/^备注/.test(label))pending.push(amountAddress);
  }
  if(!totalAddress){warnings.push(`${layout.sheet}原表无合计行，已在末尾增加合计，原打印区域未扩展。`);const r=lastRow+2;totalAddress=at('amount',r);write(book,'A'+r,taxMode==='add'?'合计（含新增税额）':'合计');write(book,totalAddress,null,{formula:extraRate?`ROUND((${sum([...components,...pending])})*(1+${extraRate}),2)`:sum([...components,...pending,...taxes])});}
  if(taxMode==='original'&&originalRate===null&&!taxSeen&&!extraRate)warnings.push(`${layout.sheet}未发现明确可计算税额；按原表金额汇总，不额外加税。需要加税请选择“未税价加税”并输入税率。`);
  summaries.push({sheet:layout.sheet,address:totalAddress,rate:originalRate,extraRate});
  const dimension=nodes(book.doc,'dimension')[0];if(dimension){const addresses=[...book.cells.keys()];dimension.setAttribute('ref',`A1:${columnName(Math.max(...addresses.map(columnNumber)))}${Math.max(...addresses.map(a=>Number(a.match(/\d+/)[0])))}`);}
 }
 const cache=new Map(),busy=new Set(),unsupported=new Set();
 function read(sheet,address){
  const id=sheet+'!'+address;if(cache.has(id))return cache.get(id);if(busy.has(id))throw Error('循环引用');
  const book=books.get(sheet);if(!book)throw Error('未知工作表');const cell=book.cells.get(address);if(!cell)return '';
  if(cell.getAttribute('t')==='e')throw Error('原表存在错误值');
  const formula=formulaFor(book,cell);if(formula===null){const text=textValue(cell);return cell.getAttribute('t')==='s'||cell.getAttribute('t')==='inlineStr'||cell.getAttribute('t')==='str'?text:(number(text)??text);}
  busy.add(id);try{const value=calculateFormula(formula,sheet,read);cache.set(id,value);return value;}finally{busy.delete(id);}
 }
 for(const [sheet,book]of books)for(const [address,cell]of book.cells)if(child(cell,'f')){
  try{
   const value=read(sheet,address);const old=child(cell,'v');if(old)old.remove();cell.removeAttribute('t');
   if(value!==''&&value!==null){const v=make(book.doc,'v');v.textContent=String(typeof value==='number'?Number(value.toPrecision(15)):typeof value==='boolean'?Number(value):value);cell.append(v);if(typeof value==='boolean')cell.setAttribute('t','b');else if(typeof value==='string')cell.setAttribute('t','str');}
  }catch{child(cell,'v')?.remove();unsupported.add(sheet+'!'+address);}
  book.changed=true;
 }
 if(unsupported.size)warnings.push(`${unsupported.size} 个复杂/共享公式已保留并清除旧缓存，需在Excel打开时自动重算。`);
 let grand=0;for(const summary of summaries){try{summary.total=money(Number(read(summary.sheet,summary.address)));grand+=summary.total;}catch{summary.total='打开Excel后重算';}}
 const audit=[['报价计算与核对','说明'],['计价状态',missing?`不完整：${missing}行缺单价或数量，总价仅供核对`:'本次植物行均已填写单价与数量'],['原表固定折后价处理',discountMode==='preserve'?'沿用原固定价，出现时须人工核对':'按更新行金额求和，不应用原固定折扣'],['税费口径',taxMode==='original'?'沿用原表税费，不重复加税':taxMode==='included'?'库价已含税，不额外加税':`未税价加税，税率${taxRate}%`],...summaries.flatMap(s=>[[s.sheet+'合计',s.total],[s.sheet+'新增税率',s.extraRate?(s.extraRate*100)+'%':'未额外加税']]),['各清单合计汇总（非跨表去重）',money(grand)],...warnings.map(w=>['需核对',w]),[],['植物品种','目标规格','数量','最终单价','采用库规格','匹配方式','报价来源','参与报价数','原始平均价']];
 for(const row of rows)audit.push([row.name,row.spec,row.quantity,row.price,(row.matchedName||'')+' '+(row.matchedSpec||''),row.status,row.source,row.quoteCount||0,row.averagePrice??'']);
 audit.push([],['匹配报价明细','目标规格','库中名称','库中规格','库中单价','来源']);for(const row of rows)for(const quote of row.matchedQuotes||[])audit.push([row.name,row.spec,quote.name,quote.spec,Number(quote.price),quote.source]);
 const auditName=(()=>{let name='报价计算核对',i=1;while(books.has(name))name='报价计算核对'+i++;return name;})();
 const id=Math.max(0,...nodes(workbook,'sheet').map(s=>Number(s.getAttribute('sheetId'))))+1,relId='rIdPlantQuote'+id,path=`xl/worksheets/plant-quote-${id}.xml`;
 const xmlRows=audit.map((row,i)=>`<row r="${i+1}">${row.map((value,j)=>typeof value==='number'?`<c r="${columnName(j+1)}${i+1}"><v>${value}</v></c>`:`<c r="${columnName(j+1)}${i+1}" t="inlineStr"><is><t xml:space="preserve">${esc(value)}</t></is></c>`).join('')}</row>`).join('');
 zip.file(path,`<?xml version="1.0" encoding="UTF-8"?><worksheet xmlns="${NS}"><cols><col min="1" max="7" width="28" customWidth="1"/></cols><sheetData>${xmlRows}</sheetData></worksheet>`);
 const sheet=make(workbook,'sheet');sheet.setAttribute('name',auditName);sheet.setAttribute('sheetId',id);sheet.setAttributeNS('http://schemas.openxmlformats.org/officeDocument/2006/relationships','r:id',relId);nodes(workbook,'sheets')[0].append(sheet);
 const relation=relationships.createElementNS(REL,'Relationship');relation.setAttribute('Id',relId);relation.setAttribute('Type','http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet');relation.setAttribute('Target',path.slice(3));relationships.documentElement.append(relation);
 const contentTypes=xml(await zip.file('[Content_Types].xml').async('string')),override=contentTypes.createElementNS(contentTypes.documentElement.namespaceURI,'Override');override.setAttribute('PartName','/'+path);override.setAttribute('ContentType','application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml');contentTypes.documentElement.append(override);
 for(const node of nodes(relationships,'Relationship'))if(node.getAttribute('Type').endsWith('/calcChain')){zip.remove(new URL(node.getAttribute('Target'),'https://xlsx/xl/workbook.xml').pathname.slice(1));node.remove();}
 for(const node of nodes(contentTypes,'Override'))if(node.getAttribute('PartName').endsWith('/calcChain.xml'))node.remove();
 let calc=nodes(workbook,'calcPr')[0];if(!calc){calc=make(workbook,'calcPr');const after=Array.from(workbook.documentElement.children).find(node=>['oleSize','customWorkbookViews','pivotCaches','smartTagPr','smartTagTypes','webPublishing','fileRecoveryPr','webPublishObjects','extLst'].includes(node.localName));workbook.documentElement.insertBefore(calc,after||null);}calc.setAttribute('calcMode','auto');calc.setAttribute('fullCalcOnLoad','1');calc.setAttribute('forceFullCalc','1');
 for(const book of books.values())if(book.changed)zip.file(book.path,serialize(book.doc));
 zip.file('xl/workbook.xml',serialize(workbook));zip.file('xl/_rels/workbook.xml.rels',serialize(relationships));zip.file('[Content_Types].xml',serialize(contentTypes));
 return {blob:await zip.generateAsync({type:'blob',mimeType:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}),warnings,missing,grand:money(grand)};
}
