import {norm,specKey,unitKey} from './pricing.js?v=preview7';
export function itemSpec(row){return row.category==='other'&&['/','-','无','不适用','n/a'].includes(norm(row.spec))?'':row.spec||'';}
export function customKey(row){return JSON.stringify([norm(row.name),specKey(itemSpec(row)),unitKey(row.unit),row.category||'plant',row.billingPeriod||'',(row.business==='indoor'||row.billingPeriod)?row.chargeType||'':'']);}
export function validateCustom(row){
 if(!String(row.name||'').trim()||/^未命名项目/.test(row.name))throw Error('请先填写实际项目名称。');
 if(!String(row.unit||'').trim())throw Error('请填写计量单位后再加入新品库。');
 if(String(row.price??'').trim()===''||!Number.isFinite(Number(row.price))||Number(row.price)<0)throw Error('请填写有效非负单价后再加入新品库。');
 if(row.isGift)throw Error('赠送行不作为常规价格入库；若是正常报价，请先修改赠送备注。');
}
export function lineAmount(row,{indoor=false,duration=1}={}){
 if(indoor&&row.isGift)return 0;
 if(String(row.amountOverride??'').trim()!==''){const value=Number(row.amountOverride);return Number.isFinite(value)?value:null;}
 const quantity=String(row.quantity??'').replace(/,/g,'').trim(),price=String(row.price??'').trim();
 if(quantity===''||price===''||!Number.isFinite(Number(quantity))||!Number.isFinite(Number(price)))return Number.isFinite(row._importedAmount)&&row._importedAmount>0?row._importedAmount:null;
 const period=indoor&&row.chargeType!=='once'?Number(String(row.duration??'').trim()||duration):1;
 if(!Number.isFinite(period)||period<=0)return null;
 return Math.round((Number(quantity)*Number(price)*period+Number.EPSILON)*100)/100;
}
