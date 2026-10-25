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
 return Number.isFinite(low)&&Number.isFinite(high)?{dimension,low,high,explicitUnit:/(?:mm|毫米|cm|厘米|米|m)(?:\s|$|[-~～])/i.test(String(spec))}:null;
}
function intervalMatch(a,b,tolerance){
 const pointA=a.low===a.high,pointB=b.low===b.high;
 if(pointA&&a.low>=b.low&&a.low<=b.high||pointB&&b.low>=a.low&&b.low<=a.high)return true;
 if(a.low>=b.low&&a.high<=b.high||b.low>=a.low&&b.high<=a.high)return true;
 const gap=Math.max(a.low-b.high,b.low-a.high,0);
 if(gap>0)return gap/Math.max(a.high,b.high,1)<=tolerance;
 return Math.abs((a.low+a.high)/2-(b.low+b.high)/2)/Math.max((a.low+a.high+b.low+b.high)/4,1)<=tolerance;
}
function compatibleSpec(row,quote,tolerance){
 const requested=String(row.spec||'').trim(),available=String(quote.spec||'').trim();
 if(!requested||!available)return false;
 if(specKey(requested)===specKey(available))return '同规格';
 const distance=specificationDistance(requested,available);
 if(distance&&(distance.contained||distance.score<=tolerance+1e-9))return distance.contained?'范围内':'相近规格';
 const a=singleSize(requested),b=singleSize(available);
 if(!a||!b)return false;
 // A single 0.25 without a unit is common for 0.25 m in planting lists.
 // Only infer metres when the other side is at least 10 cm; explicit units
 // always win. This is applied only after the plant name has matched.
 let inferredMetres=false;
 if(!a.explicitUnit&&a.high>0&&a.high<=3&&b.low>=10){a.low*=100;a.high*=100;inferredMetres=true;}
 else if(!b.explicitUnit&&b.high>0&&b.high<=3&&a.low>=10){b.low*=100;b.high*=100;inferredMetres=true;}
 // H, D, L and an unlabelled size can denote overall height/diameter for
 // potted foliage. On trees sold by 株, D may mean trunk diameter: keep the
 // dimension distinction there unless the species is a known foliage alias.
 const potted=['盆','组'].includes(unitKey(row.unit))||['盆','组'].includes(unitKey(quote.unit))||/^(绿萝|虎皮兰|吊兰|白掌|红掌|金钱树|龟背竹|竹芋)$/.test(plantNameKey(row.name));
 const unlabelled=(a.dimension==='s'||b.dimension==='s')&&['h','l','s'].includes(a.dimension)&&['h','l','s'].includes(b.dimension);
 if(a.dimension!==b.dimension&&(!(potted||unlabelled)||!['h','d','l','s'].includes(a.dimension)||!['h','d','l','s'].includes(b.dimension)||Math.max(a.high,b.high)>150))return false;
 if(intervalMatch(a,b,tolerance))return inferredMetres?'米与厘米换算':a.dimension===b.dimension?'规格范围重叠':'盆栽尺寸写法';
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
