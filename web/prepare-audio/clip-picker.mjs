import {createWaveformPicker} from './waveform-picker.mjs?v=audio-source-1';
import {clipRange,formatTime,MAX_CLIP_SECONDS} from './clip-range.mjs?v=613';
export async function chooseClip(file,duration,{ask,document,stream=null}){
 const fields=document.createElement('div');fields.className='clip-picker';
 const audio=document.createElement('audio');audio.controls=true;audio.preload='metadata';audio.setAttribute('aria-label','Source audio preview');const url=URL.createObjectURL(file);audio.src=url;
 const times=document.createElement('div');times.className='clip-times';
 const input=(name,value)=>{const label=document.createElement('label');label.textContent=name;const field=document.createElement('input');field.type='text';field.inputMode='decimal';field.value=formatTime(value);field.setAttribute('aria-label',name);label.append(field);times.append(label);return field};
 let known=Number.isFinite(duration)&&duration>0?duration:null;
 const start=input('Start',0),end=input('End',Math.min(known??MAX_CLIP_SECONDS,MAX_CLIP_SECONDS));const hint=document.createElement('p');hint.setAttribute('role','status');
 fields.append(audio,times,hint);let valid=false,waveform;
 const update=()=>{try{const r=clipRange(start.value,end.value,known);hint.textContent=formatTime(r.end-r.start)+' selected · up to 8 minutes';hint.classList.remove('clip-error');valid=true}catch(e){hint.textContent=e.message;hint.classList.add('clip-error');valid=false}document.getElementById('dialogOK').disabled=!valid;waveform?.sync()};
 start.oninput=end.oninput=update;
 audio.onloadedmetadata=()=>{if(!known&&Number.isFinite(audio.duration)&&audio.duration>0)known=audio.duration;update()};
 audio.onplay=()=>{try{const r=clipRange(start.value,end.value,known);if(audio.currentTime<r.start||audio.currentTime>=r.end)audio.currentTime=r.start}catch{audio.pause()}};
 audio.ontimeupdate=()=>{const rEnd=parseFloat(end.value.includes(':')?end.value.split(':').reduce((n,x)=>n*60+Number(x),0):end.value);if(Number.isFinite(rEnd)&&audio.currentTime>=rEnd)audio.pause()};
 waveform=createWaveformPicker({file,stream,document,audio,getDuration:()=>known,getRange:()=>clipRange(start.value,end.value,known),onChange:r=>{start.value=formatTime(r.start);end.value=formatTime(r.end);update()}});fields.insertBefore(waveform.element,times);
 const ok=document.getElementById('dialogOK');
 try{const result=ask('Choose a clip',file.name+(known?' · '+formatTime(Math.floor(known)):'')+'\nImport up to 8 minutes. The source file stays unchanged.',fields,'Use clip');update();if(!await result)return null;return clipRange(start.value,end.value,known)}finally{waveform.destroy();audio.pause();audio.removeAttribute('src');audio.load();URL.revokeObjectURL(url);ok.disabled=false}
}
