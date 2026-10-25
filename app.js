import * as pdfjs from './vendor/pdf.mjs';
pdfjs.GlobalWorkerOptions.workerSrc = new URL('./vendor/pdf.worker.mjs', import.meta.url).href;
const $ = id => document.getElementById(id);
const KEY='plant-pricing-library-v1';
let library=[], rows=[];
const norm=v=>String(v??'').normalize('NFKC').replace(/\s+/g,'').toLowerCase();
function notice(text){$('notice').style.display='block';$('notice').textContent=text;}
try {library=JSON.parse(localStorage.getItem(KEY)||'[]');if(!Array.isArray(library))throw Error();}
catch {notice('无法读取浏览器价格库。请检查浏览器存储权限，或用备份文件重新导入。');}
const aliases={name:['植物品种','植物名称','品种','名称','苗木名称','品类'],spec:['规格','植物规格','苗木规格','规格cm','规格(cm)'],unit:['单位','计量单位'],price:['单价','价格','综合单价','单价(元)'],quantity:['数量','工程量']};
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
async function table(file,{allowEmpty=false}={}){
 let raw=[];
 if(file.name.toLowerCase().endsWith('.csv')){
  let bytes=await file.arrayBuffer(),text;
  try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}catch{text=new TextDecoder('gb18030').decode(bytes);}
  raw=csvParse(text.replace(/^\uFEFF/,''));
 }else{
  const book=new ExcelJS.Workbook();await book.xlsx.load(await file.arrayBuffer());
  const sheet=book.worksheets[0];if(!sheet)throw Error('工作簿没有工作表。');
  sheet.eachRow({includeEmpty:true},row=>raw.push(Array.from({length:sheet.columnCount},(_,i)=>(row.getCell(i+1).isMerged&&row.getCell(i+1).master.address!==row.getCell(i+1).address?'':cellValue(row.getCell(i+1).value)))));
 }
 let columns={},header=-1;
 for(let i=0;i<Math.min(raw.length,30);i++){
  let found={};raw[i].forEach((v,j)=>Object.entries(aliases).forEach(([key,words])=>{if(words.some(w=>norm(w)===norm(v)))found[key]=j;}));
  if('name'in found&&('spec'in found||'price'in found)){columns=found;header=i;break;}
 }
 if(header<0)throw Error('前30行没有找到表头，需要植物名称/品类列，以及规格或单价列。');
 const items=[],skipped=[];
 raw.slice(header+1).forEach((r,i)=>{
  const item=Object.fromEntries(Object.entries(columns).map(([k,j])=>[k,String(r[j]??'').trim()]));item.line=header+i+2;
  if(item.name&&!aliases.name.some(w=>norm(w)===norm(item.name)))items.push(item);
  else if(item.price||item.spec||item.quantity)skipped.push(`第${item.line}行：${r[1]||'未命名费用项'}`);
 });
 if(!items.length&&!allowEmpty)throw Error('没有读到植物数据。');return {items,skipped};
}
const key=r=>[r.name,r.spec,r.unit].map(norm).join('\u0000');
function match(r){
 const result={...r,price:'',source:''};
 let candidates=library.filter(x=>norm(x.name)===norm(r.name));
 if(!candidates.length)candidates=library.filter(x=>norm(x.name.replace(/[A-Za-z0-9]+$/,''))===norm(r.name));
 const exact=candidates.filter(x=>norm(x.name)===norm(r.name)&&r.spec&&norm(x.spec)===norm(r.spec)&&(!r.unit||norm(x.unit)===norm(r.unit)));
 if(exact.length===1)Object.assign(result,{price:exact[0].price,unit:exact[0].unit,source:exact[0].source,status:'已匹配'});
 else result.status=candidates.length?'规格或单位待确认':'价格库无此品种';
 result.candidates=candidates;return result;
}
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
async function run(id,action){const controls=[$('import'),$('quote'),$('import-templates')];controls.forEach(b=>b.disabled=true);notice('正在本机浏览器中处理文件，请稍候……');try{await action();}catch(e){notice('处理失败：'+e.message);}finally{controls.forEach(b=>b.disabled=false);}}
function selected(id){const file=$(id).files[0];if(!file)throw Error('请先选择文件。');if(file.size>20*1024*1024)throw Error('文件超过20MB，请拆分后上传。');return file;}
$('import').onclick=()=>run('import',async()=>{
 const file=selected('prices'),{items,skipped}=await table(file),newItems=new Map(),errors=[];
 for(const r of items){const value=String(r.price??'').replace(/[,￥¥]/g,'').trim();
  if(!r.spec||!r.unit||!/^\d+(\.\d+)?$/.test(value)||!Number.isFinite(Number(value))){errors.push(`第${r.line}行：规格/单位缺失或单价不是非负数字`);continue;}
  const entry={name:r.name,spec:r.spec,unit:r.unit,price:String(Number(value)),source:file.name},k=key(entry);
  if(newItems.has(k)&&newItems.get(k).price!==entry.price)errors.push(`同一规格出现冲突单价：${r.name} / ${r.spec}`);newItems.set(k,entry);
 }
 if(errors.length)throw Error('本次未保存，请修正后重试。\n'+errors.join('\n'));
 const merged=new Map(library.map(r=>[key(r),r]));for(const [k,r]of newItems)merged.set(k,r);
 const next=[...merged.values()];localStorage.setItem(KEY,JSON.stringify(next));library=next;refresh();
 notice(`导入 ${items.length} 行，涉及 ${newItems.size} 个植物规格。当前库共 ${library.length} 项。\n排除 ${skipped.length} 行无植物名称的费用项。`+(skipped.length?'\n'+skipped.join('\n'):''));
});
$('quote').onclick=()=>run('quote',async()=>{
 const file=selected('project');let text='',warning='';
 if(/\.(xlsx|csv)$/i.test(file.name)){const data=await table(file);rows=data.items.map(match);warning=`已匹配。排除 ${data.skipped.length} 行无植物名称的费用项。`;}
 else{text=await documentText(file);if(!text.trim())throw Error('没有可提取文字。扫描件和图片方案暂不支持，请使用文字版PDF/PPTX。');rows=extract(text);warning='方案提取为辅助草稿，词表外品种可能遗漏。数量不推断，规格未确认时价格留空；备选方案不可直接合并计量。请核对原文。';}
 $('original').hidden=!text;$('text').textContent=text;render();notice(warning);
});
$('rematch').onclick=()=>{rows=rows.map(match);render();};
$('add').onclick=()=>{rows.push({name:'',spec:'',unit:'',quantity:'',price:'',status:'待填写'});render();};
$('export').onclick=()=>{if(!rows.length)return notice('请先生成清单。');download(['植物品种','规格','单位','数量','单价','匹配状态','报价来源'],rows.map(r=>['name','spec','unit','quantity','price','status','source'].map(k=>r[k])),'植物报价清单.xlsx').catch(e=>notice(e.message));};
$('template').onclick=e=>{e.preventDefault();download(['植物品种','规格','单位','单价','数量'],[['桂花','高度200cm 冠幅150cm','株','380','10']],'植物报价模板.xlsx').catch(e=>notice(e.message));};
$('backup').onclick=()=>download(['植物品种','规格','单位','单价'],library.map(r=>[r.name,r.spec,r.unit,r.price]),'植物价格库备份.xlsx').catch(e=>notice(e.message));
refresh();

