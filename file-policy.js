export const MAX_UPLOAD_BYTES=1024*1024*1024;
export function checkUpload(file){
 if(!file)throw Error('请先选择文件。');
 if(file.size>MAX_UPLOAD_BYTES)throw Error('文件超过1 GB，请拆分后上传。');
 return file;
}
