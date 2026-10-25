import {PERIODS,periodFromText,rentalUnit,rentDuration} from './rental.js?v=dual6';
import {fillOriginalXlsx} from './original-xlsx.js?v=dual6';
import {norm,priceKey,matchPrice,priceEntries,quoteKey} from './pricing.js?v=dual6';
import * as pdfjs from './vendor/pdf.mjs';
pdfjs.GlobalWorkerOptions.workerSrc = new URL('./vendor/pdf.worker.mjs', import.meta.url).href;
const $ = id => document.getElementById(id);
const MODE=new URLSearchParams(location.search).get('mode')==='indoor'?'indoor':'outdoor';
const isIndoor=MODE==='indoor';
const KEY=isIndoor?'plant-pricing-rental-library-v1':'plant-pricing-library-v1';
const settingsKey='plant-pricing-settings-'+MODE+'-v1';
let library=[], rows=[];
let originalProject=null;
let initializeLibrary=Promise.resolve();
let activeDownloadUrl=null;
function notice(text){$('notice').style.display='block';$('notice').textContent=text;}
try {library=JSON.parse(localStorage.getItem(KEY)||'[]');if(!Array.isArray(library))throw Error();}
catch {notice('无法读取浏览器价格库。请检查浏览器存储权限，或用备份文件重新导入。');}
const aliases={name:['植物品种','植物名称','品种','名称','苗木名称','苗木品种','品类'],spec:['规格','植物规格','苗木规格','规格cm','规格(cm)'],unit:['单位','计量单位'],price:['单价','价格','综合单价','单价(元)','含税单价','不含税单价','单价(元/株)','单价(元/棵)','月租金','月租单价','月租金单价','租金(元/月)','月单价','日租金','日租单价','年租金','年租单价','租赁单价','租赁价格','元/月','元/天','元/日','元/年','租金/月'],quantity:['数量','工程量'],amount:['金额','总价','合价','金额(元)','合计金额','总价(元)','合计(元)'],source:['报价来源','来源文件'],sourceId:['报价文件标识'],business:['报价工作区'],billingPeriod:['计租周期'],notes:['备注','说明','计费说明'],duration:['租期','租赁月数','租期(月)','租赁天数','租期(天)','租期(日)','租赁年数','租期(年)','租赁周期数']};
const vocabulary='银合欢 侧柏 珍珠狗牙花 蓝星花 肾蕨 迷迭香 澳洲米花 女贞 油橄榄 蒲葵 花叶十万错 银毛野牡丹 银叶仓鼠 滴水观音 鹤望兰 花叶长春蔓 狐尾天门冬 菲油果 棕竹 琴叶榕 富贵蕨 发财树 幸福树 散尾葵 绿萝 大叶绿萝 绿萝柱 金钱树 富贵竹 白掌 红掌 巴西木 龙血树 橡皮树 也门铁 富贵椰子 夏威夷椰子 竹芋 吊兰 虎皮兰 黄杨球 女贞球 天冬 大红花 桂花 芭蕉 旅人蕉 龟背竹 霸王蕨'.split(' ');
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
 const items=[],skipped=[],layouts=[],rentalReceipts=[];let recognized=0;
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
  if(!isIndoor&&periodFromText(raw[header][columns.price])&&/租|元[\/／]/.test(String(raw[header][columns.price])))throw Error('这份文件是按周期计价的租赁报价，请切换到室内植物租赁工作区导入。');
  if(isIndoor)for(let r=header+1;r<raw.length;r++){const label=raw[r].map(value=>String(value??'')).find(value=>/^实收(?:金额)?$/.test(value.trim()));if(label&&columns.amount!==undefined){const value=Number(raw[r][columns.amount]);if(Number.isFinite(value)&&String(raw[r][columns.amount]??'')!=='')rentalReceipts.push({sheet:name,value,row:r+1,period:periodFromText(raw[header][columns.price])||$('rental-period').value});}}
  recognized++;layouts.push({sheet:name,headerRow:header+1,columns:{...columns}});
  raw.slice(header+1).forEach((r,index)=>{
   const item=Object.fromEntries(Object.entries(columns).map(([key,j])=>[key,String(r[j]??'').trim()]));item.line=`${name} 第${header+index+2}`;
   if(item.business&&item.business!==MODE)throw Error('这份备份属于'+(item.business==='indoor'?'室内租赁':'室外植物')+'工作区，请切换后再导入，避免混入当前价格库。');
   if(isIndoor)item.billingPeriod=periodFromText(item.billingPeriod)||periodFromText(r[columns.unit])||periodFromText(raw[header][columns.price])||periodFromText(raw[header][columns.duration])||$('rental-period').value;
   if(!item.spec&&dimensions.length)item.spec=dimensions.filter(d=>String(r[d.index]??'').trim()).map(d=>{
    const value=String(r[d.index]).trim();return d.label+value+(/(?:cm|厘米|m|米)$/i.test(value)?'':d.unit);
   }).join(' ');
   if(item.name&&!/^(小计|合计|总计|税额|税费|税金|税率|未税合计|含税合计|价税合计)/.test(norm(item.name))&&!aliases.name.some(word=>norm(word)===norm(item.name))){item._origin={sheet:name,row:header+index+2,columns:{...columns},original:{...item}};if(isIndoor){item.isGift=/赠送|赠品|免租/.test(item.notes||'')||/^免费(?:$|[，,;；])/.test(item.notes||'');item.unit=item.unit||$('rental-default-unit').value;}items.push(item);}
   else if(item.price||item.spec||item.quantity)skipped.push(`${item.line}行：${r[1]||'未命名费用项'}`);
  });
 }
 if(!recognized)throw Error('未找到报价表头。需要植物名称/品类列，以及规格、单价或高度/冠幅等列。已检查所有工作表的前30行。');
 if(!items.length)throw Error('没有读到植物数据。');return {items,skipped,layouts,rentalReceipts,taxIncluded:isIndoor&&tables.some(table=>table.raw.some(row=>row.some(value=>/(?<!不)含税/.test(String(value??'')))))};
}
const key=priceKey;
const match=row=>{
 if(isIndoor&&row.isGift)return {...row,price:row.price||'0',status:'原表赠送，不计租金',source:'原清单赠送约定',matchedQuotes:[],quoteCount:0,averagePrice:null};
 if(!isIndoor)return matchPrice(row,library,{tolerance:Number($('tolerance').value)/100});
 const period=$('rental-period').value;
 const selected=library.filter(quote=>(quote.billingPeriod||'month')===period).map(quote=>({...quote,unit:rentalUnit(quote.unit)}));
 const matched=matchPrice({...row,unit:rentalUnit(row.unit)},selected,{tolerance:Number($('tolerance').value)/100});
 matched.unit=row.unit||matched.unit;matched.billingPeriod=period;
 if(!selected.length&&library.length)matched.status='当前按'+PERIODS[period]+'计价，库中没有该周期报价';
 return matched;
};
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
 for(const r of library){const tr=document.createElement('tr');for(const k of ['name','spec','unit','price','source'])cell(tr,k==='unit'&&isIndoor?(r.unit||'')+'/'+PERIODS[r.billingPeriod||'month']:r[k]);$('library').append(tr);}
}
function render(){
 invalidateDownload();
 $('rows').replaceChildren();$('empty').hidden=rows.length>0;
 rows.forEach((r,i)=>{const tr=document.createElement('tr');
  for(const k of ['name','spec','unit','quantity','price',...(isIndoor?['duration']:[])]){const input=document.createElement('input');input.value=r[k]??(k==='duration'?$('rental-duration').value:'');input.setAttribute('aria-label',k);
   input.onchange=()=>{r[k]=input.value;if(['name','spec','unit'].includes(k)){r.price='';r.source='';r.status='已修改，待重新匹配';}else if(k==='price'){r.status='人工填写';r.source='人工填写';}render();};cell(tr,'').append(input);}
  const td=cell(tr,r.status||'待填写');if(r.quoteCount>1){const average=document.createElement('small');average.textContent=`参与均价 ${r.quoteCount} 条；原始均价 ¥${r.averagePrice.toFixed(2)}，按个位规则取 ¥${r.price}`;td.append(average);}if(r.matchedSpec){const basis=document.createElement('small');basis.textContent='采用库规格：'+(r.matchedName||r.name)+' '+r.matchedSpec;td.append(basis);}if(r.candidates?.length&&String(r.price??'')===''){const small=document.createElement('small');small.textContent=r.candidates.map(c=>`${c.name}：${c.spec} / ${c.unit}：¥${c.price}`).join('；');td.append(small);}
  if(r.candidates?.length&&String(r.price??'')===''){
   const select=document.createElement('select');select.setAttribute('aria-label','选择参考报价');select.style.maxWidth='240px';
   const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='选择库中规格（需确认）';select.append(placeholder);
   r.candidates.forEach((candidate,index)=>{const option=document.createElement('option');option.value=String(index);option.textContent=`${candidate.name} / ${candidate.spec} / ${candidate.unit} / ¥${candidate.price}`;select.append(option);});
   const adopt=document.createElement('button');adopt.className='secondary';adopt.textContent='确认采用';adopt.style.marginTop='6px';
   adopt.onclick=()=>{
    if(select.value===''){notice('请先选择一条参考报价，并确认规格及单位。');return;}
    const candidate=r.candidates[Number(select.value)];
    if(!window.confirm(`确认使用 ${candidate.name}，${candidate.spec}，${candidate.price}元/${candidate.unit}？将按该报价更新本行名称、规格和单位。`))return;
    Object.assign(r,{name:candidate.name,spec:candidate.spec,unit:candidate.unit,price:candidate.price,source:candidate.source,matchedSpec:candidate.spec,matchedQuotes:[candidate],quoteCount:1,averagePrice:Number(candidate.price),status:'人工确认库价'});render();notice(matchingReport());
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
 saveBlob(new Blob([await book.xlsx.writeBuffer()],{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}),filename);
}
function exportStatus(text,isError=false){const node=$('export-status');node.textContent=text;node.style.color=isError?'#a12e20':'#276448';node.setAttribute('role',isError?'alert':'status');if(isError)node.scrollIntoView({block:'nearest'});}
function invalidateDownload(){
 if(activeDownloadUrl){const old=activeDownloadUrl;activeDownloadUrl=null;setTimeout(()=>URL.revokeObjectURL(old),60000);$('download-ready').hidden=true;$('download-file').removeAttribute('href');exportStatus('清单或设置已更新，请重新导出。');}
}
async function run(id,action){
 const controls=[$('import'),$('quote'),$('export')];controls.forEach(button=>button.disabled=true);
 const locked=id==='export'?Array.from(document.querySelectorAll('#rows input,#rows select,#rows button,#add,#rematch,#tolerance,#tax-mode,#tax-rate,#discount-mode,#export-mode,#rental-period,#rental-duration,#rental-default-unit')).map(node=>({node,disabled:node.disabled})):[];
 locked.forEach(({node})=>node.disabled=true);
 if(id==='export'){invalidateDownload();exportStatus('正在生成Excel文件，请稍候……');$('export').textContent='正在生成……';}
 notice('正在本机浏览器中处理文件，请稍候……');
 try{await initializeLibrary;await action();}
 catch(error){const message=error.message||String(error);notice('处理失败：'+message);if(id==='export')exportStatus('导出失败：'+message+'\n匹配结果已保留，可以修正设置后重试。',true);}
 finally{controls.forEach(button=>button.disabled=false);locked.forEach(({node,disabled})=>node.disabled=disabled);if(id==='export')$('export').textContent='导出 Excel';}
}
function selected(id){const file=$(id).files[0];if(!file)throw Error('请先选择文件。');if(file.size>20*1024*1024)throw Error('文件超过20MB，请拆分后上传。');return file;}
function savePrices(next){localStorage.setItem(KEY,JSON.stringify(next));library=next;refresh();}
function rematchAfterImport(){rows=rows.map(row=>['人工填写','人工确认库价'].includes(row.status)?row:match(row));render();}
function matchingReport(extra=''){
 const priced=rows.filter(row=>String(row.price??'')!=='').length;
 return `${rows.length} 行植物：已填价 ${priced} 行，待确认 ${rows.length-priced} 行。${library.length?'':'价格库为空，请先在左侧导入带单价的报价表。'}${extra}`;
}
$('import').onclick=()=>run('import',async()=>{
 const file=selected('prices'),{items,skipped}=await table(file);
 const sourceId=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',await file.arrayBuffer()))).map(byte=>byte.toString(16).padStart(2,'0')).join('');
 const gifts=isIndoor?items.filter(row=>row.isGift).length:0;
 const priceItems=items.filter(row=>!isIndoor||!row.isGift).map(row=>isIndoor?{...row,unit:rentalUnit(row.unit),business:MODE}: {...row,business:MODE});
 const {entries,errors}=priceEntries(priceItems,file.name,sourceId);
 if(errors.length)throw Error('本次未保存，请修正后重试。\n'+errors.join('\n'));
 const incoming=[...entries.values()];
 const retained=library.filter(row=>row.sourceId||!incoming.some(entry=>entry.source===row.source&&priceKey(entry)===priceKey(row)&&Number(entry.price)===Number(row.price)&&(entry.billingPeriod||'')===(row.billingPeriod||'')));
 const combined=new Map(retained.map(row=>[quoteKey(row),row]));for(const [id,entry]of entries)combined.set(id,entry);
 const next=[...combined.values()];
 savePrices(next);if(rows.length)rematchAfterImport();
 notice(`${isIndoor?`室内租赁：默认单位 ${$('rental-default-unit').value}；赠送 ${gifts} 行不入库。\n`:''}导入 ${priceItems.length} 行，涉及 ${entries.size} 条有效报价。当前价格库共 ${library.length} 条报价，多来源报价将共同参与计算。`+(rows.length?'\n当前清单已自动重新填价：'+matchingReport():'')+`\n排除 ${skipped.length} 行无植物名称的费用项。`+(skipped.length?'\n'+skipped.join('\n'):''));
});
$('quote').onclick=()=>run('quote',async()=>{
 const file=selected('project');let text='',warning='';
 if(/\.(xlsx|csv)$/i.test(file.name)){const data=await table(file);if(isIndoor&&data.items.some(row=>row.billingPeriod!==$('rental-period').value))throw Error('清单计租周期与当前设置不同，请先切换计租单位后重新读取。');originalProject=/\.xlsx$/i.test(file.name)?{buffer:await file.arrayBuffer(),filename:file.name,context:data}:null;rows=data.items.map(match);warning=matchingReport(`排除 ${data.skipped.length} 行无植物名称的费用项。`);}
 else{text=await documentText(file);if(!text.trim())throw Error('没有可提取文字。扫描件和图片方案暂不支持，请使用文字版PDF/PPTX。');originalProject=null;rows=extract(text);warning='方案提取为辅助草稿，词表外品种可能遗漏。数量不推断，规格未确认时价格留空；备选方案不可直接合并计量。请核对原文。';}
 $('rental-terms').hidden=!isIndoor||!originalProject?.context.rentalReceipts?.length;if(isIndoor&&originalProject?.context.rentalReceipts?.length)$('rental-terms').textContent='原表实收：'+originalProject.context.rentalReceipts.map(receipt=>receipt.value+'元/'+PERIODS[$('rental-period').value]).join('；')+'。保留固定价时按租期计算实收；选择按行求和则重新计算，不沿用旧实收。';
 $('export-mode').value=originalProject?'original':'standard';$('export-note').textContent=originalProject?'已保留原文件：'+originalProject.filename+'。原格式导出将填入原单价/金额列，并增加报价核对页。':'当前不是XLSX原表，将导出带金额和税费汇总的标准报价表。';$('original').hidden=!text;$('text').textContent=text;render();notice(warning);
});
$('rematch').onclick=()=>{rows=rows.map(match);render();notice(matchingReport());};
$('add').onclick=()=>{rows.push({name:'',spec:'',unit:'',quantity:'',price:'',status:'待填写'});render();};
function financialOptions(){
 const text=$('tax-rate').value.trim(),taxRate=text===''?null:Number(text);
 if(taxRate!==null&&(!Number.isFinite(taxRate)||taxRate<0||taxRate>100))throw Error('税率请输入0至100之间的百分数。');
 if($('tax-mode').value==='add'&&taxRate===null)throw Error('请选择税率后再导出，避免漏计或误加税。');
 return {taxMode:$('tax-mode').value,taxRate,discountMode:$('discount-mode').value,...(isIndoor?{rental:{period:$('rental-period').value,duration:rentDuration({},$('rental-duration').value)}}:{})};
}
async function askExportConfirmation(message){
 const panel=$('export-confirm');$('export-confirm-text').textContent=message;panel.hidden=false;panel.scrollIntoView({block:'nearest'});$('export-continue').focus();
 return new Promise(resolve=>{const finish=value=>{panel.hidden=true;$('export-continue').onclick=null;$('export-cancel').onclick=null;resolve(value);};$('export-continue').onclick=()=>finish(true);$('export-cancel').onclick=()=>finish(false);});
}
function saveBlob(blob,filename){
 invalidateDownload();if(isIndoor&&!filename.startsWith('室内租赁_'))filename='室内租赁_'+filename;activeDownloadUrl=URL.createObjectURL(blob);
 const link=$('download-file');link.href=activeDownloadUrl;link.download=filename;link.textContent='点击下载 Excel · '+filename;
 $('download-ready').hidden=false;exportStatus('Excel已生成。若未自动下载，请点击下方下载链接。');
 // The anchor remains attached and its URL remains valid for a manual retry.
 // Firefox/Safari can ignore detached links or block an async automatic click.
 link.click();
}
window.addEventListener('beforeunload',()=>{if(activeDownloadUrl)URL.revokeObjectURL(activeDownloadUrl);});
for(const id of ['tax-mode','tax-rate','discount-mode','export-mode','rental-duration'])$(id).addEventListener('change',invalidateDownload);
async function exportStandard(options){
 const book=new ExcelJS.Workbook(),sheet=book.addWorksheet(isIndoor?'室内租赁报价':'植物报价清单'),amountColumn=isIndoor?'G':'F';
 sheet.addRow(['植物品种','规格','单位','数量',isIndoor?PERIODS[$('rental-period').value]+'租金单价':'单价',...(isIndoor?['租赁'+PERIODS[$('rental-period').value]+'数']:[]),'金额','匹配状态','采用库规格','报价来源','参与报价数','原始平均价']);let subtotal=0;
 rows.forEach((row,index)=>{
  const quantity=String(row.quantity??'').trim()===''?null:Number(row.quantity),price=String(row.price??'').trim()===''?null:Number(row.price),amount=isIndoor&&row.isGift?0:quantity!==null&&price!==null&&Number.isFinite(quantity)&&Number.isFinite(price)?Math.round(quantity*price*(isIndoor?rentDuration(row,options.rental.duration):1)*100)/100:null;
  if(amount!==null)subtotal+=amount;
  sheet.addRow([row.name,row.spec,row.unit,quantity,price,...(isIndoor?[rentDuration(row,options.rental.duration)]:[]),amount===null?null:{formula:isIndoor&&row.isGift?'0':`ROUND(D${index+2}*E${index+2}${isIndoor?'*F'+(index+2):''},2)`,result:amount},row.status,row.matchedSpec||'',row.source,row.quoteCount||0,row.averagePrice??null]);
 });
 const summaryRow=sheet.rowCount+2;sheet.getCell(`A${summaryRow}`).value='小计';sheet.getCell(`${amountColumn}${summaryRow}`).value={formula:`SUM(${amountColumn}2:${amountColumn}${rows.length+1})`,result:Math.round(subtotal*100)/100};
 const rate=options.taxMode==='included'?0:(options.taxRate??0),tax=Math.round(subtotal*rate)/100;
 sheet.getCell(`A${summaryRow+1}`).value='税率';sheet.getCell(`${amountColumn}${summaryRow+1}`).value=rate/100;sheet.getCell(`${amountColumn}${summaryRow+1}`).numFmt='0.00%';
 sheet.getCell(`A${summaryRow+2}`).value='税额';sheet.getCell(`${amountColumn}${summaryRow+2}`).value={formula:`ROUND(${amountColumn}${summaryRow}*${amountColumn}${summaryRow+1},2)`,result:tax};
 sheet.getCell(`A${summaryRow+3}`).value='合计';sheet.getCell(`${amountColumn}${summaryRow+3}`).value={formula:`${amountColumn}${summaryRow}+${amountColumn}${summaryRow+2}`,result:Math.round((subtotal+tax)*100)/100};
 sheet.getCell(`A${summaryRow+4}`).value='核对说明';sheet.getCell(`B${summaryRow+4}`).value='缺单价或数量的项目金额留空，合计仅包含已定价项目；相近规格须核对。'+(options.taxReview?'原表税费未完整继承，此文件税费待核对。':'')+(options.taxMode==='included'?'库价已含税，不重复加税。':options.taxRate===null?'未指定税率，未额外计税。':`额外计税${rate}%。`);
 if(isIndoor&&originalProject?.context.rentalReceipts?.length){
  const receiptRow=summaryRow+5;let receipt=0;
  for(const term of originalProject.context.rentalReceipts){const layout=originalProject.context.layouts.find(layout=>layout.sheet===term.sheet);receipt+=term.value*(layout?.columns.duration===undefined?options.rental.duration:1);}
  sheet.getCell(`A${receiptRow}`).value=options.discountMode==='preserve'?'实收（原表协商金额按租期）':'实收（按计算合计）';
  sheet.getCell(`${amountColumn}${receiptRow}`).value=options.discountMode==='preserve'?Math.round(receipt*100)/100:{formula:`${amountColumn}${summaryRow+3}`,result:Math.round((subtotal+tax)*100)/100};
 }
 const detail=book.addWorksheet('匹配报价依据');detail.addRow(['植物品种','目标规格','候选库名称','候选库规格','候选单价','来源']);
 for(const row of rows)for(const quote of row.matchedQuotes||[])detail.addRow([row.name,row.spec,quote.name,quote.spec,Number(quote.price),quote.source]);
 sheet.columns.forEach(c=>c.width=24);sheet.views=[{state:'frozen',ySplit:1}];book.calcProperties.fullCalcOnLoad=true;
 saveBlob(new Blob([await book.xlsx.writeBuffer()],{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}),options.taxReview?'植物报价清单_税费待核对.xlsx':'植物报价清单_含计算.xlsx');
}
$('export').onclick=()=>run('export',async()=>{
 if(!rows.length)throw Error('请先生成清单。');
 const options=financialOptions();if(isIndoor&&originalProject?.context.taxIncluded&&options.taxMode==='original'&&$('export-mode').value==='standard')options.taxMode='included';const incomplete=rows.filter(row=>String(row.price??'').trim()===''||String(row.quantity??'').trim()===''||!Number.isFinite(Number(row.price))||!Number.isFinite(Number(row.quantity)));
 if(incomplete.length&&!await askExportConfirmation(`还有 ${incomplete.length} 行缺单价或数量，金额将留空，合计不能视为完整最终报价。是否仍导出待核价文件？`)){exportStatus('已取消导出。补齐价格/数量后重试，或确认导出待核价文件。');return;}
 if($('export-mode').value==='original'){
  if(!originalProject)throw Error('请先上传XLSX原文件，或选择标准表导出。');
  const result=await fillOriginalXlsx(originalProject.buffer,rows,originalProject.context,options);
  saveBlob(result.blob,originalProject.filename.replace(/\.xlsx$/i,'')+(result.missing?'_待核价':'_已填价')+'.xlsx');
  notice('已在原文件中填价并计算。图片、样式和合并单元格保留，详情见新增“报价计算核对”页。'+(result.warnings.length?'\n需核对：\n'+result.warnings.join('\n'):''));
 }else{if(originalProject&&options.taxMode==='original'){if(!await askExportConfirmation('标准表不能完整沿用原Excel的税费公式。是否按补充税率（留空则不额外计税）导出税费待核对的标准表？如需保留原公式，请取消并选择原格式导出。')){exportStatus('已取消标准表导出。可切换为原格式，或明确税费口径后重试。');return;}options.taxReview=true;}await exportStandard(options);notice('已导出带金额、税额和合计公式的标准报价表。'+(options.taxReview?'原表税费未自动继承，请核对。':''));}
});
$('tolerance').onchange=()=>{rows=rows.map(row=>['人工填写','人工确认库价'].includes(row.status)?row:match(row));render();notice(matchingReport('范围内和相近规格已按新容差重新匹配。'));};
$('template').onclick=e=>{e.preventDefault();download(['植物品种','规格','单位',isIndoor?'月租金':'单价','数量',...(isIndoor?['租赁月数']:[])],[isIndoor?['发财树','H150-180 P60-80','盆','30','10','12']:['桂花','高度200cm 冠幅150cm','株','380','10']],isIndoor?'室内植物月租报价样表.xlsx':'植物报价模板.xlsx').catch(e=>notice(e.message));};
$('backup').onclick=()=>download(['植物品种','规格','单位','单价','报价来源','报价文件标识','报价工作区',...(isIndoor?['计租周期']:[])],library.map(r=>[r.name,r.spec,r.unit,r.price,r.source,r.sourceId||'',MODE,...(isIndoor?[r.billingPeriod||'month']:[])]),'植物价格库备份.xlsx').catch(e=>notice(e.message));
refresh();


// Retire the old template UI without losing prices that users saved there.
// Original template files remain in the retired store as a local recovery copy.
async function recoverLegacyPrices(){
 if(isIndoor)return;
 const marker='plant-pricing-templates-recovered-v3';if(localStorage.getItem(marker))return;
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
 const combined=new Map(library.map(row=>[quoteKey(row),row]));let recovered=0;
 for(const template of templates)for(const row of template.items||[]){
  const {entries,errors}=priceEntries([row],template.name);if(errors.length)continue;
  for(const [id,entry]of entries)if(!combined.has(id)){combined.set(id,entry);recovered++;}
 }
 if(recovered)savePrices([...combined.values()]);
 localStorage.setItem(marker,'done');
 if(recovered)notice(`已将旧模板中的 ${recovered} 条有效报价合并到植物报价库，原有报价保留。后续匹配自动查询全部来源并汇总均价。`);
}
initializeLibrary=recoverLegacyPrices().catch(error=>notice(error.message+' 现有价格库保留。'));

const settingIds=['tolerance','tax-mode','tax-rate','discount-mode','export-mode','rental-period','rental-duration','rental-default-unit'];
try{const saved=JSON.parse(localStorage.getItem(settingsKey)||'{}');for(const id of settingIds)if(saved[id]!==undefined)$(id).value=saved[id];}catch{notice('计价设置无法恢复，已使用默认值；价格库保留。');}
for(const id of settingIds)$(id).addEventListener('change',()=>{try{localStorage.setItem(settingsKey,JSON.stringify(Object.fromEntries(settingIds.map(id=>[id,$(id).value]))));}catch{notice('当前设置未能持久保存，价格库和清单仍在。');}});
function rentalLabels(){const period=PERIODS[$('rental-period').value];$('price-heading').textContent=isIndoor?`单价（元/单位/${period}）`:'单价（元）';$('duration-heading').hidden=!isIndoor;$('duration-heading').textContent='租期（'+period+'）';}
$('rental-settings').hidden=!isIndoor;
$('workspace-label').textContent=isIndoor?' / 室内植物租赁助手':' / 室外植物报价助手';
$('workspace-title').textContent=isIndoor?'从室内绿植，到租赁报价。':'从植物清单，到室外报价。';
$('library-title').textContent=isIndoor?'导入室内植物租赁价格库':'导入室外植物报价库';
$('library-summary').textContent=isIndoor?'查看室内租赁价格库':'查看室外价格库';
$('project-title').textContent=isIndoor?'室内租赁清单':'室外项目清单';
document.title=isIndoor?'植价 · 室内植物租赁':'植价 · 室外植物报价';
$('rental-duration').addEventListener('change',()=>{if(isIndoor)render();});
$('rental-period').onchange=()=>{rentalLabels();rows=rows.map(row=>match({...row,unit:rentalUnit(row.unit),billingPeriod:$('rental-period').value}));render();notice('已按'+PERIODS[$('rental-period').value]+'重新匹配，仅使用该计租周期报价；租期数值未做天/月/年换算，请确认。');};
if(isIndoor){$('library-title').nextElementSibling.textContent='支持XLSX/CSV月租、日租、年租报价。可识别“元/月”等列；缺单位时采用缺省单位。赠送行不作为常规价格入库。本库与室外报价完全隔离。';}
rentalLabels();
if(parent!==window){
 const measure=()=>parent.postMessage({type:'plant-workspace-height',height:document.body.offsetHeight+48},location.origin);
 new ResizeObserver(()=>requestAnimationFrame(measure)).observe(document.body);
 window.addEventListener('message',event=>{if(event.origin===location.origin&&event.source===parent&&event.data?.type==='plant-workspace-measure')measure();});
 measure();
}
