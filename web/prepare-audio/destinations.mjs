import {routeFiles} from './legacy-names.mjs?v=568';
export function destinationReview(files,fallback){
 const routes=routeFiles(files,fallback),fields=document.createElement('div');fields.className='destination-review';
 const table=document.createElement('table'),head=table.createTHead().insertRow();
 for(const name of ['Audio','Song','Track']){const th=document.createElement('th');th.textContent=name;head.append(th);}
 const body=table.createTBody(),inputs=[];
 for(const r of routes){const row=body.insertRow();row.insertCell().textContent=r.file.name;const pair=[];
  for(const [key,max]of [['song',16],['track',4]]){const select=document.createElement('select');select.setAttribute('aria-label',r.file.name+' '+key);
   for(let n=1;n<=max;n++){const option=document.createElement('option');option.value=String(n);option.textContent=String(n);select.append(option);}select.value=String(r[key]);row.insertCell().append(select);pair.push(select);}
  inputs.push(pair);
 }
 fields.append(table);
 return {fields,read(){return routes.map((r,i)=>{const song=Number(inputs[i][0].value),track=Number(inputs[i][1].value);if(!Number.isInteger(song)||song<1||song>16||!Number.isInteger(track)||track<1||track>4)throw Error('Choose a song and track for each file');return {...r,song,track};});}};
}
