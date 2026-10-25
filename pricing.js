// Price matching is conservative: formatting and explicit unit conversions are
// equivalent; different dimensions, pots vs plants, and named variants are not.
export const norm=value=>String(value??'').normalize('NFKC').replace(/\s+/g,'').toLowerCase();
export function unitKey(value){const unit=norm(value);return ({'棵':'株','平方米':'m2','m²':'m2','㎡':'m2','立方米':'m3','m³':'m3','公斤':'kg','千克':'kg','吨':'t','人天':'工日','人·天':'工日'})[unit]||unit;}
// Classification applies equally to imported quotes, legacy records, and lists.
// Services and materials may have no specification; plants still require one.
export function categoryOf(row){
 if(/^(other|其他|其它|其他品类|其它品类|材料|人工|费用|服务|配件|非植物)$/.test(norm(row.category)))return 'other';
 if(/^(plant|植物)$/.test(norm(row.category)))return 'plant';
 if(/^(运费|运输费|人工|安装费|吊装费|拆除费|养护费|服务费|辅材|发泡胶|防水膜|土工布|陶粒|火山石|树皮|种植土|营养土|花盆$|花箱$)/.test(String(row.name||''))||/(设计费|服务费|安装费|运输费|施工费|养护费|管理费)$/.test(String(row.name||'')))return 'other';
 if(/^(袋|包|瓶|桶|箱|批|卷|张|kg|g|t|m2|m3|工|工日|人|天|台班|趟|车|项|次|件|套|米|m)$/.test(unitKey(row.unit)))return 'other';
 return 'plant';
}
export function matchingSpec(row){return categoryOf(row)==='other'&&['/','-','—','无','不适用','n/a'].includes(norm(row.spec))?'':String(row.spec||'').trim();}
export function specKey(value){
 const text=norm(value).replace(/[～~至–—]/g,'-').replace(/(高度|株高|冠幅|冠径|蓬径|蓬幅|胸径|米径|地径)[,;]*(?=\1)/g,'');
 if(!text)return '';
 const plain=text.match(/^(\d+(?:\.\d+)?)(?:-(\d+(?:\.\d+)?))?(mm|毫米|cm|厘米|m|米)?$/);
 if(plain){const factor=plain[3]==='m'||plain[3]==='米'?100:plain[3]==='mm'||plain[3]==='毫米'?0.1:1;const numeric=n=>String(Math.round(Number(n)*factor*1e6)/1e6);return 'dimensions:s:'+numeric(plain[1])+(plain[2]?'-'+numeric(plain[2]):'');}
 const dimensions={高度:'h',株高:'h',高:'h',h:'h',冠幅:'p',冠径:'p',冠:'p',蓬径:'p',蓬幅:'p',p:'p',胸径:'d',米径:'d',d:'d',地径:'g',g:'g',长度:'l',藤长:'l',l:'l'};
 const pattern=/(高度|株高|冠幅|冠径|蓬径|蓬幅|胸径|米径|地径|长度|藤长|高|冠|h|p|d|g|l)[:=]?(\d+(?:\.\d+)?)(mm|毫米|cm|厘米|m|米)?(?:-(\d+(?:\.\d+)?)(mm|毫米|cm|厘米|m|米)?)?/g;
 const tokens=[];let duplicate=false;const seen=new Set();
 const rest=text.replace(pattern,(_,label,low,lowUnit,high,highUnit)=>{
  const dimension=dimensions[label];if(seen.has(dimension))duplicate=true;seen.add(dimension);
  const numeric=(n,unit)=>{const factor=unit==='m'||unit==='米'?100:unit==='mm'||unit==='毫米'?0.1:1;return String(Math.round(Number(n)*factor*1e6)/1e6);};
  tokens.push(`${dimension}:${numeric(low,lowUnit||highUnit)}${high?'-'+numeric(high,highUnit||lowUnit):''}`);return '';
 });
 if(tokens.length&&!duplicate&&!rest.replace(/[,;，；/()×x*]/g,''))return 'dimensions:'+tokens.sort().join('|');
 // For free-text specifications preserve every descriptive word. No fuzzy or
 // nearest-size price guesses, and no removal of qualifiers such as 约/以上.
 return 'text:'+text.replace(/[，；]/g,match=>match==='，'?',':';');
}
export const priceKey=row=>[norm(row.name),specKey(matchingSpec(row)),unitKey(row.unit)].join('\u0000');
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
export const quoteKey=row=>JSON.stringify([priceKey(row),String(Number(row.price)),row.sourceId||row.source||'',row.billingPeriod||'',categoryOf(row),row.billingPeriod?(row.chargeType|| (row.billingPeriod==='once'?'once':'recurring')):'']);
export function matchPrice(row,library,{tolerance=0.1}={}){
 const result={...row,price:'',source:'',matchedSpec:'',matchedName:'',matchedQuotes:[],quoteCount:0,averagePrice:null,candidates:[]};
 if(!library.length){result.status='价格库为空，请先导入报价';return result;}
 const category=categoryOf(row),scoped=library.filter(item=>categoryOf(item)===category);
 const named=scoped.filter(item=>norm(item.name)===norm(row.name));
 const candidates=named.length?named:category==='plant'?scoped.filter(item=>norm(item.name.replace(/[A-Za-z0-9]+$/,''))===norm(row.name)):[];
 result.candidates=candidates;
 if(!candidates.length){result.status='价格库无此项目';return result;}
 const requested=matchingSpec(row);
 const eligible=candidates.filter(item=>!row.unit||!item.unit||unitKey(item.unit)===unitKey(row.unit));
 // All appropriate quotations participate, across source files and tiers.
 // Importing the same quotation twice must not give it extra weight.
 const accepted=new Map(),fallback=new Map();
 for(const item of eligible){
  const quoted=matchingSpec(item),exact=specKey(quoted)===specKey(requested),distance=requested&&quoted?specificationDistance(requested,quoted):null;
  const incomplete=(!requested||!quoted)&&!(category==='other'&&!requested&&!quoted);
  if(exact||distance&&(distance.contained||distance.score<=tolerance+1e-9))accepted.set(quoteKey(item),{...item,matchKind:incomplete?'信息不全':exact?'完全匹配':distance.contained?'范围内':'相近规格'});
  else if(incomplete)fallback.set(quoteKey(item),{...item,matchKind:'信息不全'});
 }
 if(!accepted.size)for(const [key,value]of fallback)accepted.set(key,value);
 const quotes=[...accepted.values()];
 if(!quotes.length){result.status=eligible.length?'规格超出容差，请核对':'单位不同，请核对';return result;}
 const knownUnits=new Set(quotes.map(item=>unitKey(item.unit)).filter(Boolean));
 if(knownUnits.size>1){result.status='报价计量单位不同，请先补齐单位';return result;}
 const specIncomplete=quotes.some(item=>item.matchKind==='信息不全');
 const incomplete=specIncomplete||quotes.some(item=>!item.unit||!row.unit);
 if(specIncomplete&&new Set(quotes.map(item=>Number(item.price))).size>1){result.status='名称相同但规格不全且价格不同，请补齐规格后确认';return result;}
 const average=quotes.reduce((sum,item)=>sum+Number(item.price),0)/quotes.length;
 const price=quotes.length>1?roundAveragePrice(average):Number(quotes[0].price);
 Object.assign(result,{price:String(price),unit:row.unit||quotes[0].unit,source:[...new Set(quotes.map(item=>item.source))].join('；'),matchedSpec:[...new Set(quotes.map(item=>item.spec))].join('；'),matchedName:[...new Set(quotes.map(item=>item.name))].join('；'),matchedQuotes:quotes,quoteCount:quotes.length,averagePrice:average});
 result.status=incomplete?'信息不全，按同名报价参考，请核对规格/单位':quotes.length>1?`自动均价（${quotes.length}条，${average.toFixed(2)}→${price}）`:quotes[0].matchKind==='完全匹配'?'已匹配':quotes[0].matchKind==='范围内'?'范围内采用库价':'相近规格采用库价';
 return result;
}
export function priceEntries(items,source,sourceId=''){
 const entries=new Map(),errors=[];
 for(const item of items){
  const text=String(item.price??'').replace(/[,￥¥]/g,'').trim();
  const category=categoryOf(item);
  if(!/^\d+(\.\d+)?$/.test(text)||!Number.isFinite(Number(text))){errors.push(`第${item.line??'?'}行：没有可用的非负单价`);continue;}
  if(!String(item.name||'').trim())item.name=`未命名项目（${item.line??'?'}）`;
  const chargeType=item.chargeType||(category==='other'?'once':'recurring');
  const entry={name:item.name,spec:item.spec||'',unit:item.unit,price:String(Number(text)),category,chargeType,notes:item.notes||'',source:item.source||source,sourceId:/^[a-f0-9]{64}$/i.test(item.sourceId||'')?item.sourceId.toLowerCase():item.source?'':sourceId,...(item.billingPeriod?{billingPeriod:chargeType==='once'?'once':item.billingPeriod}:{}),...(item.business?{business:item.business}:{}),_locations:item._origin?[item._origin]:item._locations||[]},key=quoteKey(entry);
  const previous=entries.get(key);if(previous)entry._locations=[...previous._locations,...entry._locations];entries.set(key,entry);
 }
 return {entries,errors};
}
