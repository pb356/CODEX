// Small, non-eval Excel expression interpreter used only for cached results.
// Unsupported Excel formulas are preserved for Excel's own recalculation.
export function columnNumber(address){let n=0;for(const c of address.replace(/\d|\$/g,''))n=n*26+c.charCodeAt(0)-64;return n;}
export function columnName(n){let result='';for(;n>0;n=Math.floor((n-1)/26))result=String.fromCharCode(65+(n-1)%26)+result;return result;}
export function calculateFormula(formula,sheet,read){
 const pattern=/\s*('(?:[^']|'')+'!\$?[A-Z]+\$?\d+|[\p{L}_][\p{L}\p{N}_.]*!\$?[A-Z]+\$?\d+|\$?[A-Z]+\$?\d+|\d+(?:\.\d*)?(?:e[+-]?\d+)?|\.\d+|"(?:[^"]|"")*"|[A-Z_][A-Z0-9_.]*|<>|<=|>=|[+\-*/^%=<>,:();])/gui;
 const tokens=[];let offset=0;formula=formula.replace(/^=/,'');
 while(offset<formula.length){pattern.lastIndex=offset;const match=pattern.exec(formula);if(!match||match.index!==offset)throw Error('不支持的公式');tokens.push(match[1]);offset=pattern.lastIndex;}
 let index=0;
 const ref=token=>{const bang=token.lastIndexOf('!');return {type:'ref',sheet:bang>=0?token.slice(0,bang).replace(/^'|'$/g,'').replace(/''/g,"'"):sheet,address:token.slice(bang+1).replace(/\$/g,'').toUpperCase()};};
 function primary(){
  const token=tokens[index++];if(!token)throw Error('公式不完整');
  if(token==='+'||token==='-')return {type:'unary',op:token,arg:expression(4)};
  if(token==='('){const node=expression(0);if(tokens[index++]!==')')throw Error('公式括号不完整');return node;}
  if(/^\d|^\./.test(token))return {type:'value',value:Number(token)};
  if(token.startsWith('"'))return {type:'value',value:token.slice(1,-1).replace(/""/g,'"')};
  if(/(?:^|!)\$?[A-Z]+\$?\d+$/i.test(token))return ref(token);
  if(/^(TRUE|FALSE)$/i.test(token))return {type:'value',value:token.toUpperCase()==='TRUE'};
  if(tokens[index++]!=='(')throw Error('不支持命名区域或函数');
  const args=[];if(tokens[index]!==')')while(true){args.push(expression(0));if(tokens[index]!==','&&tokens[index]!==';')break;index++;}
  if(tokens[index++]!==')')throw Error('公式函数参数错误');return {type:'function',name:token.toUpperCase(),args};
 }
 const precedence={'=':1,'<>':1,'<':1,'>':1,'<=':1,'>=':1,'+':2,'-':2,'*':3,'/':3,'^':5,':':6};
 function expression(min){let node=primary();while(index<tokens.length){const op=tokens[index];if(op==='%'){index++;node={type:'unary',op:'%',arg:node};continue;}const power=precedence[op];if(power===undefined||power<min)break;index++;node={type:'binary',op,left:node,right:expression(op==='^'?power:power+1)};}return node;}
 const tree=expression(0);if(index!==tokens.length)throw Error('公式包含不支持的内容');
 const numeric=value=>{if(value===''||value==null)return 0;const n=Number(value);if(!Number.isFinite(n))throw Error('公式引用不是数字');return n;};
 function evaluate(node){
  if(node.type==='value')return node.value;
  if(node.type==='ref')return read(node.sheet,node.address);
  if(node.type==='unary'){const n=numeric(evaluate(node.arg));return node.op==='-'?-n:node.op==='%'?n/100:n;}
  if(node.type==='binary'){
   if(node.op===':'){
    if(node.left.type!=='ref'||node.right.type!=='ref')throw Error('不支持的区域');
    const left=node.left,right=node.right;if(left.sheet!==right.sheet)throw Error('不支持三维区域');
    const values=[];for(let r=Number(left.address.match(/\d+/)[0]);r<=Number(right.address.match(/\d+/)[0]);r++)for(let c=columnNumber(left.address);c<=columnNumber(right.address);c++)values.push(read(left.sheet,columnName(c)+r));return values;
   }
   const a=evaluate(node.left),b=evaluate(node.right);
   if(['=','<>','<','>','<=','>='].includes(node.op))return ({'=':()=>a===b,'<>':()=>a!==b,'<':()=>a<b,'>':()=>a>b,'<=':()=>a<=b,'>=':()=>a>=b})[node.op]();
   const x=numeric(a),y=numeric(b);if(node.op==='/'&&y===0)throw Error('除数为零');return ({'+':()=>x+y,'-':()=>x-y,'*':()=>x*y,'/':()=>x/y,'^':()=>x**y})[node.op]();
  }
  const args=node.args,values=()=>args.map(evaluate).flat(Infinity);
  switch(node.name){
   case 'IF':return evaluate(args[evaluate(args[0])?1:2]||{type:'value',value:false});
   case 'IFERROR':try{return evaluate(args[0]);}catch{return evaluate(args[1]);}
   case 'SUM':return values().filter(v=>typeof v==='number').reduce((a,b)=>a+b,0);
   case 'ROUND':{const n=numeric(evaluate(args[0])),factor=10**numeric(evaluate(args[1]));return Math.sign(n)*Math.round((Math.abs(n)+Number.EPSILON)*factor)/factor;}
   case 'SUBTOTAL':if(Number(evaluate(args[0]))!==9)throw Error('仅支持SUBTOTAL 9');return args.slice(1).map(evaluate).flat(Infinity).filter(v=>typeof v==='number').reduce((a,b)=>a+b,0);
   case 'AND':return values().every(Boolean);
   case 'OR':return values().some(Boolean);
   case 'MIN':return Math.min(...values().filter(v=>typeof v==='number'));
   case 'MAX':return Math.max(...values().filter(v=>typeof v==='number'));
   default:throw Error('保留原公式，由Excel重算：'+node.name);
  }
 }
 const value=evaluate(tree);if(typeof value==='number'&&!Number.isFinite(value))throw Error('公式结果异常');return value;
}
