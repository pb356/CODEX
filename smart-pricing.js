import {categoryOf,norm,quoteKey,roundAveragePrice,specificationDistance,specKey,unitKey} from './pricing.js';

export function plantNameKey(value){
 let name=norm(value).replace(/[（(][^()（）]*[)）]$/,'');
 name=name.replace(/^([一二三四五六七八九十两\d]+)[枝杆干](发财树)$/,'$1杆$2');
 if(name==='小绿萝')name='绿萝';
 return name;
}
function singleSize(spec){
 const key=specKey(spec);if(!key.startsWith('dimensions:'))return null;
 const parts=key.slice(11).split('|');if(parts.length!==1)return null;
 const [dimension,value]=parts[0].split(':');const [low,high=low]=value.split('-').map(Number);
 return Number.isFinite(low)&&Number.isFinite(high)?{dimension,low,high}:null;
}
function compatibleSpec(row,quote,tolerance){
 const requested=String(row.spec||'').trim(),available=String(quote.spec||'').trim();
 if(!requested||!available)return false;
 if(specKey(requested)===specKey(available))return '同规格';
 const distance=specificationDistance(requested,available);
 if(distance&&(distance.contained||distance.score<=tolerance+1e-9))return distance.contained?'范围内':'相近规格';
 // A short foliage-plant label can describe overall size as H or D. Restrict
 // this equivalence to the known 绿萝 alias; chest diameter of trees is not height.
 if(plantNameKey(row.name)==='绿萝'){
  const a=singleSize(requested),b=singleSize(available);
  if(a&&b&&['h','d','s'].includes(a.dimension)&&['h','d','s'].includes(b.dimension)){
   const score=Math.max(Math.abs(a.low-b.low)/Math.max(b.low,1),Math.abs(a.high-b.high)/Math.max(b.high,1));
   if(score<=tolerance+1e-9)return '绿萝尺寸写法';
  }
 }
 return false;
}
export function smartMatchPrice(row,library,{tolerance=.1}={}){
 if(categoryOf(row)!=='plant'||!String(row.name||'').trim())return null;
 const target=plantNameKey(row.name),quotes=new Map(),reasons=new Set();
 for(const quote of library){
  if(categoryOf(quote)!=='plant'||plantNameKey(quote.name)!==target)continue;
  if(row.unit&&quote.unit&&unitKey(row.unit)!==unitKey(quote.unit))continue;
  const reason=compatibleSpec(row,quote,tolerance);if(!reason)continue;
  quotes.set(quoteKey(quote),quote);reasons.add(reason);
 }
 const found=[...quotes.values()];if(!found.length)return null;
 const units=[...new Set(found.map(quote=>unitKey(quote.unit)).filter(Boolean))];if(units.length>1)return null;
 const average=found.reduce((sum,quote)=>sum+Number(quote.price),0)/found.length;
 if(!Number.isFinite(average))return null;
 const price=found.length>1?roundAveragePrice(average):Number(found[0].price);
 return {...row,price:String(price),unit:row.unit||found[0].unit,source:[...new Set(found.map(quote=>quote.source||''))].filter(Boolean).join('；'),matchedSpec:[...new Set(found.map(quote=>quote.spec))].join('；'),matchedName:[...new Set(found.map(quote=>quote.name))].join('；'),matchedQuotes:found,quoteCount:found.length,averagePrice:average,candidates:found,status:`智能匹配（${[...reasons].join('、')}）：请核对植物与规格`,matchedFrom:'smart'};
}
