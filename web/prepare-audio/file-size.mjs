// Decimal units match Finder; every view uses the same value and unit.
export function formatFileSize(bytes){
 if(!Number.isFinite(bytes)||bytes<0)return '—';
 if(bytes<1000)return Math.round(bytes)+' B';
 const units=['KB','MB','GB','TB'];let value=bytes/1000,index=0;
 while(value>=1000&&index<units.length-1){value/=1000;index++;}
 return value.toFixed(1)+' '+units[index];
}
