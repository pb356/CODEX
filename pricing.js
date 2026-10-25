// Price matching is conservative: formatting and explicit unit conversions are
// equivalent; different dimensions, pots vs plants, and named variants are not.
export const norm=value=>String(value??'').normalize('NFKC').replace(/\s+/g,'').toLowerCase();
export function unitKey(value){const unit=norm(value);return ({'棵':'株','平方米':'m2','m²':'m2','㎡':'m2'})[unit]||unit;}
export function specKey(value){
 const text=norm(value).replace(/[～~至–—]/g,'-').replace(/(高度|株高|冠幅|冠径|蓬径|蓬幅|胸径|米径|地径)[,;]*(?=\1)/g,'');
 if(!text)return '';
 const dimensions={高度:'h',株高:'h',h:'h',冠幅:'p',冠径:'p',蓬径:'p',蓬幅:'p',p:'p',胸径:'d',米径:'d',d:'d',地径:'g',g:'g',长度:'l',藤长:'l',l:'l'};
 const pattern=/(高度|株高|冠幅|冠径|蓬径|蓬幅|胸径|米径|地径|长度|藤长|h|p|d|g|l)[:=]?(\d+(?:\.\d+)?)(?:-(\d+(?:\.\d+)?))?(cm|厘米|m|米)?/g;
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
export function specificationDistance(requested,quoted){
 const parse=value=>{
  const key=specKey(value);if(!key.startsWith('dimensions:'))return null;
  return Object.fromEntries(key.slice(11).split('|').map(token=>{const [dimension,value]=token.split(':');return [dimension,value.split('-').map(Number)];}));
 };
 const target=parse(requested),reference=parse(quoted);
 if(!target||!reference||Object.keys(target).sort().join()!==Object.keys(reference).sort().join())return null;
 let contained=true,score=0;
 for(const dimension of Object.keys(target)){
  const [lo,hi=lo]=target[dimension],[min,max=min]=reference[dimension];
  if(lo>hi||min>max||lo<0||min<0)return null;
  if(lo<min||hi>max)contained=false;
  // Outside distance measured from each requested endpoint to the quote band.
  // Every dimension must stay within the chosen tolerance; no averaging away
  // a large height or crown difference.
  const below=Math.max(min-lo,0)/Math.max(Math.abs(min),1),above=Math.max(hi-max,0)/Math.max(Math.abs(max),1);
  score=Math.max(score,below,above);
 }
 return {contained,score};
}
export function roundAveragePrice(value){
 const integer=Math.round(value),last=integer%10;
 if(last>=1&&last<=3)return integer-last;
 if(last>=4&&last<=7)return integer-last+5;
 if(last>=8)return integer-last+10;
 return integer;
}
export const quoteKey=row=>JSON.stringify([priceKey(row),String(Number(row.price)),row.sourceId||row.source||'',row.billingPeriod||'']);
export function matchPrice(row,library,{tolerance=0.1}={}){
 const result={...row,price:'',source:'',matchedSpec:'',matchedName:'',matchedQuotes:[],quoteCount:0,averagePrice:null,candidates:[]};
 if(!library.length){result.status='价格库为空，请先导入报价';return result;}
 const named=library.filter(item=>norm(item.name)===norm(row.name));
 const candidates=named.length?named:library.filter(item=>norm(item.name.replace(/[A-Za-z0-9]+$/,''))===norm(row.name));
 result.candidates=candidates;
 if(!candidates.length){result.status='价格库无此品种';return result;}
 if(!String(row.spec??'').trim()){result.status='规格缺失，暂不能判断适用报价';return result;}
 const eligible=candidates.filter(item=>!row.unit||unitKey(item.unit)===unitKey(row.unit));
 // All appropriate quotations participate, across source files and tiers.
 // Importing the same quotation twice must not give it extra weight.
 const accepted=new Map();
 for(const item of eligible){
  const exact=specKey(item.spec)===specKey(row.spec),distance=specificationDistance(row.spec,item.spec);
  if(exact||distance&&(distance.contained||distance.score<=tolerance+1e-9))accepted.set(quoteKey(item),{...item,matchKind:exact?'完全匹配':distance.contained?'范围内':'相近规格'});
 }
 const quotes=[...accepted.values()];
 if(!quotes.length){result.status=eligible.length?'规格超出容差，请核对':'单位不同，请核对';return result;}
 if(new Set(quotes.map(item=>unitKey(item.unit))).size>1){result.status='清单单位缺失且报价计量单位不同，暂不能混合平均';return result;}
 const average=quotes.reduce((sum,item)=>sum+Number(item.price),0)/quotes.length;
 const price=quotes.length>1?roundAveragePrice(average):Number(quotes[0].price);
 Object.assign(result,{price:String(price),unit:row.unit||quotes[0].unit,source:[...new Set(quotes.map(item=>item.source))].join('；'),matchedSpec:[...new Set(quotes.map(item=>item.spec))].join('；'),matchedName:[...new Set(quotes.map(item=>item.name))].join('；'),matchedQuotes:quotes,quoteCount:quotes.length,averagePrice:average});
 result.status=quotes.length>1?`自动均价（${quotes.length}条，${average.toFixed(2)}→${price}）`:quotes[0].matchKind==='完全匹配'?'已匹配':quotes[0].matchKind==='范围内'?'范围内采用库价':'相近规格采用库价';
 return result;
}
export function priceEntries(items,source,sourceId=''){
 const entries=new Map(),errors=[];
 for(const item of items){
  const text=String(item.price??'').replace(/[,￥¥]/g,'').trim();
  if(!item.spec||!item.unit||!/^\d+(\.\d+)?$/.test(text)||!Number.isFinite(Number(text))){errors.push(`第${item.line??'?'}行：规格、单位缺失或单价不是非负数字`);continue;}
  const entry={name:item.name,spec:item.spec,unit:item.unit,price:String(Number(text)),source:item.source||source,sourceId:/^[a-f0-9]{64}$/i.test(item.sourceId||'')?item.sourceId.toLowerCase():item.source?'':sourceId,...(item.billingPeriod?{billingPeriod:item.billingPeriod}:{}),...(item.business?{business:item.business}:{})},key=quoteKey(entry);
  entries.set(key,entry);
 }
 return {entries,errors};
}
