export const PERIODS={month:'月',day:'天',year:'年'};
export function periodFromText(value){
 const text=String(value??'').toLowerCase();
 if(text==='month'||/月/.test(text))return 'month';
 if(text==='day'||/天|日/.test(text))return 'day';
 if(text==='year'||/年/.test(text))return 'year';
 return null;
}
export function rentalUnit(value){return String(value??'').replace(/(?:\/|／|每|·)(?:月|天|日|年).*$/,'').trim();}
export function rentDuration(row,fallback){
 const raw=String(row.duration??'').trim(),n=Number(raw||fallback);
 if(!Number.isFinite(n)||n<=0)throw Error('租期必须是大于0的数字，请核对清单或租期设置。');return n;
}