// Store original template files separately from the price library. IndexedDB
// accommodates XLSX files without consuming the small localStorage quota.
let templateDatabase;
function openTemplates(){
 if(!templateDatabase)templateDatabase=new Promise((resolve,reject)=>{
  const request=indexedDB.open('plant-pricing-templates',1);
  request.onupgradeneeded=()=>request.result.createObjectStore('templates',{keyPath:'id'});
  request.onsuccess=()=>resolve(request.result);
  request.onerror=()=>reject(Error('无法访问浏览器模板存储，请检查存储权限或隐私模式。'));
  request.onblocked=()=>reject(Error('模板存储被其他标签页占用，请关闭其他本网站标签页后刷新。'));
 });
 return templateDatabase;
}
async function templateOperation(mode,operation){
 const db=await openTemplates();
 return new Promise((resolve,reject)=>{
  const transaction=db.transaction('templates',mode);
  let request;
  try{request=operation(transaction.objectStore('templates'));}catch(error){reject(error);return;}
  transaction.oncomplete=()=>resolve(request.result);
  transaction.onerror=transaction.onabort=()=>reject(Error(transaction.error?.name==='QuotaExceededError'?'浏览器存储空间不足，请先下载备份并删除不需要的模板。':'模板保存或读取失败，原有模板未被清空。'));
 });
}
function downloadOriginal(template){
 const url=URL.createObjectURL(template.file),link=document.createElement('a');
 link.href=url;link.download=template.name;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
async function showTemplates(){
 const templates=await templateOperation('readonly',store=>store.getAll());
 templates.sort((a,b)=>b.created-a.created);
 $('saved-templates').replaceChildren();$('templates-empty').hidden=templates.length>0;
 $('template-count').textContent=`已保存 ${templates.length} 份模板`;
 for(const template of templates){
  const tr=document.createElement('tr');
  cell(tr,template.name);cell(tr,String(template.items.length));cell(tr,new Date(template.created).toLocaleString('zh-CN'));
  const actions=cell(tr,'');
  const use=document.createElement('button');use.textContent='使用模板填价';
  use.onclick=()=>{
   if(rows.length&&!window.confirm('使用模板将替换当前项目清单，未导出的修改会丢失。是否继续？'))return;
   rows=template.items.map(match);render();$('original').hidden=true;$('text').textContent='';
   notice(`已使用模板“${template.name}”，生成 ${rows.length} 行清单，按当前价格库重新匹配。原模板中的价格不会自动导入价格库。`+(template.skipped.length?`\n排除 ${template.skipped.length} 行无植物名称的费用项。`:''));
   document.querySelector('.results').scrollIntoView({behavior:'smooth',block:'start'});
  };
  // Keep rendering independent from template filenames; user content is text.
  const original=document.createElement('button');original.textContent='下载原模板';original.className='secondary';original.onclick=()=>downloadOriginal(template);
  const remove=document.createElement('button');remove.textContent='删除';remove.className='secondary';
  remove.onclick=async()=>{
   if(!window.confirm(`删除模板“${template.name}”？此操作不会删除价格库和当前项目清单。`))return;
   remove.disabled=true;
   try{await templateOperation('readwrite',store=>store.delete(template.id));await showTemplates();notice('已删除模板，价格库和当前清单保留。');}
   catch(error){notice(error.message);remove.disabled=false;}
  };
  for(const button of [use,original,remove]){button.style.margin='3px';actions.append(button);}
  $('saved-templates').append(tr);
 }
}
$('import-templates').onclick=()=>run('import-templates',async()=>{
 const files=Array.from($('template-files').files);if(!files.length)throw Error('请先选择一份或多份报价模板。');
 const saved=[],failed=[];
 for(const file of files){
  try{
   if(!/\.(xlsx|csv)$/i.test(file.name))throw Error('仅支持XLSX或CSV格式');
   if(file.size>20*1024*1024)throw Error('文件超过20MB');
   const {items,skipped}=await table(file,{allowEmpty:true});
   const template={id:crypto.randomUUID(),name:file.name,created:Date.now(),items,skipped,file};
   await templateOperation('readwrite',store=>store.add(template));saved.push(`${file.name}（${items.length}行${skipped.length?`，排除${skipped.length}行费用项`:''}）`);
  }catch(error){failed.push(`${file.name}：${error.message}`);}
 }
 await showTemplates();
 notice(`已保存 ${saved.length} 份模板，失败 ${failed.length} 份。`+(saved.length?'\n已保存：\n'+saved.join('\n'):'')+(failed.length?'\n未保存：\n'+failed.join('\n'):''));
 $('template-files').value='';
});
showTemplates().catch(error=>{$('template-count').textContent='模板存储不可用';notice(error.message);});
