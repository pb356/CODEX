// Price matching is conservative: formatting and explicit unit conversions are
// equivalent; different dimensions, pots vs plants, and named variants are not.
export const norm=value=>String(value??'').normalize('NFKC').replace(/\s+/g,'').toLowerCase();
export function unitKey(value){const unit=norm(value);return ({'棵':'株','平方米':'m2','m²':'m2','㎡':'m2'})[unit]||unit;}
export function specKey(value){
 const text=norm(value).replace(/[～~至–—]/g,'-');
 if(!text)return '';
 const dimensions={高度:'h',株高:'h',h:'h',冠幅:'p',冠径:'p',蓬径:'p',蓬幅:'p',p:'p',胸径:'d',米径:'d',d:'d',地径:'g',g:'g'};
 const pattern=/(高度|株高|冠幅|冠径|蓬径|蓬幅|胸径|米径|地径|h|p|d|g)[:=]?(\d+(?:\.\d+)?)(?:-(\d+(?:\.\d+)?))?(cm|厘米|m|米)?/g;
 const tokens=[];let duplicate=false;const seen=new Set();
 const rest=text.replace(pattern,(_,label,low,high,unit)=>{
  const dimension=dimensions[label];if(seen.has(dimension))duplicate=true;seen.add(dimension);
  const factor=unit==='m'||unit==='米'?100:1;
  const numeric=n=>String(Math.round(Number(n)*factor*1e6)/1e6);
  tokens.push(`${dimension}:${numeric(low)}${high?'-'+numeric(high):''}`);return '';
 });
 if(tokens.length&&!duplicate&&!rest.replace(/[,;，；/()×x*]/g,''))return 'dimensions:'+tokens.sort().join('|');
 // For free-text specifications preserve every descriptive word. No fuzzy or
 // nearest-size price guesses, and no removal of qualifiers such as 约/以上.
 return 'text:'+text.replace(/[，；]/g,match=>match==='，'?',':';');
}
export const priceKey=row=>[norm(row.name),specKey(row.spec),unitKey(row.unit)].join('\u0000');
export function matchPrice(row,library){
 const result={...row,price:'',source:'',candidates:[]};
 if(!library.length){result.status='价格库为空，请先导入报价';return result;}
 const named=library.filter(item=>norm(item.name)===norm(row.name));
 const candidates=named.length?named:library.filter(item=>norm(item.name.replace(/[A-Za-z0-9]+$/,''))===norm(row.name));
 result.candidates=candidates;
 if(!candidates.length){result.status='价格库无此品种';return result;}
 if(!String(row.spec??'').trim()){result.status='规格缺失，请确认参考报价';return result;}
 const sameSpec=named.filter(item=>specKey(item.spec)===specKey(row.spec));
 const exact=sameSpec.filter(item=>!row.unit||unitKey(item.unit)===unitKey(row.unit));
 if(exact.length===1){const item=exact[0];Object.assign(result,{price:item.price,unit:item.unit,source:item.source,status:'已匹配'});}
 else if(exact.length>1)result.status='多条报价符合，请确认参考报价';
 else if(sameSpec.length)result.status='单位不同，请确认参考报价';
 else result.status='规格或名称版本不同，请确认参考报价';
 return result;
}
export function priceEntries(items,source){
 const entries=new Map(),errors=[];
 for(const item of items){
  const text=String(item.price??'').replace(/[,￥¥]/g,'').trim();
  if(!item.spec||!item.unit||!/^\d+(\.\d+)?$/.test(text)||!Number.isFinite(Number(text))){errors.push(`第${item.line??'?'}行：规格、单位缺失或单价不是非负数字`);continue;}
  const entry={name:item.name,spec:item.spec,unit:item.unit,price:String(Number(text)),source},key=priceKey(entry);
  if(entries.has(key)&&entries.get(key).price!==entry.price)errors.push(`同一植物规格出现冲突单价：${item.name} / ${item.spec}`);
  entries.set(key,entry);
 }
 return {entries,errors};
}
