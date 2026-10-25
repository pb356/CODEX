import {createExcelPreview} from './excel-preview.js?v=original8';
import {customKey,itemSpec,validateCustom,lineAmount} from './custom-items.js?v=preview7';
import {PERIODS,periodFromText,rentalUnit,rentDuration} from './rental.js?v=dual6';
import {fillOriginalXlsx} from './original-xlsx.js?v=original8';
import {norm,specKey,unitKey,priceKey,matchPrice,priceEntries,quoteKey} from './pricing.js?v=preview7';
import * as pdfjs from './vendor/pdf.mjs';
pdfjs.GlobalWorkerOptions.workerSrc = new URL('./vendor/pdf.worker.mjs', import.meta.url).href;
const $ = id => document.getElementById(id);
const MODE=new URLSearchParams(location.search).get('mode')==='indoor'?'indoor':'outdoor';
const isIndoor=MODE==='indoor';
const KEY=isIndoor?'plant-pricing-rental-library-v1':'plant-pricing-library-v1';
const settingsKey='plant-pricing-settings-'+MODE+'-v1';
const CUSTOM_KEY='plant-pricing-custom-'+MODE+'-v1';
let customLibrary=[];try{customLibrary=JSON.parse(localStorage.getItem(CUSTOM_KEY)||'[]');if(!Array.isArray(customLibrary))throw Error();}catch{customLibrary=[];}
const customDrafts=new Map();
let previewSnapshot=null;
let library=[], rows=[];
let originalProject=null;
let originalPreview=null,previewTimer=null,previewVersion=0;
let initializeLibrary=Promise.resolve();
let activeDownloadUrl=null;
function notice(text){$('notice').style.display='block';$('notice').textContent=text;}
try {library=JSON.parse(localStorage.getItem(KEY)||'[]');if(!Array.isArray(library))throw Error();}
catch {notice('无法读取浏览器价格库。请检查浏览器存储权限，或用备份文件重新导入。');}
const aliases={name:['植物品种','植物名称','品种','名称','苗木名称','苗木品种','品类','项目名称','项目','品项','项目内容','工作内容','费用名称','品名','材料名称'],spec:['规格','植物规格','苗木规格','规格cm','规格(cm)'],unit:['单位','计量单位'],price:['单价','价格','综合单价','单价(元)','含税单价','不含税单价','单价(元/株)','单价(元/棵)','月租金','月租单价','月租金单价','租金(元/月)','月单价','日租金','日租单价','年租金','年租单价','租赁单价','租赁价格','元/月','元/天','元/日','元/年','租金/月'],quantity:['数量','工程量'],amount:['金额','总价','合价','金额(元)','合计金额','总价(元)','合计(元)'],source:['报价来源','来源文件'],sourceId:['报价文件标识'],business:['报价工作区'],libraryType:['价格库类型'],category:['项目类别','类别','分类'],chargeType:['计费方式','收费方式'],billingPeriod:['计租周期'],notes:['备注','说明','计费说明'],duration:['租期','租赁月数','租期(月)','租赁天数','租期(天)','租期(日)','租赁年数','租期(年)','租赁周期数']};
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
async function table(file,{purpose='project'}={}){
 const tables=[];
 if(file.name.toLowerCase().endsWith('.csv')){
  let bytes=await file.arrayBuffer(),text;
  try{text=new TextDecoder('utf-8',{fatal:true}).decode(bytes);}catch{text=new TextDecoder('gb18030').decode(bytes);}
  tables.push({name:file.name,raw:csvParse(text.replace(/^\uFEFF/,''))});
 }else{
  const book=new ExcelJS.Workbook();await book.xlsx.load(await file.arrayBuffer());
  for(const sheet of book.worksheets){
   const raw=[],mergeTargets={},formulaCells={};
   sheet.eachRow({includeEmpty:true},row=>raw.push(Array.from({length:sheet.columnCount},(_,i)=>{const cell=row.getCell(i+1);if(cell.value&&typeof cell.value==='object'&&('formula'in cell.value||'sharedFormula'in cell.value))formulaCells[row.number+':'+i]=true;if(cell.isMerged&&cell.master.address!==cell.address){mergeTargets[row.number+':'+i]=cell.master.address;return '';}return cellValue(cell.value);} )));
   tables.push({name:sheet.name,raw,mergeTargets,formulaCells});
  }
 }
 const items=[],skipped=[],layouts=[],rentalReceipts=[];let recognized=0;
 const dimensionHeader=value=>{
  const text=norm(value),hit=text.match(/^(高度|株高|h|冠幅|冠径|蓬幅|蓬径|p|胸径|米径|d|地径|g)(?:\((cm|厘米|m|米)\)|(cm|厘米|m|米))?$/);
  return hit?{label:hit[1],unit:hit[2]||hit[3]||'cm'}:null;
 };
 for(const {name,raw,mergeTargets={},formulaCells={}} of tables){
  let columns={},dimensions=[],header=-1;
  for(let i=0;i<Math.min(raw.length,30);i++){
   const found={},dims=[];
   raw[i].forEach((value,j)=>{
    Object.entries(aliases).forEach(([key,words])=>{if(words.some(word=>norm(word)===norm(value)))found[key]=j;});
    const dimension=dimensionHeader(value);if(dimension)dims.push({...dimension,index:j});
   });
   if('name'in found&&('spec'in found||'price'in found||'amount'in found||dims.length)){columns=found;dimensions=dims;header=i;break;}
  }
  if(header<0)continue;
  if(!isIndoor&&periodFromText(raw[header][columns.price])&&/租|元[\/／]/.test(String(raw[header][columns.price])))throw Error('这份文件是按周期计价的租赁报价，请切换到室内植物租赁工作区导入。');
  if(isIndoor)for(let r=header+1;r<raw.length;r++){const label=raw[r].map(value=>String(value??'')).find(value=>/^实收(?:金额)?$/.test(value.trim()));if(label&&columns.amount!==undefined){const value=Number(raw[r][columns.amount]);if(Number.isFinite(value)&&String(raw[r][columns.amount]??'')!=='')rentalReceipts.push({sheet:name,value,row:r+1,period:periodFromText(raw[header][columns.price])||$('rental-period').value});}}
  recognized++;layouts.push({sheet:name,headerRow:header+1,columns:{...columns},dimensions:dimensions.map(d=>({...d}))});
  raw.slice(header+1).forEach((r,index)=>{
   const item=Object.fromEntries(Object.entries(columns).map(([key,j])=>[key,String(r[j]??'').trim()]));item.line=`${name} 第${header+index+2}`;
   if(item.business&&item.business!==MODE)throw Error('这份备份属于'+(item.business==='indoor'?'室内租赁':'室外植物')+'工作区，请切换后再导入，避免混入当前价格库。');
   if(isIndoor)item.billingPeriod=periodFromText(item.billingPeriod)||periodFromText(r[columns.unit])||periodFromText(raw[header][columns.price])||periodFromText(raw[header][columns.duration])||$('rental-period').value;
   if(!item.spec&&dimensions.length)item.spec=dimensions.filter(d=>String(r[d.index]??'').trim()).map(d=>{
    const value=String(r[d.index]).trim();return d.label+value+(/(?:cm|厘米|m|米)$/i.test(value)?'':d.unit);
   }).join(' ');
   const primaryName=String(item.name||'').trim();
   const headerNames=raw[header].map(value=>String(value??''));
   const candidates=headerNames.map((label,column)=>({label,column})).filter(({label})=>/^(点位|位置|项目|项目名称|费用名称|名称|品名|项目内容|工作内容|内容|品项|材料名称|备注|说明)$/.test(label.trim()));
   const fallback=candidates.find(({column})=>String(r[column]??'').trim());
   const label=primaryName||String(fallback?r[fallback.column]:'').trim();
   const summary=/^(?:\d+[.、：:]?)?(小计|合计|总计|税额|税费|税金|税率|未税合计|含税合计|价税合计|实收|应收|应付|优惠|折扣|备注[:：])/.test(norm(label));
   const numeric=value=>String(value??'').trim()!==''&&Number.isFinite(Number(String(value).replace(/,/g,'')));
   const transactional=numeric(item.quantity)||numeric(item.price)||numeric(item.amount);
   if(!transactional&&!item.spec&&/^(备注|说明|报价单位|联系人|联系电话|开户行|银行账号)/.test(primaryName))return;
   const summaryElsewhere=!primaryName&&r.some((value,column)=>column!==columns.amount&&column!==columns.price&&column!==columns.quantity&&/^(小计|合计|总计|税额|税费|税金|税率|未税合计|含税合计|价税合计|实收|应收|应付|优惠|折扣)/.test(norm(value)));
   if(summary||summaryElsewhere||aliases.name.some(word=>norm(word)===norm(primaryName)))return;
   if(!primaryName&&!transactional)return;
   const nameColumn=primaryName?columns.name:fallback?.column??columns.name;
   item.name=label||`未命名项目（${item.line}行）`;
   const stored=customLibrary.find(entry=>norm(entry.name)===norm(item.name)&&specKey(itemSpec({...item,category:entry.category}))===specKey(itemSpec(entry))&&(!item.unit||unitKey(entry.unit)===unitKey(isIndoor?rentalUnit(item.unit):item.unit)));
   const plantHeader=/植物|苗木|品种|品类/.test(String(raw[header][columns.name]||''));
   item.category=/^(其他|其它|非植物|other)$/.test(norm(item.category))?'other':/^(植物|plant)$/.test(norm(item.category))?'plant':!primaryName?'other':/^(运费|运输费|人工|安装费|吊装费|拆除费|养护费|服务费|辅材|发泡胶|防水膜|土工布|陶粒|火山石|树皮|种植土|花盆$|花箱$)/.test(item.name)||/(设计费|服务费|安装费|运输费|施工费|养护费|管理费)$/.test(item.name)?'other':stored?.category||(plantHeader||purpose==='prices'||library.some(entry=>norm(entry.name)===norm(item.name))||vocabulary.includes(item.name)||(/^(株|棵|盆)$/.test(item.unit||'')&&/h|p|高度|冠幅/i.test(item.spec||''))?'plant':'other');
   item.chargeType=norm(item.chargeType)==='once'||/一次|单次/.test(item.chargeType||'')?'once':/周期|recurring/.test(item.chargeType||'')?'recurring':stored?.chargeType||(item.category==='other'?'once':'recurring');
   item._importedPrice=item.price;
   if(numeric(item.amount))item._importedAmount=Number(item.amount);
   item._origin={sheet:name,row:header+index+2,nameColumn,amountFormula:Boolean(formulaCells[(header+index+2)+':'+columns.amount]),columns:{...columns},original:{...item}};
   const known=new Set(Object.values(columns));known.add(nameColumn);
   item._extras=headerNames.map((label,column)=>({label:label||('列'+(column+1)),column,value:String(r[column]??''),dirty:false,mergedTo:mergeTargets[(header+index+2)+':'+column]||''})).filter(field=>!known.has(field.column)&&!/图片|意向图|效果图|照片/.test(field.label));
   if(isIndoor){item.isGift=/赠送|赠品|免租/.test(item.notes||'')||/^免费(?:$|[，,;；])/.test(item.notes||'');if(item.category==='plant')item.unit=item.unit||$('rental-default-unit').value;}
   items.push(item);

  });
 }
 if(!recognized)throw Error('未找到报价表头。需要植物名称/品类列，以及规格、单价或高度/冠幅等列。已检查所有工作表的前30行。');
 if(!items.length)throw Error('没有读到植物数据。');return {items,skipped,layouts,rentalReceipts,sourceTables:tables.map(table=>({name:table.name,raw:table.raw})),taxIncluded:isIndoor&&tables.some(table=>table.raw.some(row=>row.some(value=>/(?<!不)含税/.test(String(value??'')))))};
}
const key=priceKey;
const mainMatch=row=>{
 if(isIndoor&&row.isGift)return {...row,price:row.price||'0',status:'原表赠送，不计租金',source:'原清单赠送约定',matchedQuotes:[],quoteCount:0,averagePrice:null};
 if(!isIndoor)return matchPrice(row,library,{tolerance:Number($('tolerance').value)/100});
 const period=row.chargeType==='once'?'once':$('rental-period').value;
 const selected=library.filter(quote=>(quote.billingPeriod||'month')===period).map(quote=>({...quote,unit:rentalUnit(quote.unit)}));
 const matched=matchPrice({...row,unit:rentalUnit(row.unit)},selected,{tolerance:Number($('tolerance').value)/100});
 matched.unit=row.unit||matched.unit;matched.billingPeriod=period;
 if(!selected.length&&library.length)matched.status='当前按'+(PERIODS[period]||'一次性')+'计价，库中没有该周期报价';
 return matched;
};
const match=row=>{
 const base=mainMatch(row);if(isIndoor&&row.isGift)return base;
 const period=isIndoor?(row.chargeType==='once'?'once':$('rental-period').value):'';
 const custom=customLibrary.filter(entry=>(entry.category||'plant')===(row.category||'plant')&&(!isIndoor||(entry.billingPeriod||'month')===period));
 const request={...row,spec:itemSpec(row),unit:isIndoor?rentalUnit(row.unit):row.unit};
 const exact=custom.filter(entry=>norm(entry.name)===norm(row.name)&&norm(itemSpec(entry))===norm(itemSpec(row))&&(!request.unit||unitKey(entry.unit)===unitKey(request.unit)));
 let extra;
 if(exact.length===1)extra={...row,price:exact[0].price,unit:row.unit||exact[0].unit,source:'新品类库',status:'新品类库匹配',matchedQuotes:[exact[0]],quoteCount:1,averagePrice:Number(exact[0].price),matchedSpec:exact[0].spec,_customEntryId:exact[0].id};
 else extra=matchPrice(request,custom.map(entry=>({...entry,spec:itemSpec(entry)})),{tolerance:Number($('tolerance').value)/100});
 if(String(extra.price??'')!=='')return {...extra,spec:row.spec,unit:row.unit||extra.unit,status:'新品类库匹配',matchedFrom:'custom',_origin:row._origin,_extras:row._extras};
 if(String(base.price??'')!=='')return {...base,matchedFrom:'main'};
 const retained=String(row._importedPrice??'').trim();
 if(retained!==''&&Number.isFinite(Number(retained))&&Number(retained)>=0)return {...base,price:retained,source:'原清单报价',status:'原清单报价（可编辑）',matchedFrom:'original'};
 return {...base,candidates:[...(base.candidates||[]),...custom],matchedFrom:'unmatched'};
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
 const names=[...new Set([...vocabulary,...library.map(r=>r.name),...customLibrary.map(r=>r.name),...library.map(r=>r.name.replace(/[A-Za-z0-9]+$/,''))])].filter(Boolean).sort((a,b)=>b.length-a.length);
 const canonical=new Map(names.map(n=>[norm(n),n]));
 const pattern=new RegExp([...canonical.keys()].map(s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')).join('|'),'g'),found=new Map();
 for(const line of text.split('\n'))for(const hit of norm(line).matchAll(pattern)){const name=canonical.get(hit[0]);if(!found.has(name))found.set(name,[]);found.get(name).push(line);}
 const output=[];
 for(const [name,lines] of found){const specs=[...new Set([...library,...customLibrary].filter(r=>r.name===name&&lines.some(line=>norm(line).includes(norm(r.spec)))).map(r=>r.spec))];
  for(const spec of specs.length?specs:[''])output.push(match({name,spec,unit:'',quantity:'',category:customLibrary.find(entry=>norm(entry.name)===norm(name))?.category||'plant',chargeType:customLibrary.find(entry=>norm(entry.name)===norm(name))?.chargeType||'recurring'}));
 }return output;
}
function cell(tr,text){const td=document.createElement('td');td.textContent=text;tr.append(td);return td;}
function refresh(){
 $('count').textContent=library.length;$('library').replaceChildren();
 for(const r of library){const tr=document.createElement('tr');for(const k of ['name','spec','unit','price','source'])cell(tr,k==='unit'&&isIndoor?(r.unit||'')+'/'+PERIODS[r.billingPeriod||'month']:r[k]);$('library').append(tr);}
}
function currentAmount(row){
 if(previewSnapshot&&row._origin){const value=previewSnapshot.lines?.find(line=>line.sheet===row._origin.sheet&&line.row===row._origin.row);if(value)return value.amount;}
 return lineAmount(row,{indoor:isIndoor,duration:$('rental-duration').value});
}
function updatePreviewTotals(){
 const amounts=rows.map(currentAmount),sum=amounts.reduce((sum,value)=>sum+(value??0),0),pending=amounts.filter(value=>value===null).length;
 $('summary').textContent=`共 ${rows.length} 项 · 已填价 ${rows.filter(row=>String(row.price??'')!=='').length} 项 · 缺金额 ${pending} 项`;
 $('preview-totals').textContent=previewSnapshot?`最终预览合计：¥${previewSnapshot.grand.toFixed(2)}${previewSnapshot.warnings.length?'；需核对：'+previewSnapshot.warnings.join('；'):''}`:`明细金额小计：¥${sum.toFixed(2)}${pending?`（${pending}项未定价，不是完整总价）`:''}。${originalProject?'正在按原表重新计算折扣、税费和实收，也可点击“预览最终报价”核算。':'税费在最终预览与导出时按设置计算。'}`;
}
function regularContains(row){return library.some(entry=>norm(entry.name)===norm(row.name)&&specKey(itemSpec(entry))===specKey(itemSpec(row))&&unitKey(entry.unit)===unitKey(isIndoor?rentalUnit(row.unit):row.unit));}
function storedCustom(row){const entry=customFromRow(row);return customLibrary.find(existing=>customKey(existing)===customKey(entry));}
function render({keepPreview=false}={}){
 const snapshot=previewSnapshot;invalidateDownload();previewSnapshot=keepPreview?snapshot:null;
 $('standard-preview').hidden=!!originalProject;$('excel-preview').hidden=!originalProject;$('add').hidden=!!originalProject;
 if(originalProject&&originalPreview){originalPreview.update(rows,originalOverlay());updatePreviewTotals();if(!keepPreview)scheduleOriginalCalculation();return;}
 $('rows').replaceChildren();$('empty').hidden=rows.length>0;
 rows.forEach((r,i)=>{
  const tr=document.createElement('tr');
  const inputCells={};let statusText;
  const amountInput=document.createElement('input');amountInput.setAttribute('aria-label','amount');amountInput.disabled=isIndoor&&r.isGift;const amountInfo=document.createElement('small'),recoverAmount=document.createElement('button');recoverAmount.className='secondary';recoverAmount.textContent='恢复计算';
  const amountHint=()=>{const fixed=String(r.amountOverride??'').trim()!=='';amountInfo.textContent=isIndoor&&r.isGift?'赠送，不计费':fixed?(r._manualAmount?'手动金额覆盖计算':'保留原行固定金额'):'按数量、单价和计费方式计算';recoverAmount.hidden=!fixed;};
  const addCustom=document.createElement('button');
  const update=()=>{amountHint();amountInput.disabled=isIndoor&&r.isGift;invalidateDownload();previewSnapshot=null;const value=currentAmount(r);if(document.activeElement!==amountInput)amountInput.value=value===null?'':String(value);statusText.textContent=r.status||'待填写';addCustom.textContent=storedCustom(r)?'更新新品库':'一键加入新品库';addCustom.disabled=regularContains(r)&&!storedCustom(r);updatePreviewTotals();};
  for(const key of ['name','spec','unit','quantity','price',...(isIndoor?['duration']:[])]){
   const input=document.createElement('input');input.value=r[key]??(key==='duration'?(r.chargeType==='once'?'':$('rental-duration').value):'');input.setAttribute('aria-label',key);
   if(key==='duration'&&r.chargeType==='once')input.disabled=true;
   input.oninput=()=>{r[key]=input.value;if(['name','spec','unit'].includes(key)){r.price='';r._importedPrice='';r.source='';r.candidates=[];r.matchedQuotes=[];r.quoteCount=0;r.matchedSpec='';r.averagePrice=null;statusText?.parentElement.querySelectorAll('small').forEach(node=>node.remove());r.status='已修改，待重新匹配';inputCells.price.value='';}else if(key==='price'){r.status='人工填写';r.source='人工填写';r.quoteCount=0;r.matchedQuotes=[];r.matchedSpec='';r.averagePrice=null;statusText?.parentElement.querySelectorAll('small').forEach(node=>node.remove());}if(['quantity','price','duration'].includes(key)){delete r.amountOverride;r._manualAmount=false;}update();};
   cell(tr,'').append(input);inputCells[key]=input;
  }
  amountInput.value=currentAmount(r)??'';amountInput.oninput=()=>{r.amountOverride=amountInput.value;r._manualAmount=true;r.status='人工金额';update();};recoverAmount.onclick=()=>{delete r.amountOverride;r._manualAmount=false;r.status='按数量与单价计算';update();};amountHint();cell(tr,'').append(amountInput,amountInfo,recoverAmount);
  const notes=document.createElement('input');notes.value=r.notes||'';notes.setAttribute('aria-label','notes');notes.oninput=()=>{r.notes=notes.value;if(isIndoor)r.isGift=/赠送|赠品|免租/.test(r.notes);update();};cell(tr,'').append(notes);
  const type=cell(tr,'');const category=document.createElement('select');category.setAttribute('aria-label','category');for(const [value,label]of [['plant','植物'],['other','其他品类']]){const option=document.createElement('option');option.value=value;option.textContent=label;category.append(option);}category.value=r.category||'plant';
  category.onchange=()=>{r.category=category.value;if(isIndoor)r.chargeType=r.category==='other'?'once':'recurring';r.status='人工调整分类';update();render();};type.append(category);
  if(isIndoor){const charge=document.createElement('select');charge.setAttribute('aria-label','chargeType');for(const [value,label]of [['recurring','按租期'],['once','一次性费用']]){const option=document.createElement('option');option.value=value;option.textContent=label;charge.append(option);}charge.value=r.chargeType||'recurring';charge.onchange=()=>{r.chargeType=charge.value;{delete r.amountOverride;r._manualAmount=false;}update();render();};type.append(charge);}
  const status=cell(tr,'');statusText=document.createElement('span');statusText.textContent=r.status||'待填写';status.append(statusText);
  if(r.quoteCount>1){const detail=document.createElement('small');detail.textContent=`${r.quoteCount}条报价，均价${r.averagePrice.toFixed(2)}→${r.price}`;status.append(detail);}
  if(r.matchedSpec){const basis=document.createElement('small');basis.textContent='采用库规格：'+r.matchedSpec;status.append(basis);}
  if(String(r.price??'')===''&&r.candidates?.length){
   const select=document.createElement('select');select.setAttribute('aria-label','选择参考报价');const blank=document.createElement('option');blank.value='';blank.textContent='选择参考规格';select.append(blank);
   r.candidates.forEach((quote,index)=>{const option=document.createElement('option');option.value=index;option.textContent=`${quote.name} / ${quote.spec} / ${quote.unit} / ¥${quote.price}`;select.append(option);});
   const adopt=document.createElement('button');adopt.textContent='确认采用';adopt.className='secondary';adopt.onclick=()=>{if(select.value==='')return;const quote=r.candidates[Number(select.value)];Object.assign(r,{name:quote.name,spec:quote.spec,unit:quote.unit,price:quote.price,source:quote.source,status:'人工确认库价',quoteCount:1,averagePrice:Number(quote.price),matchedQuotes:[quote],matchedSpec:quote.spec});render();};status.append(select,adopt);
  }
  if(r._extras?.length){const details=document.createElement('details'),summary=document.createElement('summary');summary.textContent='编辑其他原表字段';details.append(summary);for(const field of r._extras){const label=document.createElement('label');label.textContent=field.label+(field.mergedTo?'（合并至'+field.mergedTo+'，请在首行编辑）':'');const input=document.createElement('input');input.value=field.value;input.disabled=Boolean(field.mergedTo);input.setAttribute('aria-label','原表字段 '+field.label);input.oninput=()=>{field.value=input.value;field.dirty=true;update();};label.append(input);details.append(label);}status.append(details);}
  const actions=cell(tr,'');addCustom.textContent=storedCustom(r)?'更新新品库':'一键加入新品库';addCustom.disabled=regularContains(r)&&!storedCustom(r);addCustom.className='secondary';addCustom.onclick=()=>{try{upsertCustomRows([r]);r.status='新品报价已保存';render();notice('已写入独立新品类库，普通植物库保留。');}catch(error){notice(error.message);$('notice').scrollIntoView({block:'nearest'});}};actions.append(addCustom);
  const remove=document.createElement('button');remove.className='secondary';remove.textContent='移除';remove.onclick=()=>{rows.splice(i,1);render();};actions.append(remove);
  $('rows').append(tr);
 });
 updatePreviewTotals();
}
async function download(headers,items,filename){
 const book=new ExcelJS.Workbook(),sheet=book.addWorksheet('植物清单');sheet.addRow(headers);
 for(const row of items)sheet.addRow(row.map(v=>String(v??'')));
 sheet.columns.forEach(c=>c.width=24);sheet.views=[{state:'frozen',ySplit:1}];
 saveBlob(new Blob([await book.xlsx.writeBuffer()],{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}),filename);
}
function exportStatus(text,isError=false){const node=$('export-status');node.textContent=text;node.style.color=isError?'#a12e20':'#276448';node.setAttribute('role',isError?'alert':'status');if(isError)node.scrollIntoView({block:'nearest'});}
function invalidateDownload({keepPreview=false}={}){
 if(!keepPreview&&previewSnapshot){previewSnapshot=null;$('preview-totals').textContent='数据或计价设置已变化，请重新预览最终报价。';}
 if(activeDownloadUrl){const old=activeDownloadUrl;activeDownloadUrl=null;setTimeout(()=>URL.revokeObjectURL(old),60000);$('download-ready').hidden=true;$('download-file').removeAttribute('href');exportStatus('清单或设置已更新，请重新导出。');}
}
async function run(id,action){
 const controls=[$('import'),$('quote'),$('export'),$('preview'),$('custom-import'),$('save-new-all')];controls.forEach(button=>button.disabled=true);
 const locked=['export','preview'].includes(id)?Array.from(document.querySelectorAll('#rows input,#rows select,#rows button,#add,#rematch,#tolerance,#tax-mode,#tax-rate,#discount-mode,#export-mode,#rental-period,#rental-duration,#rental-default-unit,#save-new-all,#custom-library input,#custom-library select,#custom-library button')).map(node=>({node,disabled:node.disabled})):[];
 locked.forEach(({node})=>node.disabled=true);
 if(['export','preview'].includes(id))$('excel-preview').inert=true;
 if(id==='preview')exportStatus('正在按原表计算预览，请稍候……');
 if(id==='export'){invalidateDownload({keepPreview:true});exportStatus('正在生成Excel文件，请稍候……');$('export').textContent='正在生成……';}
 notice('正在本机浏览器中处理文件，请稍候……');
 try{await initializeLibrary;await action();}
 catch(error){const message=error.message||String(error);notice('处理失败：'+message);if(id==='custom-import'){customStatus('导入失败：'+message);$('custom-status').scrollIntoView({block:'nearest'});}if(['export','preview'].includes(id))exportStatus((id==='preview'?'预览失败：':'导出失败：')+message+'\n匹配结果已保留，可以修正设置后重试。',true);}
 finally{$('excel-preview').inert=false;controls.forEach(button=>button.disabled=false);locked.forEach(({node,disabled})=>node.disabled=disabled);if(id==='export')$('export').textContent='导出 Excel';}
}
function selected(id){const file=$(id).files[0];if(!file)throw Error('请先选择文件。');if(file.size>20*1024*1024)throw Error('文件超过20MB，请拆分后上传。');return file;}
function savePrices(next){localStorage.setItem(KEY,JSON.stringify(next));library=next;refresh();}
function rematchAfterImport(){rows=rows.map(row=>['人工填写','人工确认库价','人工金额','新品报价已保存'].includes(row.status)?row:match(row));render();}
function matchingReport(extra=''){
 const priced=rows.filter(row=>String(row.price??'')!=='').length;
 return `${rows.length} 行植物：已填价 ${priced} 行，待确认 ${rows.length-priced} 行。${library.length?'':'价格库为空，请先在左侧导入带单价的报价表。'}${extra}`;
}
$('import').onclick=()=>run('import',async()=>{
 const file=selected('prices'),{items,skipped}=await table(file,{purpose:'prices'});if(items.some(row=>row.libraryType==='custom'))throw Error('这是新品类价格库备份，请使用下方新品类库的导入入口。');
 const sourceId=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',await file.arrayBuffer()))).map(byte=>byte.toString(16).padStart(2,'0')).join('');
 const gifts=isIndoor?items.filter(row=>row.isGift).length:0;
 const priceItems=items.filter(row=>row.category!=='other'&&(!isIndoor||!row.isGift)).map(row=>isIndoor?{...row,unit:rentalUnit(row.unit),business:MODE}: {...row,business:MODE});
 const {entries,errors}=priceEntries(priceItems,file.name,sourceId);
 if(!entries.size&&!errors.length)throw Error('没有可加入普通植物库的报价。其他品类可在预览中补价后加入新品类库。');
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
 if(/\.(xlsx|csv)$/i.test(file.name)){const data=await table(file);if(isIndoor&&data.items.some(row=>row.billingPeriod!==$('rental-period').value))throw Error('清单计租周期与当前设置不同，请先切换计租单位后重新读取。');originalProject=/\.xlsx$/i.test(file.name)?{buffer:await file.arrayBuffer(),filename:file.name,context:data}:null;rows=data.items.map(match);warning=matchingReport('植物、其他品类和原有报价均已完整保留，可在预览中修改。');await mountOriginalPreview();}
 else{text=await documentText(file);if(!text.trim())throw Error('没有可提取文字。扫描件和图片方案暂不支持，请使用文字版PDF/PPTX。');originalProject=null;await mountOriginalPreview();rows=extract(text);warning='方案提取为辅助草稿，词表外品种可能遗漏。数量不推断，规格未确认时价格留空；备选方案不可直接合并计量。请核对原文。';}
 $('rental-terms').hidden=!isIndoor||!originalProject?.context.rentalReceipts?.length;if(isIndoor&&originalProject?.context.rentalReceipts?.length)$('rental-terms').textContent='原表实收：'+originalProject.context.rentalReceipts.map(receipt=>receipt.value+'元/'+PERIODS[$('rental-period').value]).join('；')+'。保留固定价时按租期计算实收；选择按行求和则重新计算，不沿用旧实收。';
 $('export-mode').value=originalProject?'original':'standard';$('export-note').textContent=originalProject?'已保留原文件：'+originalProject.filename+'。原格式导出将填入原单价/金额列，并增加报价核对页。':'当前不是XLSX原表，将导出带金额和税费汇总的标准报价表。';$('original').hidden=!text;$('text').textContent=text;render();notice(warning);
});
$('rematch').onclick=()=>{rows=rows.map(match);render();notice(matchingReport());};
$('add').onclick=()=>{rows.push({name:'',spec:'',unit:'',quantity:'',price:'',category:'other',chargeType:'once',status:'待填写'});render();};
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
 invalidateDownload({keepPreview:true});if(isIndoor&&!filename.startsWith('室内租赁_'))filename='室内租赁_'+filename;activeDownloadUrl=URL.createObjectURL(blob);
 const link=$('download-file');link.href=activeDownloadUrl;link.download=filename;link.textContent='点击下载 Excel · '+filename;
 $('download-ready').hidden=false;exportStatus('Excel已生成。若未自动下载，请点击下方下载链接。');
 // The anchor remains attached and its URL remains valid for a manual retry.
 // Firefox/Safari can ignore detached links or block an async automatic click.
 link.click();
}
window.addEventListener('beforeunload',()=>{if(activeDownloadUrl)URL.revokeObjectURL(activeDownloadUrl);});
for(const id of ['tax-mode','tax-rate','discount-mode','export-mode','rental-duration'])$(id).addEventListener('change',()=>{invalidateDownload();if(originalProject){originalPreview?.update(rows,originalOverlay());scheduleOriginalCalculation();}});
async function exportStandard(options){
 const book=new ExcelJS.Workbook(),sheet=book.addWorksheet(isIndoor?'室内租赁报价':'植物报价清单'),amountColumn=isIndoor?'G':'F';
 sheet.addRow(['植物品种','规格','单位','数量',isIndoor?PERIODS[$('rental-period').value]+'租金单价':'单价',...(isIndoor?['租赁'+PERIODS[$('rental-period').value]+'数']:[]),'金额','匹配状态','采用库规格','报价来源','参与报价数','原始平均价','备注','类别','计费方式']);let subtotal=0;
 rows.forEach((row,index)=>{
  const numeric=value=>String(value??'').trim()!==''&&Number.isFinite(Number(String(value).replace(/,/g,'')))?Number(String(value).replace(/,/g,'')):null;
  const quantity=numeric(row.quantity),price=numeric(row.price),amount=lineAmount(row,{indoor:isIndoor,duration:options.rental?.duration||1}),periodCount=isIndoor?(row.chargeType==='once'?1:rentDuration(row,options.rental.duration)):1;
  if(amount!==null)subtotal+=amount;
  sheet.addRow([row.name,row.spec,row.unit,quantity,price,...(isIndoor?[periodCount]:[]),amount===null?null:{formula:row.amountOverride!==undefined||quantity===null||price===null?String(amount):isIndoor&&row.isGift?'0':`ROUND(D${index+2}*E${index+2}${isIndoor?'*F'+(index+2):''},2)`,result:amount},row.status,row.matchedSpec||'',row.source,row.quoteCount||0,row.averagePrice??null,row.notes||'',row.category==='other'?'其他品类':'植物',row.chargeType==='once'?'一次性':'按周期']);
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
 const options=financialOptions();if(isIndoor&&originalProject?.context.taxIncluded&&options.taxMode==='original'&&$('export-mode').value==='standard')options.taxMode='included';const incomplete=rows.filter(row=>currentAmount(row)===null);
 if(incomplete.length&&!await askExportConfirmation(`还有 ${incomplete.length} 行缺单价或数量，金额将留空，合计不能视为完整最终报价。是否仍导出待核价文件？`)){exportStatus('已取消导出。补齐价格/数量后重试，或确认导出待核价文件。');return;}
 if($('export-mode').value==='original'){
  if(!originalProject)throw Error('请先上传XLSX原文件，或选择标准表导出。');
  const result=await fillOriginalXlsx(originalProject.buffer,rows,originalProject.context,options);
  saveBlob(result.blob,originalProject.filename.replace(/\.xlsx$/i,'')+(result.missing?'_待核价':'_已填价')+'.xlsx');
  notice('已在原文件中填价并计算。图片、样式和合并单元格保留，详情见新增“报价计算核对”页。'+(result.warnings.length?'\n需核对：\n'+result.warnings.join('\n'):''));
 }else{if(originalProject&&options.taxMode==='original'){if(!await askExportConfirmation('标准表不能完整沿用原Excel的税费公式。是否按补充税率（留空则不额外计税）导出税费待核对的标准表？如需保留原公式，请取消并选择原格式导出。')){exportStatus('已取消标准表导出。可切换为原格式，或明确税费口径后重试。');return;}options.taxReview=true;}await exportStandard(options);notice('已导出带金额、税额和合计公式的标准报价表。'+(options.taxReview?'原表税费未自动继承，请核对。':''));}
});
$('tolerance').onchange=()=>{rows=rows.map(row=>['人工填写','人工确认库价','人工金额','新品报价已保存'].includes(row.status)?row:match(row));render();notice(matchingReport('范围内和相近规格已按新容差重新匹配。'));};
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
$('project-title').textContent=isIndoor?'室内租赁报价预览（可编辑）':'室外报价预览（可编辑）';
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

function previewAction(row){return {label:storedCustom(row)?'更新新品库':'一键加入新品库',disabled:(regularContains(row)&&!storedCustom(row))||(isIndoor&&row.isGift),title:(row.status||'')+' · '+(row.source||'')};}
function originalOverlay(){
 const values={};const put=(sheet,address,value)=>{(values[sheet]??={})[address]=value;};
 for(const row of rows){if(!row._origin)continue;const origin=row._origin,columns=originalProject?.context.previewColumns?.[origin.sheet]||origin.columns,r=origin.row,sheet=origin.sheet;
  for(const field of ['name','spec','unit','quantity','price','notes','duration'])if(columns[field]!==undefined){const column=field==='name'?(origin.nameColumn??columns.name):columns[field];let value=row[field]??'';if(['quantity','price','duration'].includes(field)&&String(value).trim()!==''&&Number.isFinite(Number(value)))value=Number(value);if(field==='duration'&&isIndoor&&row.chargeType==='once')value='';put(sheet,excelAddress(column,r),value);}
  if(columns.amount!==undefined)put(sheet,excelAddress(columns.amount,r),currentAmount(row));
  for(const field of row._extras||[])if(field.dirty)put(sheet,excelAddress(field.column,r),field.value);
 }
 // Formula results and explicit edits have the final say over provisional line values.
 for(const [sheet,cells]of Object.entries(previewSnapshot?.cellValues||{}))Object.assign(values[sheet]??={},cells);
 for(const edit of originalProject?.context.cellEdits||[])if(!edit.formula)put(edit.sheet,edit.address,edit.value);
 return values;
}
function excelAddress(column,row){let n=column+1,name='';for(;n;n=Math.floor((n-1)/26))name=String.fromCharCode(65+(n-1)%26)+name;return name+row;}
async function mountOriginalPreview(){
 clearTimeout(previewTimer);previewVersion++;originalPreview?.dispose();originalPreview=null;
 if(!originalProject)return;
 const project=originalProject;
 originalPreview=await createExcelPreview($('excel-preview'),project.buffer,{
  context:project.context,actionState:previewAction,
  onAdd:row=>{try{upsertCustomRows([row]);row.status='新品报价已保存';originalPreview.update(rows,originalOverlay());notice('已加入独立新品类库，可在下方价格库中继续编辑。');}catch(error){notice(error.message);$('notice').scrollIntoView({block:'nearest'});}},
  onEdit:change=>{if(originalProject!==project)return;editOriginalCell(change);}
 });
}
function editOriginalCell({sheet,address,text,cell}){
 const project=originalProject;if(!project)return;
 const trimmed=text.trim(),numeric=trimmed!==''&&/^[+-]?(?:\d[\d,]*\.?\d*|\.\d+)\s*%?$/.test(trimmed);
 let value=numeric?Number(trimmed.replace(/[,%\s]/g,''))*(trimmed.endsWith('%')?.01:1):text;
 const formula=trimmed.startsWith('=')?trimmed.slice(1):null;
 const row=rows.find(row=>row._origin?.sheet===sheet&&row._origin.row===Number(address.match(/\d+/)[0]));
 const columns=row?(project.context.previewColumns?.[sheet]||row._origin.columns):{};
 const field=row?Object.keys(columns).find(key=>excelAddress(key==='name'?(row._origin.nameColumn??columns.name):columns[key],row._origin.row)===address):null;
 if(formula&&row&&['quantity','price','duration'].includes(field)){notice('数量、单价和租期请填写数值；金额或汇总单元格可输入公式。');originalPreview.update(rows,originalOverlay());return;}
 project.context.cellEdits=(project.context.cellEdits||[]).filter(edit=>edit.sheet!==sheet||edit.address!==address);
 if(row&&field&&['name','spec','unit','quantity','price','amount','notes','duration'].includes(field)&&!formula){
  if(field==='amount'){row.amountOverride=value;row._manualAmount=true;row.status='人工金额';}
  else{row[field]=String(value);if(field==='name'&&row._origin.nameColumn===columns.notes)row.notes=String(value);if(['name','spec','unit'].includes(field)){row.price='';row._importedPrice='';row.source='';row.matchedQuotes=[];row.quoteCount=0;row.matchedSpec='';row.averagePrice=null;row.status='已修改，待重新匹配';}if(['quantity','price','duration'].includes(field)){delete row.amountOverride;row._manualAmount=false;}if(field==='price'){row.status='人工填写';row.source='人工填写';row.quoteCount=0;row.matchedQuotes=[];}if(field==='notes'&&isIndoor)row.isGift=/赠送|赠品|免租/.test(row.notes);}
 }else{
  project.context.cellEdits.push({sheet,address,value,formula});
  const dimensions=project.context.layouts.find(layout=>layout.sheet===sheet)?.dimensions||[];
  const changedDimension=row&&dimensions.find(d=>excelAddress(d.index,row._origin.row)===address);
  if(changedDimension&&!formula){const extra=row._extras?.find(field=>field.column===changedDimension.index);if(extra){extra.value=String(value);extra.dirty=true;}row.spec=dimensions.map(d=>{const v=String(row._extras?.find(field=>field.column===d.index)?.value||'').trim();return v?d.label+v+(/(?:cm|厘米|m|米)$/i.test(v)?'':d.unit):'';}).filter(Boolean).join(' ');row.price='';row._importedPrice='';row.status='已修改规格，待重新匹配';row.matchedQuotes=[];row.quoteCount=0;}
 }
 invalidateDownload();previewVersion++;originalPreview.update(rows,originalOverlay());updatePreviewTotals();scheduleOriginalCalculation();
}
function scheduleOriginalCalculation(){
 clearTimeout(previewTimer);const version=++previewVersion,project=originalProject;if(!project||rows.some(row=>!row._origin))return;
 previewTimer=setTimeout(async()=>{
  try{const result=await fillOriginalXlsx(project.buffer,rows,project.context,{...financialOptions(),previewOnly:true});if(version!==previewVersion||project!==originalProject)return;
   previewSnapshot={grand:result.grand,warnings:[...result.warnings,...(result.missing?[`还有${result.missing}项未定价，此合计不完整`]:[])],lines:result.lines,cellValues:result.cellValues};originalPreview.update(rows,originalOverlay());updatePreviewTotals();
  }catch(error){if(version===previewVersion){$('preview-totals').textContent='原表预览待核对：'+error.message+'。编辑内容已保留，可修正后重新预览。';}}
 },350);
}

function customFromRow(row){
 const chargeType=row.chargeType||(row.category==='other'?'once':'recurring');
 return {id:row._customEntryId||row.id||'',name:String(row.name||'').trim(),spec:String(row.spec||'').trim(),unit:isIndoor?rentalUnit(row.unit):String(row.unit||'').trim(),price:String(row.price??'').trim(),category:row.category||'plant',chargeType,billingPeriod:isIndoor?(chargeType==='once'?'once':periodFromText(row.billingPeriod)||$('rental-period').value):'',business:MODE,notes:row.notes||'',source:'新品类库',isGift:row.isGift||false};
}
function persistCustom(next){localStorage.setItem(CUSTOM_KEY,JSON.stringify(next));customLibrary=next;renderCustomLibrary();}
function upsertCustomRows(items){
 const seen=new Map();for(const row of items){const entry=customFromRow(row),key=customKey(entry);if(seen.has(key)&&Number(seen.get(key))!==Number(entry.price))throw Error('同名称、规格和单位存在不同手填价，请先统一或逐条确认后再入库。');seen.set(key,entry.price);}
 const next=[...customLibrary];
 for(const row of items){const entry=customFromRow(row);validateCustom(entry);const index=next.findIndex(existing=>customKey(existing)===customKey(entry));entry.id=index>=0?next[index].id:crypto.randomUUID();entry.sourceId='custom:'+entry.id;entry.updatedAt=new Date().toISOString();if(index>=0)next[index]=entry;else next.push(entry);}
 persistCustom(next);
}
function customStatus(message){$('custom-status').textContent=message;}
function renderCustomLibrary(){
 $('custom-count').textContent=customLibrary.length+' 项';$('custom-rows').replaceChildren();
 const editors=[...customLibrary,...Array.from(customDrafts.values()).filter(draft=>!customLibrary.some(entry=>entry.id===draft.id))];
 for(const stored of editors){const draft=customDrafts.get(stored.id)||{...stored};const tr=document.createElement('tr');
  const field=(name,label)=>{const input=document.createElement('input');input.value=draft[name]??'';input.setAttribute('aria-label',label);input.oninput=()=>{draft[name]=input.value;customDrafts.set(draft.id,draft);};cell(tr,'').append(input);};
  for(const [key,label]of [['name','新品名称'],['spec','新品规格'],['unit','新品单位'],['price','新品单价']])field(key,label);
  const select=(key,options,label,target)=>{const input=document.createElement('select');input.setAttribute('aria-label',label);for(const [value,text]of options){const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);}input.value=options.some(([value])=>value===draft[key])?draft[key]:options[0][0];input.onchange=()=>{draft[key]=input.value;customDrafts.set(draft.id,draft);};target.append(input);};
  select('category',[['plant','植物'],['other','其他品类']],'新品类别',cell(tr,''));
  const charging=cell(tr,'');if(isIndoor)select('chargeType',[['once','一次性'],['recurring','按周期']],'新品计费方式',charging);else charging.textContent='数量×单价';if(isIndoor)select('billingPeriod',[['month','月'],['day','天'],['year','年']],'新品计租周期',charging);
  field('notes','新品备注');const actions=cell(tr,'');const save=document.createElement('button');save.textContent='保存';save.onclick=()=>{
   try{const entry=customFromRow(draft);validateCustom(entry);const duplicate=customLibrary.find(existing=>existing.id!==draft.id&&customKey(existing)===customKey(entry));if(duplicate)throw Error('同名称、规格和单位的新品报价已存在，请编辑该记录。');entry.id=draft.id;entry.sourceId='custom:'+entry.id;entry.updatedAt=new Date().toISOString();const next=customLibrary.filter(existing=>existing.id!==entry.id);next.push(entry);localStorage.setItem(CUSTOM_KEY,JSON.stringify(next));customLibrary=next;customDrafts.delete(entry.id);renderCustomLibrary();rematchAfterImport();customStatus('已保存。自动匹配会使用更新后的新品类报价；手动填写的项目价格保留。');}
   catch(error){customStatus(error.message);}
  };
  const remove=document.createElement('button');remove.className='secondary';remove.textContent='删除';let confirm=false;remove.onclick=()=>{if(!confirm){confirm=true;remove.textContent='确认删除';return;}try{const next=customLibrary.filter(existing=>existing.id!==draft.id);localStorage.setItem(CUSTOM_KEY,JSON.stringify(next));customLibrary=next;customDrafts.delete(draft.id);renderCustomLibrary();rematchAfterImport();customStatus('已删除此新品记录，普通植物库保留。');}catch(error){customStatus(error.message);}};actions.append(save,remove);$('custom-rows').append(tr);
 }
}
$('custom-add').onclick=()=>{const id=crypto.randomUUID();customDrafts.set(id,{id,name:'',spec:'',unit:'',price:'',category:'other',chargeType:'once',billingPeriod:isIndoor?$('rental-period').value:'',notes:''});renderCustomLibrary();$('custom-rows').lastElementChild?.scrollIntoView({block:'nearest'});};
$('save-new-all').onclick=()=>{
 const eligible=rows.filter(row=>!regularContains(row)&&!storedCustom(row)),valid=[],errors=[];
 for(const row of eligible)try{validateCustom(customFromRow(row));valid.push(row);}catch(error){errors.push(`${row.name||'未命名'}：${error.message}`);}
 try{if(valid.length){upsertCustomRows(valid);render();}notice(`已将 ${valid.length} 个已填价新品加入独立新品库。`+(errors.length?'\n暂未加入：\n'+errors.join('\n'):''));}catch(error){notice(error.message);}
};
$('custom-backup').onclick=()=>download(['植物品种','规格','单位','单价','项目类别','计费方式','计租周期','备注','报价工作区','价格库类型'],customLibrary.map(entry=>[entry.name,entry.spec,entry.unit,entry.price,entry.category,entry.chargeType,entry.billingPeriod||'',entry.notes,MODE,'custom']),'新品类价格库备份.xlsx').catch(error=>customStatus(error.message));
$('custom-import').onclick=()=>run('custom-import',async()=>{
 const file=selected('custom-file'),data=await table(file,{purpose:'custom'});upsertCustomRows(data.items);rematchAfterImport();customStatus('已导入新品类库，普通植物库保留；重复记录按本次单价更新。');
});
$('preview').onclick=()=>run('preview',async()=>{
 if(!rows.length)throw Error('请先读取清单。');clearTimeout(previewTimer);previewVersion++;const options=financialOptions();
 if(originalProject&&rows.every(row=>row._origin)){const result=await fillOriginalXlsx(originalProject.buffer,rows,originalProject.context,{...options,previewOnly:true});previewSnapshot={grand:result.grand,warnings:[...result.warnings,...(result.missing?[`还有${result.missing}项未定价，此合计不完整`]:[])],lines:result.lines,cellValues:result.cellValues};}
 else{const values=rows.map(row=>lineAmount(row,{indoor:isIndoor,duration:$('rental-duration').value})),subtotal=values.reduce((sum,value)=>sum+(value??0),0),rate=options.taxMode==='included'?0:options.taxRate||0;previewSnapshot={grand:Math.round(subtotal*(1+rate/100)*100)/100,warnings:[...(values.some(value=>value===null)?['仍有项目缺少金额，此合计不完整']:[]),...(originalProject?['含新增项目，当前为明细预览；原表固定折扣/实收未套用，可选择标准表导出']:[])],lines:[]};}
 render({keepPreview:true});notice('最终预览已更新。可直接在原表单元格中编辑，金额和汇总会自动更新。');
});
$('custom-title').textContent=(isIndoor?'室内租赁':'室外')+' · 新品类价格库';
renderCustomLibrary();
