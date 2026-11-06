export function rockerPosition(panel){
 if(!panel?.valid)return 0;
 const v=panel.volumeRaw;return v>=200&&v<560?1:v>=950&&v<1500?-1:0;
}
// Snapshot values are ceil(PWM duty ×255 / period); keep observed duty.
export const visibleLed=(duty,normal)=>duty===0?0:Math.min(1,duty/255);
// Firmware279 ADC caches; mirror measured main.c ladder bands, not gesture actions.
// Unsupported PLAY+multiple-track combinations remain unknown rather than guessed.
export function panelState(panel){
 if(!panel)return {held:[],rocker:0,lights:{},physicalLEDs:true};
 const held=[];if(panel.fn)held.push('FN');const rocker=rockerPosition(panel);
 if(panel.valid){
  const r=panel.trackRaw;let mask=0;
  const bands=[[110,0],[300,1],[488,2],[650,3],[795,4],[925,5],[1044,6],[1154,7],[1256,8],[1347,9],[1430,10],[1509,11],[1583,12],[1650,13],[1713,14],[1773,15],[1840,16],[1886,17],[1957,18],[2081,20],[2267,24]];
  if(r>=0){const band=bands.find(([limit])=>r<limit);if(band)mask=band[1];}
  for(let i=0;i<4;i++)if(mask&(1<<i))held.push('T'+(i+1));if(mask&16)held.push('PLAY');
  const v=panel.volumeRaw;if(v>=560&&v<950)held.push('VOL−');else if(v>=1500&&v<1910)held.push('VOL+');else if(v>=1910&&v<=4095)held.push('VOL−','VOL+');
 }
 const lights={};for(let i=0;i<4;i++){lights['LED.Side'+(i+1)]=visibleLed(panel.side[i],17);lights['LED.Track'+(i+1)]=visibleLed(panel.tracks[i],14);}
 return {held,rocker,lights,physicalLEDs:true};
}
