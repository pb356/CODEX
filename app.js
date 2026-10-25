import {norm,priceKey,matchPrice,priceEntries} from './pricing.js';
import * as pdfjs from './vendor/pdf.mjs';
pdfjs.GlobalWorkerOptions.workerSrc = new URL('./vendor/pdf.worker.mjs', import.meta.url).href;
const $ = id => document.getElementById(id);
const KEY='plant-pricing-library-v1';
let library=[], rows=[];
let initializeLibrary=Promise.resolve();
function notice(text){$('notice').style.display='block';$('notice').textContent=text;}
try {library=JSON.parse(localStorage.getItem(KEY)||'[]');if(!Array.isArray(library))throw Error();}
catch {notice('无法读取浏览器价格库。请检查浏览器存储权限，或用备份文件重新导入。');}
const aliases={name:['植物品种','植物名称','品种','名称','苗木名称','苗木品种','品类'],spec:['规格','植物规格','苗木规格','规格cm','规格(cm)'],unit:['单位','计量单位'],price:['单价','价格','综合单价','单价(元)','含税单价','不含税单价','单价(元/株)','单价(元/棵)'],quantity:['数量','工程量']};
const vocabulary='银合欢 侧柏 珍珠狗牙花 蓝星花 肾蕨 迷迭香 澳洲米花 女贞 油橄榄 蒲葵 花叶十万错 银毛野牡丹 银叶仓鼠 滴水观音 鹤望兰 花叶长春蔓 狐尾天门冬 菲油果 棕竹 琴叶榕 富贵蕨 黄杨球 女贞球 天冬 大红花 桂花 芭蕉 旅人蕉 龟背竹 霸王蕨'.split(' ');
function csvParse(text){
 const result=[];let row=[],value='',quoted=false;
 for(let i=0;i<text.length;i++){let c=text[i];
  if(c==='"'){if(quoted&&text[i+1]==='"'){value+='"';i++;}else quoted=!quoted;}
  else if(c===','&&!quoted){row.push(value);value='';}
  else if((c==='\n'||c==='\r')&&!quoted){if(c==='\r'&&text[i+1]==='\n')i++;row.push(value);result.push(row);row=[];value='';}
  else value+=c;
 }
 if(quoted)throw Error('CSV引号未闭合，请检查文件。');
 if(value||row.length){row.push(value);result.push(row);}return result;
}
function cellValue(v){if(v===null||v===undefined)return '';if(typeof v==='object'){if('result'in v)return cellValue(v.result);if(v.richText)return v.richText.map(x=>x.text).join('');if(v.text)return v.text;return '';}return String(v);}
async function table(file){
 const tables=[];
 if(file.name.toLowerCase().endsWith('.csv')){
  let bytes=await file.arrayBuffer(),text;
  try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}catch{text=new TextDecoder('gb18030').decode(bytes);}
  tables.push({name:file.name,raw:csvParse(text.replace(/^\uFEFF/,''))});
 }else{
  const book=new ExcelJS.Workbook();await book.xlsx.load(await file.arrayBuffer());
  for(const sheet of book.worksheets){
   const raw=[];
   sheet.eachRow({includeEmpty:true},row=>raw.push(Array.from({length:sheet.columnCount},(_,i)=>{const cell=row.getCell(i+1);return cell.isMerged&&cell.master.address!==cell.address?'':cellValue(cell.value);} )));
   tables.push({name:sheet.name,raw});
  }
 }
 const items=[],skipped=[];let recognized=0;
 const dimensionHeader=value=>{
  const text=norm(value),hit=text.match(/^(高度|株高|h|冠幅|冠径|蓬幅|蓬径|p|胸径|米径|d|地径|g)(?:\((cm|厘米|m|米)\)|(cm|厘米|m|米))?$/);
  return hit?{label:hit[1],unit:hit[2]||hit[3]||'cm'}:null;
 };
 for(const {name,raw} of tables){
  let columns={},dimensions=[],header=-1;
  for(let i=0;i<Math.min(raw.length,30);i++){
   const found={},dims=[];
   raw[i].forEach((value,j)=>{
    Object.entries(aliases).forEach(([key,words])=>{if(words.some(word=>norm(word)===norm(value)))found[key]=j;});
    const dimension=dimensionHeader(value);if(dimension)dims.push({...dimension,index:j});
   });
   if('name'in found&&('spec'in found||'price'in found||dims.length)){columns=found;dimensions=dims;header=i;break;}
  }
  if(header<0)continue;
  recognized++;
  raw.slice(header+1).forEach((r,index)=>{
   const item=Object.fromEntries(Object.entries(columns).map(([key,j])=>[key,String(r[j]??'').trim()]));item.line=`${name} 第${header+index+2}`;
   if(!item.spec&&dimensions.length)item.spec=dimensions.filter(d=>String(r[d.index]??'').trim()).map(d=>{
    const value=String(r[d.index]).trim();return d.label+value+(/(?:cm|厘米|m|米)$/i.test(value)?'':d.unit);
   }).join(' ');
   if(item.name&&!aliases.name.some(word=>norm(word)===norm(item.name)))items.push(item);
   else if(item.price||item.spec||item.quantity)skipped.push(`${item.line}行：${r[1]||'未命名费用项'}`);
  });
 }
 if(!recognized)throw Error('未找到报价表头。需要植物名称/品类列，以及规格、单价或高度/冠幅等列。已检查所有工作表的前30行。');
 if(!items.length)throw Error('没有读到植物数据。');return {items,skipped};
}
const key=priceKey;
const match=row=>matchPrice(row,library);
async function documentText(file){
 if(file.name.toLowerCase().endsWith('.pdf')){
  const doc=await pdfjs.getDocument({data:new Uint8Array(await file.arrayBuffer()),cMapUrl:new URL('./vendor/cmaps/',import.meta.url).href,cMapPacked:true,standardFontDataUrl:new URL('./vendor/standard_fonts/',import.meta.url).href}).promise;
  const parts=[];
  try{for(let n=1;n<=doc.numPages;n++){
   const page=await doc.getPage(n), content=await page.getTextContent();
   let line='',lastY=null;
   for(const item of content.items){if(!('str'in item))continue;const y=item.transform[5];if(lastY!==null&&Math.abs(y-lastY)>3){parts.push(line);line='';}line+=item.str+' ';lastY=y;if(item.hasEOL){parts.push(line);line='';lastY=null;}}
   if(line)parts.push(line);
  }}finally{await doc.destroy();}
  return parts.join('\n');
 }
 if(!file.name.toLowerCase().endsWith('.pptx'))throw Error('方案仅支持PDF或PPTX，旧版PPT请另存为PPTX。');
 const zip=await JSZip.loadAsync(file),parts=[];
 const names=Object.keys(zip.files).filter(x=>/^ppt\/slides\/slide\d+\.xml$/.test(x)).sort((a,b)=>Number(a.match(/slide(\d+)/)[1])-Number(b.match(/slide(\d+)/)[1]));
 for(const name of names){
  const xml=new DOMParser().parseFromString(await zip.file(name).async('string'),'application/xml');
  for(const paragraph of xml.getElementsByTagNameNS('*','p')){
   const text=Array.from(paragraph.getElementsByTagNameNS('*','t')).map(x=>x.textContent).join('');if(text)parts.push(text);
  }
 }
 return parts.join('\n');
}
function extract(text){
 const names=[...new Set([...vocabulary,...library.map(r=>r.name),...library.map(r=>r.name.replace(/[A-Za-z0-9]+$/,''))])].filter(Boolean).sort((a,b)=>b.length-a.length);
 const canonical=new Map(names.map(n=>[norm(n),n]));
 const pattern=new RegExp([...canonical.keys()].map(s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')).join('|'),'g'),found=new Map();
 for(const line of text.split('\n'))for(const hit of norm(line).matchAll(pattern)){const name=canonical.get(hit[0]);if(!found.has(name))found.set(name,[]);found.get(name).push(line);}
 const output=[];
 for(const [name,lines] of found){const specs=[...new Set(library.filter(r=>r.name===name&&lines.some(line=>norm(line).includes(norm(r.spec)))).map(r=>r.spec))];
  for(const spec of specs.length?specs:[''])output.push(match({name,spec,unit:'',quantity:''}));
 }return output;
}
function cell(tr,text){const td=document.createElement('td');td.textContent=text;tr.append(td);return td;}
function refresh(){
 $('count').textContent=library.length;$('library').replaceChildren();
 for(const r of library){const tr=document.createElement('tr');for(const k of ['name','spec','unit','price','source'])cell(tr,r[k]);$('library').append(tr);}
}
function render(){
 $('rows').replaceChildren();$('empty').hidden=rows.length>0;
 rows.forEach((r,i)=>{const tr=document.createElement('tr');
  for(const k of ['name','spec','unit','quantity','price']){const input=document.createElement('input');input.value=r[k]??'';input.setAttribute('aria-label',k);
   input.onchange=()=>{r[k]=input.value;if(['name','spec','unit'].includes(k)){r.price='';r.source='';r.status='已修改，待重新匹配';}else if(k==='price'){r.status='人工填写';r.source='人工填写';}render();};cell(tr,'').append(input);}
  const td=cell(tr,r.status||'待填写');if(r.candidates?.length&&r.status!=='已匹配'){const small=document.createElement('small');small.textContent=r.candidates.map(c=>`${c.name}：${c.spec} / ${c.unit}：¥${c.price}`).join('；');td.append(small);}
  if(r.candidates?.length&&r.status!=='已匹配'){
   const select=document.createElement('select');select.setAttribute('aria-label','选择参考报价');select.style.maxWidth='240px';
   const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='选择库中规格（需确认）';select.append(placeholder);
   r.candidates.forEach((candidate,index)=>{const option=document.createElement('option');option.value=String(index);option.textContent=`${candidate.name} / ${candidate.spec} / ${candidate.unit} / ¥${candidate.price}`;select.append(option);});
   const adopt=document.createElement('button');adopt.className='secondary';adopt.textContent='确认采用';adopt.style.marginTop='6px';
   adopt.onclick=()=>{
    if(select.value===''){notice('请先选择一条参考报价，并确认规格及单位。');return;}
    const candidate=r.candidates[Number(select.value)];
    if(!window.confirm(`确认使用 ${candidate.name}，${candidate.spec}，${candidate.price}元/${candidate.unit}？将按该报价更新本行名称、规格和单位。`))return;
    Object.assign(r,{name:candidate.name,spec:candidate.spec,unit:candidate.unit,price:candidate.price,source:candidate.source,status:'人工确认库价'});render();notice(matchingReport());
   };
   td.append(select,adopt);
  }
  const button=document.createElement('button');button.className='secondary';button.textContent='移除';button.onclick=()=>{rows.splice(i,1);render();};cell(tr,'').append(button);$('rows').append(tr);
 });
 $('summary').textContent=`共 ${rows.length} 项 · 已填价 ${rows.filter(r=>String(r.price??'')!=='').length} 项 · 请核对后导出`;
}
async function download(headers,items,filename){
 const book=new ExcelJS.Workbook(),sheet=book.addWorksheet('植物清单');sheet.addRow(headers);
 for(const row of items)sheet.addRow(row.map(v=>String(v??'')));
 sheet.columns.forEach(c=>c.width=24);sheet.views=[{state:'frozen',ySplit:1}];
 const url=URL.createObjectURL(new Blob([await book.xlsx.writeBuffer()],{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}));
 const a=document.createElement('a');a.href=url;a.download=filename;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
async function run(id,action){const controls=[$('import'),$('quote')];controls.forEach(b=>b.disabled=true);notice('正在本机浏览器中处理文件，请稍候……');try{await initializeLibrary;await action();}catch(e){notice('处理失败：'+e.message);}finally{controls.forEach(b=>b.disabled=false);}}
function selected(id){const file=$(id).files[0];if(!file)throw Error('请先选择文件。');if(file.size>20*1024*1024)throw Error('文件超过20MB，请拆分后上传。');return file;}
function savePrices(next){localStorage.setItem(KEY,JSON.stringify(next));library=next;refresh();}
function rematchAfterImport(){rows=rows.map(row=>['人工填写','人工确认库价'].includes(row.status)?row:match(row));render();}
function matchingReport(extra=''){
 const priced=rows.filter(row=>String(row.price??'')!=='').length;
 return `${rows.length} 行植物：已填价 ${priced} 行，待确认 ${rows.length-priced} 行。${library.length?'':'价格库为空，请先在左侧导入带单价的报价表。'}${extra}`;
}
$('import').onclick=()=>run('import',async()=>{
 const file=selected('prices'),{items,skipped}=await table(file);
 const {entries,errors}=priceEntries(items,file.name);
 if(errors.length)throw Error('本次未保存，请修正后重试。\n'+errors.join('\n'));
 const next=[...library.filter(row=>!entries.has(key(row))),...entries.values()];
 savePrices(next);if(rows.length)rematchAfterImport();
 notice(`导入 ${items.length} 行，涉及 ${entries.size} 个植物规格。当前价格库共 ${library.length} 项。`+(rows.length?'\n当前清单已自动重新填价：'+matchingReport():'')+`\n排除 ${skipped.length} 行无植物名称的费用项。`+(skipped.length?'\n'+skipped.join('\n'):''));
});
$('quote').onclick=()=>run('quote',async()=>{
 const file=selected('project');let text='',warning='';
 if(/\.(xlsx|csv)$/i.test(file.name)){const data=await table(file);rows=data.items.map(match);warning=matchingReport(`排除 ${data.skipped.length} 行无植物名称的费用项。`);}
 else{text=await documentText(file);if(!text.trim())throw Error('没有可提取文字。扫描件和图片方案暂不支持，请使用文字版PDF/PPTX。');rows=extract(text);warning='方案提取为辅助草稿，词表外品种可能遗漏。数量不推断，规格未确认时价格留空；备选方案不可直接合并计量。请核对原文。';}
 $('original').hidden=!text;$('text').textContent=text;render();notice(warning);
});
$('rematch').onclick=()=>{rows=rows.map(match);render();notice(matchingReport());};
$('add').onclick=()=>{rows.push({name:'',spec:'',unit:'',quantity:'',price:'',status:'待填写'});render();};
$('export').onclick=()=>{if(!rows.length)return notice('请先生成清单。');download(['植物品种','规格','单位','数量','单价','匹配状态','报价来源'],rows.map(r=>['name','spec','unit','quantity','price','status','source'].map(k=>r[k])),'植物报价清单.xlsx').catch(e=>notice(e.message));};
$('template').onclick=e=>{e.preventDefault();download(['植物品种','规格','单位','单价','数量'],[['桂花','高度200cm 冠幅150cm','株','380','10']],'植物报价模板.xlsx').catch(e=>notice(e.message));};
$('backup').onclick=()=>download(['植物品种','规格','单位','单价'],library.map(r=>[r.name,r.spec,r.unit,r.price]),'植物价格库备份.xlsx').catch(e=>notice(e.message));
refresh();


// Retire the old template UI without losing prices that users saved there.
// Original template files remain in the retired store as a local recovery copy.
async function recoverLegacyPrices(){
 const marker='plant-pricing-templates-recovered-v2';if(localStorage.getItem(marker))return;
 const db=await new Promise((resolve,reject)=>{
  const request=indexedDB.open('plant-pricing-templates',1);
  request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(Error('旧模板报价恢复失败，请将原报价表导入左侧价格库。'));
 });
 let templates;
 try{
  if(!db.objectStoreNames.contains('templates')){localStorage.setItem(marker,'done');return;}
  templates=await new Promise((resolve,reject)=>{
   const tx=db.transaction('templates','readonly'),request=tx.objectStore('templates').getAll();
   tx.oncomplete=()=>resolve(request.result);tx.onerror=()=>reject(Error('旧模板读取失败，请将原报价表导入价格库。'));
  });
 }finally{db.close();}
 const groups=new Map();
 for(const template of templates)for(const row of template.items||[]){
  const {entries,errors}=priceEntries([row],template.name);if(errors.length)continue;
  for(const [key,entry] of entries){if(!groups.has(key))groups.set(key,[]);groups.get(key).push(entry);}
 }
 const existing=new Set(library.map(row=>key(row))),next=[...library];let recovered=0,conflicts=0;
 for(const [key,entries]of groups){
  if(existing.has(key))continue;
  if(new Set(entries.map(row=>row.price)).size>1){conflicts++;continue;}
  next.push(entries[0]);recovered++;
 }
 if(recovered)savePrices(next);
 localStorage.setItem(marker,'done');
 if(recovered||conflicts)notice(`已将旧模板中的 ${recovered} 条有效报价合并到植物报价库，已有库价保留。`+(conflicts?`另有 ${conflicts} 个规格报价冲突，请重新导入原报价表确认。`:''));
}
initializeLibrary=recoverLegacyPrices().catch(error=>notice(error.message+' 现有价格库保留。'));
