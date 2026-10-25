import {priceEntries,quoteKey,categoryOf} from './pricing.js?v=catalog9';
import {PERIODS,rentalUnit} from './rental.js?v=dual6';
import {createExcelPreview} from './excel-preview.js?v=catalog9';
import {fillOriginalXlsx} from './original-xlsx.js?v=catalog9';
import {editWorkbook} from './workbook-edits.js?v=catalog9';
import {loadPriceFiles,storePriceFile} from './library-files.js?v=catalog9';
const $=id=>document.getElementById(id);
const groupKey=row=>row.sourceId?'id:'+row.sourceId:'name:'+(row.source||'历史报价');
const address=(column,row)=>{let n=column+1,name='';for(;n;n=Math.floor((n-1)/26))name=String.fromCharCode(65+(n-1)%26)+name;return name+row;};
const displayField=(field,value)=>field==='category'?(value==='other'?'其他品类':'植物'):field==='chargeType'?(value==='once'?'一次性':'按周期'):field==='billingPeriod'?(value==='once'?'一次性':PERIODS[value]):value;

export function initPriceLibraryEditor({mode,getEntries,saveEntries,parse,saveBlob}){
 const indoor=mode==='indoor',drafts=new Map(),files=new Map();let state=null,busy=false;
 const status=(text,error=false)=>{$('library-status').textContent=text;$('library-status').style.color=error?'#a12e20':'#276448';};
 function groups(){const result=new Map();for(const entry of getEntries()){const key=groupKey(entry);if(!result.has(key))result.set(key,{key,name:entry.source||'历史报价',sourceId:entry.sourceId,count:0});result.get(key).count++;}return result;}
 function selectedGroup(){return groups().get($('library-source').value);}
 function refresh(){
  const current=getEntries(),validIds=new Set(current.map(row=>row.id));for(const id of drafts.keys())if(!validIds.has(id))drafts.delete(id);
  const selected=$('library-source').value,all=groups(),nameCounts=new Map(),nameOrdinals=new Map();for(const group of all.values())nameCounts.set(group.name,(nameCounts.get(group.name)||0)+1);$('library-source').replaceChildren(new Option('全部来源文件',''));
  for(const group of all.values()){const ordinal=(nameOrdinals.get(group.name)||0)+1;nameOrdinals.set(group.name,ordinal);$('library-source').add(new Option(group.name+(nameCounts.get(group.name)>1?' · 来源'+ordinal:'')+'（'+group.count+'条）',group.key));}
  if(all.has(selected))$('library-source').value=selected;
  $('library-source').disabled=!!state||busy;
  const filter=$('library-source').value,entries=getEntries().filter(entry=>!filter||groupKey(entry)===filter);$('library').replaceChildren();
  for(const stored of entries){const id=stored.id,draft=drafts.get(id)||{...stored},tr=document.createElement('tr');tr.dataset.id=id;
   const td=()=>{const cell=document.createElement('td');tr.append(cell);return cell;};
   const input=(field,label)=>{const node=document.createElement('input');node.defaultValue=draft[field]??'';node.value=draft[field]??'';node.setAttribute('aria-label',label);if(field==='price')node.inputMode='decimal';if(field==='spec'&&stored._locations?.some(origin=>origin.columns.spec===undefined&&origin.dimensions?.length)){node.disabled=true;node.title='规格分列在原文件的高度/冠幅等列中，请按原表编辑这些列。';}node.oninput=()=>{draft[field]=node.value;drafts.set(id,draft);};td().append(node);};
   for(const [field,label]of [['name','报价名称'],['spec','报价规格'],['unit','报价单位'],['price','报价单价']])input(field,label);
   const select=(field,options,label,parent)=>{const node=document.createElement('select');node.setAttribute('aria-label',label);for(const [value,text]of options)node.add(new Option(text,value));node.value=draft[field]||options[0][0];node.onchange=()=>{draft[field]=node.value;drafts.set(id,draft);};parent.append(node);return node;};
   select('category',[['plant','植物'],['other','材料 / 人工 / 其他']],'报价类别',td());
   const billing=td();if(indoor){select('chargeType',[['recurring','按周期'],['once','一次性费用']],'报价计费方式',billing);select('billingPeriod',[['month','月'],['day','天'],['year','年'],['once','一次性']],'报价计价周期',billing);}else billing.textContent='数量 × 单价';
   input('notes','报价备注');td().textContent=stored.source||'历史报价';const actions=td(),save=document.createElement('button'),reset=document.createElement('button');save.textContent='保存';reset.textContent='还原';reset.className='secondary';
   save.onclick=()=>{try{
    const candidate={...draft,category:draft.category||categoryOf(draft),unit:indoor?rentalUnit(draft.unit):String(draft.unit||'').trim()};
    if(indoor){if(candidate.chargeType==='once')candidate.billingPeriod='once';else if(candidate.billingPeriod==='once')throw Error('按周期计价请选择月、天或年；一次性费用请选择一次性计费。');}
    const checked=priceEntries([{...candidate,line:'编辑记录'}],stored.source,stored.sourceId);if(checked.errors.length)throw Error(checked.errors.join('；'));const entry={...candidate,...checked.entries.values().next().value,id:stored.id};
    if(getEntries().some(existing=>existing.id!==stored.id&&quoteKey(existing)===quoteKey(entry)))throw Error('同一来源已有完全相同的报价，请编辑该条已有记录。');
    entry._editedFields=[...new Set([...(stored._editedFields||[]),...['name','spec','unit','price','category','chargeType','billingPeriod','notes'].filter(field=>String(entry[field]??'')!==String(stored[field]??''))])];
    const next=getEntries().map(existing=>existing.id===stored.id?entry:existing);saveEntries(next);drafts.delete(id);refresh();status('已保存，当前清单已按更新后的全部报价重新匹配；手动填写的价格保留。');
   }catch(error){status('未保存：'+error.message,true);}};
   reset.onclick=()=>{drafts.delete(id);refresh();status('此条尚未保存的修改已还原。');};actions.append(save,reset);$('library').append(tr);
  }
  const group=selectedGroup(),file=group&&files.get(group.sourceId);$('library-file-open').disabled=!file||!file.filename.toLowerCase().endsWith('.xlsx');
  $('library-selection').textContent=`显示 ${entries.length} / ${getEntries().length} 条。这里的来源筛选仅用于查看和编辑，匹配始终使用全部来源。`;
  if(group&&!file)$('library-selection').textContent+=' 旧记录可直接在下表编辑；重新导入原XLSX后还可按原表版式编辑。';
 }
 function recordEdits(file){const edits=[];for(const entry of getEntries().filter(row=>row.sourceId===file.sourceId))for(const origin of entry._locations||[])for(const field of entry._editedFields||[]){const column=field==='name'?(origin.nameColumn??origin.columns.name):origin.columns[field];if(column===undefined)continue;const value=field==='price'?Number(entry.price):displayField(field,entry[field]??'');edits.push({sheet:origin.sheet,address:address(column,origin.row),value});}return edits;}
 function closeFile(){state?.view?.dispose();state=null;$('library-file-editor').hidden=true;$('library-records').hidden=false;$('library-file-open').hidden=false;$('library-source').disabled=false;}
 async function withBusy(action){if(busy)return;busy=true;const controls=Array.from(document.querySelectorAll('#price-catalog button,#price-catalog input,#price-catalog select,#import,#quote,#export,#preview,#custom-import,#save-new-all')).map(node=>({node,disabled:node.disabled}));controls.forEach(({node})=>node.disabled=true);$('library-original-preview').inert=true;try{await action();}catch(error){status(error.message,true);}finally{busy=false;controls.forEach(({node,disabled})=>node.disabled=disabled);$('library-original-preview').inert=false;refresh();}}
 async function showFile(){const group=selectedGroup(),file=group&&files.get(group.sourceId);if(!file)throw Error('请先选择已保存原文件的来源。');if(drafts.size)throw Error('请先保存或还原报价条目中的修改，再打开原表。');
  const edits=recordEdits(file),base=edits.length?await editWorkbook(file.buffer,edits):{buffer:file.buffer,cellValues:{}};
  closeFile();state={file,buffer:base.buffer,pending:new Map(),values:base.cellValues||{},view:null};const active=state;
  $('library-file-editor').hidden=false;$('library-records').hidden=true;$('library-file-open').hidden=true;$('library-file-name').textContent=file.filename;$('library-source').value=group.key;$('library-source').disabled=true;
  state.view=await createExcelPreview($('library-original-preview'),state.buffer,{showActions:false,onEdit:({sheet,address,text})=>{
   if(state!==active)return;const raw=text.trim(),numeric=raw!==''&&/^[+-]?(?:\d[\d,]*\.?\d*|\.\d+)\s*%?$/.test(raw),formula=raw.startsWith('=')?raw.slice(1):null,value=numeric?Number(raw.replace(/[,%\s]/g,''))*(raw.endsWith('%')?.01:1):text;
   active.pending.set(sheet+'!'+address,{sheet,address,value,formula});const values=structuredClone(active.values);for(const edit of active.pending.values())(values[edit.sheet]??={})[edit.address]=edit.formula?'='+edit.formula:edit.value;active.view.update([],values);status('原表有 '+active.pending.size+' 处未保存修改。点击“保存文件并更新价格库”后，新价格才参与匹配。');
  }});state.view.update([],state.values);status('可直接编辑原文件单元格，图片和格式保留。保存后更新该文件的全部报价，其他来源保持独立。');
 }
 async function saveFile(){if(!state)return;const active=state;let result=await editWorkbook(active.buffer,[...active.pending.values()]);let data=await parse(new File([result.buffer],active.file.filename,{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}));
  // Price lists often store amounts as literals. Recalculate their existing
  // money columns on save, without appending rental/audit columns to the source.
  if(data.layouts.every(layout=>['quantity','price','amount'].every(field=>layout.columns[field]!==undefined))){
   const sourceRows=data.items.map(row=>{const origin=row._origin,editedPrice=active.pending.get(origin.sheet+'!'+address(origin.columns.price,origin.row)),ownAmount=address(origin.columns.amount,origin.row),derivedFromAmount=editedPrice?.formula&&new RegExp('(^|[^A-Z0-9_])'+ownAmount+'([^A-Z0-9_]|$)','i').test(editedPrice.formula.replace(/\$/g,''));return row.isGift?{...row,amountOverride:0}:origin.priceFormula&&!origin.amountFormula&&(!editedPrice||derivedFromAmount)&&Number.isFinite(row._importedAmount)?{...row,amountOverride:row._importedAmount}:row;});
   const calculated=await fillOriginalXlsx(result.buffer,sourceRows,{...data,cellEdits:[...active.pending.values()]},{previewOnly:true}),edits=[];
   for(const layout of data.layouts)for(const cell of Object.keys(result.cellValues[layout.sheet]||{})){const number=Number(cell.match(/\d+/)[0]);if(number<=layout.headerRow||cell!==address(layout.columns.amount,number))continue;const values=calculated.cellValues[layout.sheet];if(!Object.hasOwn(values,cell))continue;edits.push({sheet:layout.sheet,address:cell,value:values[cell],formula:calculated.cellFormulas[layout.sheet]?.[cell]??null});}
   result=await editWorkbook(result.buffer,edits);result.warnings.push(...calculated.warnings);data=await parse(new File([result.buffer],active.file.filename,{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}));
  }
  const previous=getEntries(),byRow=new Map();for(const entry of previous.filter(row=>row.sourceId===active.file.sourceId))for(const origin of entry._locations||[])byRow.set(origin.sheet+'!'+origin.row,entry);
  const items=data.items.filter(row=>!indoor||!row.isGift).map(row=>{
   const prior=byRow.get(row._origin.sheet+'!'+row._origin.row),edited=prior?._editedFields||[];for(const field of ['spec','unit','category','chargeType','billingPeriod','notes'])if(edited.includes(field)&&row._origin.columns[field]===undefined&&!(field==='spec'&&row._origin.dimensions?.length))row[field]=prior[field];
   return {...row,unit:indoor?rentalUnit(row.unit):row.unit,billingPeriod:indoor?(row.chargeType==='once'?'once':row.billingPeriod):undefined,source:active.file.filename,sourceId:active.file.sourceId,business:mode};
  });const checked=priceEntries(items,active.file.filename,active.file.sourceId);if(checked.errors.length)throw Error('本次未保存，请修正原表：\n'+checked.errors.join('\n'));if(!checked.entries.size)throw Error('未读到有效报价，本次不覆盖已保存价格库。');
  const incoming=[...checked.entries.values()].map(entry=>{const prior=previous.find(row=>quoteKey(row)===quoteKey(entry));if(prior)entry.id=prior.id;const origin=entry._locations[0],old=origin&&byRow.get(origin.sheet+'!'+origin.row);entry._editedFields=(old?._editedFields||[]).filter(field=>origin.columns[field]===undefined&&!(field==='spec'&&origin.dimensions?.length)&&field!=='name'&&field!=='price');return entry;});
  await storePriceFile(mode,active.file.sourceId,active.file.filename,result.buffer);
  try{saveEntries([...previous.filter(row=>row.sourceId!==active.file.sourceId),...incoming]);}catch(error){await storePriceFile(mode,active.file.sourceId,active.file.filename,active.file.buffer);throw error;}
  files.set(active.file.sourceId,{...active.file,buffer:result.buffer});active.pending.clear();closeFile();await showFile();status('文件已保存，已更新 '+incoming.length+' 条报价并重新匹配当前清单。'+(result.warnings.length?'需核对：'+[...new Set(result.warnings)].join('；'):''));
 }
 $('library-source').onchange=()=>refresh();
 $('library-file-open').onclick=()=>withBusy(showFile);
 $('library-file-save').onclick=()=>withBusy(saveFile);
 $('library-file-cancel').onclick=()=>{if(state?.pending.size){state.pending.clear();status('已放弃本次原表未保存修改，已保存的价格库保留。');}closeFile();refresh();};
 $('library-file-download').onclick=()=>withBusy(async()=>{if(!state)return;if(state.pending.size)throw Error('请先保存文件修改，再下载，确保报价库与下载内容一致。');const file=state.file;saveBlob(new Blob([state.buffer],{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'}),file.filename.replace(/\.xlsx$/i,'')+'_价格库已编辑.xlsx');$('download-ready').scrollIntoView({block:'nearest'});status('已生成原格式文件，图片和样式保留；如未自动下载，请使用下载链接。');});
 $('manage-prices').onclick=()=>{$('price-catalog').open=true;$('price-catalog').scrollIntoView({block:'start'});};
 const ready=loadPriceFiles(mode).then(records=>{for(const file of records)files.set(file.sourceId,file);refresh();}).catch(error=>status(error.message+' 已有报价仍可在下表编辑。',true));
 refresh();
 return {refresh,ready,async registerOriginal(file,sourceId){if(!/\.xlsx$/i.test(file.name))return;const record={sourceId,filename:file.name,buffer:await file.arrayBuffer()};await storePriceFile(mode,sourceId,file.name,record.buffer);files.set(sourceId,record);refresh();}};
}
