// OCR assets are served by this site. Image contents never leave the browser.
let workerPromise,progressCallback=()=>{};
const asset=path=>new URL(path,import.meta.url).href;
function loadScript(url){return new Promise((resolve,reject)=>{const script=document.createElement('script');script.src=url;script.onload=resolve;script.onerror=()=>reject(Error('图片识别组件加载失败，请检查网络后重试。'));document.head.append(script);});}
async function worker(){
 if(!workerPromise)workerPromise=(async()=>{
  if(!globalThis.Tesseract)await loadScript(asset('./vendor/ocr/tesseract.min.js'));
  return Tesseract.createWorker('chi_sim+eng',1,{
   workerPath:asset('./vendor/ocr/worker.min.js'),corePath:asset('./vendor/ocr/core/'),langPath:asset('./vendor/ocr/lang/'),workerBlobURL:false,
   logger:event=>progressCallback(event)
  });
 })().catch(error=>{workerPromise=null;throw error;});
 return workerPromise;
}
async function pngImage(file){
 const bitmap=await createImageBitmap(file);try{
  const scale=Math.min(1,3200/Math.max(bitmap.width,bitmap.height));const canvas=document.createElement('canvas');
  canvas.width=Math.max(1,Math.round(bitmap.width*scale));canvas.height=Math.max(1,Math.round(bitmap.height*scale));
  const context=canvas.getContext('2d',{willReadFrequently:true});context.drawImage(bitmap,0,0,canvas.width,canvas.height);
  const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));
  if(!blob)throw Error('无法读取图片像素，请换一张清晰的图片。');
  // Full-width table rules confuse Tesseract's layout analysis. Detect long
  // straight rules, keep their coordinates for the Excel grid, and remove
  // them only from the OCR copy of the image.
  const image=context.getImageData(0,0,canvas.width,canvas.height),pixels=image.data,rows=new Uint32Array(canvas.height),columns=new Uint32Array(canvas.width);
  for(let y=0;y<canvas.height;y++)for(let x=0;x<canvas.width;x++){
   const offset=(y*canvas.width+x)*4;if(pixels[offset]<125&&pixels[offset+1]<125&&pixels[offset+2]<125){rows[y]++;columns[x]++;}
  }
  const horizontal=[],vertical=[];for(let y=0;y<rows.length;y++)if(rows[y]>canvas.width*.55)horizontal.push(y);
  for(let x=0;x<columns.length;x++)if(columns[x]>canvas.height*.55)vertical.push(x);
  for(const y of horizontal)for(let x=0;x<canvas.width;x++){const offset=(y*canvas.width+x)*4;pixels[offset]=pixels[offset+1]=pixels[offset+2]=255;}
  for(const x of vertical)for(let y=0;y<canvas.height;y++){const offset=(y*canvas.width+x)*4;pixels[offset]=pixels[offset+1]=pixels[offset+2]=255;}
  context.putImageData(image,0,0);
  const ocrBlob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));
  const groups=values=>values.reduce((all,value)=>{const last=all.at(-1);if(last&&value-last.at(-1)<=2)last.push(value);else all.push([value]);return all;},[]).map(group=>group.reduce((a,b)=>a+b,0)/group.length);
  return {blob,ocrBlob,width:canvas.width,height:canvas.height,grid:{x:groups(vertical),y:groups(horizontal)}};
 }finally{bitmap.close();}
}
function recognizedRows(tsv,grid){
 const words=String(tsv||'').split(/\r?\n/).slice(1).map(line=>line.split('\t')).filter(parts=>parts.length>=12&&parts[0]==='5').map(parts=>({left:Number(parts[6]),top:Number(parts[7]),width:Number(parts[8]),height:Number(parts[9]),confidence:Number(parts[10]),text:parts.slice(11).join('\t').trim()})).filter(word=>word.text&&word.width>0&&word.height>0&&word.confidence>=0);
 if(!words.length)return [];
 if(grid.x.length>=3&&grid.y.length>=3){
  const table=Array.from({length:grid.y.length-1},()=>Array(grid.x.length-1).fill(''));
  for(const word of words){const x=word.left+word.width/2,y=word.top+word.height/2;
   const column=grid.x.findIndex((edge,index)=>index<grid.x.length-1&&x>=edge&&x<grid.x[index+1]);
   const row=grid.y.findIndex((edge,index)=>index<grid.y.length-1&&y>=edge&&y<grid.y[index+1]);
   if(row>=0&&column>=0)table[row][column]+=word.text;
  }
  const filled=table.filter(row=>row.some(Boolean));if(filled.length)return filled;
 }
 const typical=[...words].sort((a,b)=>a.height-b.height)[Math.floor(words.length/2)].height;
 const lines=[];for(const word of words.sort((a,b)=>a.top+a.height/2-b.top-b.height/2||a.left-b.left)){
  const center=word.top+word.height/2,line=lines.find(row=>Math.abs(row.center-center)<=Math.max(5,typical*.55));
  if(line){line.words.push(word);line.center=(line.center*(line.words.length-1)+center)/line.words.length;}
  else lines.push({center,words:[word]});
 }
 const segmented=lines.sort((a,b)=>a.center-b.center).map(line=>{
  const cells=[];for(const word of line.words.sort((a,b)=>a.left-b.left)){
   const previous=cells.at(-1),gap=previous?word.left-previous.right:Infinity;
   if(previous&&gap<=Math.max(12,typical*.95)){previous.text+=word.text;previous.right=Math.max(previous.right,word.left+word.width);}
   else cells.push({left:word.left,right:word.left+word.width,text:word.text});
  }return cells;
 }).filter(cells=>cells.length);
 // Use the row with the most visible cells to set column positions. Headers
 // and data then share the same grid even when OCR splits text differently.
 const reference=[...segmented].sort((a,b)=>b.length-a.length)[0];
 if(reference.length<2)return segmented.map(cells=>[cells.map(cell=>cell.text).join(' ')]);
 const anchors=reference.map(cell=>cell.left),boundaries=anchors.slice(1).map((left,i)=>(left+anchors[i])/2);
 return segmented.map(cells=>{
  const values=Array(anchors.length).fill('');for(const cell of cells){const column=boundaries.findIndex(boundary=>cell.left<boundary);const index=column<0?anchors.length-1:column;values[index]+=cell.text;}
  return values;
 }).filter(row=>row.some(Boolean));
}
export function isImage(file){return /\.(?:png|jpe?g|webp|bmp)$/i.test(file?.name||'')||/^image\/(?:png|jpeg|webp|bmp)$/.test(file?.type||'');}
export async function imageToExcel(file,{onProgress=()=>{},purpose='project'}={}){
 if(!isImage(file))throw Error('支持 PNG、JPG、JPEG、WEBP、BMP 图片。');
 onProgress({status:'读取图片',progress:0.03});const image=await pngImage(file);
 progressCallback=onProgress;
 let result;try{const instance=await worker();await instance.setParameters({tessedit_pageseg_mode:'6'});result=await instance.recognize(image.ocrBlob,{}, {text:true,tsv:true});}catch(error){throw Error('图片文字识别失败：'+(error.message||error));}
 const values=recognizedRows(result.data.tsv,image.grid);if(!values.length)values.push(['未识别到文字，请对照原始图片核对。']);
 onProgress({status:'生成 Excel',progress:.91});
 const book=new ExcelJS.Workbook(),sheet=book.addWorksheet('识别清单');
 if(purpose==='project'&&values.length>1&&values[0].some(label=>/^(?:品种|名称|品类|植物名称|项目名称|材料名称)$/.test(label))){
  const header=values[0],priceHeader=/^(?:单价|价格|综合单价|月租金|元\/月)$/,amountHeader=/^(?:金额|合价|总价|合计金额)$/;
  if(!header.some(label=>priceHeader.test(label))){header.push('单价');for(const row of values.slice(1))row.push('');}
  if(!header.some(label=>amountHeader.test(label))){header.push('金额');for(const row of values.slice(1))row.push('');}
  const price=header.findIndex(label=>priceHeader.test(label)),amount=header.findIndex(label=>amountHeader.test(label));
  for(const row of values.slice(1)){row[price]='';row[amount]='';}
 }
 values.forEach(row=>sheet.addRow(row));sheet.columns.forEach(column=>column.width=22);sheet.views=[{state:'frozen',ySplit:1}];
 const picture=book.addWorksheet('原始图片');picture.getCell('A1').value='原始图片（请对照识别清单核对文字和数字）';picture.getColumn(1).width=28;
 const bytes=new Uint8Array(await image.blob.arrayBuffer()),imageId=book.addImage({buffer:bytes,extension:'png'});
 const scale=Math.min(1,1050/image.width);picture.addImage(imageId,{tl:{col:0,row:2},ext:{width:Math.round(image.width*scale),height:Math.round(image.height*scale)}});
 const name=file.name.replace(/\.[^.]+$/,'')+'_识别结果.xlsx';
 const converted=new File([await book.xlsx.writeBuffer()],name,{type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'});
 onProgress({status:'识别完成',progress:1});
 return {file:converted,text:result.data.text||'',rows:values.length};
}
