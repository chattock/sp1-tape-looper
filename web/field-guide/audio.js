import {AUDIO_ENABLED} from './features.js';
export class AudioPreview {
 constructor(device){
  this.device=device;this.epoch=0;this.lastSend=0;this.muted=true;this.seq=0;this.authoritative=false;this.selectedSource='drums';this.enabled=AUDIO_ENABLED;if(!this.enabled)return;
  const group=document.createElement('div');group.className='audio-source';
  group.innerHTML=`<button class="audio-preview" aria-label="Mute audio" aria-pressed="false"></button><select aria-label="Audio input"><option value="drums">Simple drums</option><option value="upload">Upload audio…</option><option value="microphone">Microphone</option></select><input type="file" accept="audio/*" hidden>`;
  document.querySelector('.header-actions').prepend(group);this.button=group.querySelector('button');this.select=group.querySelector('select');this.file=group.querySelector('input');
  this.button.onclick=()=>{this.muted=!this.muted;this.updateMute();this.start();};
  this.select.onchange=()=>{const value=this.select.value;if(value==='upload'){this.select.value=this.selectedSource;this.file.click();}else this.chooseSource(value);};
  this.file.onchange=()=>this.loadFile(this.file.files[0]);this.updateMute();this.installDock(group);
  this.debug=new URLSearchParams(location.search).has('audio-debug');if(this.debug){this.debugPanel=document.createElement('output');this.debugPanel.id='audioDebug';this.debugPanel.style.cssText='position:fixed;bottom:8px;left:8px;z-index:99;white-space:pre-wrap;background:#172033;color:white;padding:10px;border-radius:10px;font:11px monospace;pointer-events:none;max-width:calc(100vw - 16px)';document.body.append(this.debugPanel);this.audioTrace=[];this.audioStatus('idle');}
  // Touch activation may arrive on release, while module loading is still pending.
  const releaseUnlock=e=>{if(e.isTrusted&&e.target.closest?.('#viewport,.audio-source,#demo,.watch-tile'))this.resumeFromGesture(e.type);};
  document.addEventListener('pointerup',releaseUnlock,{capture:true,passive:true});
  document.addEventListener('touchend',releaseUnlock,{capture:true,passive:true});
  document.addEventListener('visibilitychange',()=>{if(this.context){if(document.hidden)this.context.suspend().catch(()=>{});else this.resumeFromGesture('visible');}});
 }
 installDock(group){
  const header=document.querySelector('.topbar'),demo=document.querySelector('#demo');
  const home=document.createElement('span');home.className='audio-home';group.before(home);home.append(group);
  const slot=document.createElement('div');slot.className='demo-audio-slot';demo.append(slot);
  const mobile=matchMedia('(max-width:780px)');
  let placement='header';
  const update=()=>{
   const away=header.getBoundingClientRect().bottom<=0;
   const next=away?(mobile.matches&&!demo.hidden?'walkthrough':'floating'):'header';
   if(next===placement)return;
   const from=group.getBoundingClientRect();
   if(placement==='header'){home.style.width=from.width+'px';home.style.height=from.height+'px';}
   placement=next;group.dataset.placement=next;
   (next==='header'?home:next==='walkthrough'?slot:document.body).append(group);
   document.body.classList.toggle('audio-docked',next==='walkthrough');
   if(next==='header'){home.style.width='';home.style.height='';}
   if(!matchMedia('(prefers-reduced-motion:reduce)').matches){
    const to=group.getBoundingClientRect();
    group.getAnimations().forEach(a=>a.cancel());
    // A small local arrival motion avoids flying the control across a long scrolled page.
    group.animate([{opacity:.35,transform:'translateY('+Math.max(-12,Math.min(12,from.top-to.top))+'px) scale(.97)'},{opacity:1,transform:'none'}],{duration:260,easing:'cubic-bezier(.22,.8,.22,1)'});
   }
  };
  new IntersectionObserver(update,{threshold:0}).observe(header);
  new MutationObserver(update).observe(demo,{attributes:true,attributeFilter:['hidden']});
  mobile.addEventListener('change',update);window.addEventListener('resize',update,{passive:true});
  update();
 }
 updateMute(){this.button.innerHTML=`<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 12V5l2-3 2 3v7M10 6.5h4M10 9.5h4M8 12h8v6a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2v-6ZM12 20v2"/>${this.muted?'<path d="M3 3 21 21" stroke-width="2.2"/>':''}</svg>`;this.button.setAttribute('aria-pressed',String(this.muted));this.button.setAttribute('aria-label',this.muted?'Unmute audio':'Mute audio');this.button.title=this.muted?'Unmute audio':'Mute audio';if(this.output)this.output.gain.setValueAtTime(this.muted?0:1,this.context.currentTime);this.node?.port.postMessage({type:'input-mute',muted:this.muted});if(this.stream)for(const track of this.stream.getAudioTracks())track.enabled=!this.muted;}
 setAudioSession(type){try{if(navigator.audioSession)navigator.audioSession.type=type;}catch(error){this.audioStatus('session-unavailable');}}
 audioStatus(event){if(!this.debugPanel)return;const c=this.context;if(event){this.audioTrace.push(`${event}: ${c?.state||'none'}`);this.audioTrace=this.audioTrace.slice(-4);}this.debugPanel.textContent=`Audio ${c?.state||'none'} · ${c?.sampleRate||0} Hz · ${(c?.currentTime||0).toFixed(2)} s
Worklet output peak: ${(this.outputPeak||0).toFixed(5)} · blocks: ${this.audioMessages||0}
Input ${this.muted?'muted':'on'} · ${this.selectedSource}
${this.audioTrace.join('\n')}`;}
 resumeFromGesture(event='gesture'){
  const context=this.context;if(!context||context.state==='closed')return;
  this.audioStatus(event);
  if(context.state==='running')return;
  // Invoke synchronously. Never wait for network/WASM, or a previous resume promise.
  try{context.resume().then(()=>this.audioStatus('resumed'),()=>this.audioStatus('resume-blocked'));}catch(error){this.audioStatus('resume-blocked');}
 }
 start(){
  if(this.enabled===false)return Promise.resolve();
  this.setPaused(false);
  if(!this.context){try{this.setAudioSession(this.selectedSource==='microphone'?'play-and-record':'playback');this.context=new AudioContext({latencyHint:'interactive',sampleRate:48000});this.context.onstatechange=()=>this.audioStatus('state');}catch(error){this.device.composer.notice('Audio could not start. Tap the SP-1 again to retry.');return Promise.resolve();}}
  this.resumeFromGesture();
  if(this.starting)return this.starting;
  if(this.node)return Promise.resolve();
  this.starting=this.prepareAudio().finally(()=>{this.starting=null;});return this.starting;
 }
 async prepareAudio(){try{
  await this.context.audioWorklet.addModule('./audio-worklet.js?v=610');const module=await WebAssembly.compile(await (await fetch('./assets/firmware-dsp552.wasm?v=552')).arrayBuffer());
  const coreModule=await WebAssembly.compile(await (await fetch('./assets/firmware-core483.wasm')).arrayBuffer());
  const codecModule=await WebAssembly.compile(await (await fetch('./assets/firmware-codec.wasm')).arrayBuffer());
  const drumResponse=await fetch('./assets/looper-track.wav');if(!drumResponse.ok)throw Error('Drum sample unavailable');
  const drumBuffer=await this.context.decodeAudioData(await drumResponse.arrayBuffer());
  const defaultSource=[drumBuffer.getChannelData(0).slice(),drumBuffer.getChannelData(Math.min(1,drumBuffer.numberOfChannels-1)).slice()];
  this.node=new AudioWorkletNode(this.context,'sp1-audio',{numberOfInputs:1,channelCount:2,channelCountMode:'explicit',numberOfOutputs:1,outputChannelCount:[2],processorOptions:{module,codecModule,coreModule,defaultSource,inputMuted:this.muted,state:structuredClone(this.device.sim)}});this.output=this.context.createGain();this.output.gain.value=this.muted?0:1;this.node.connect(this.output).connect(this.context.destination);this.updateMute();
  this.authoritative=true;this.node.port.onmessage=({data:m})=>{this.outputPeak=m.outputPeak;this.audioMessages=(this.audioMessages||0)+1;this.audioStatus();if(!this.device.composer.drafting&&m.seq===this.seq)Object.assign(this.device.sim,m.state);};this.audioStatus('ready');
 }catch(error){this.device.composer.notice('Audio could not start. Tap the SP-1 again to retry.');console.error(error);if(this.context)await this.context.close().catch(()=>{});this.context=null;this.node=null;this.authoritative=false;}}
 async loadFile(file){if(!file)return;await this.start();if(!this.context)return;try{const decoded=await this.context.decodeAudioData(await file.arrayBuffer());this.upload=[decoded.getChannelData(0).slice(),decoded.getChannelData(Math.min(1,decoded.numberOfChannels-1)).slice()];this.sourceName=file.name;let option=this.select.querySelector('[value=file]');if(!option){option=document.createElement('option');option.value='file';this.select.append(option);}option.textContent=file.name;this.select.value='file';await this.chooseSource('file');this.device.composer.notice('Audio loaded. Use the SP-1 to play or record.');}catch(error){this.device.composer.notice('This audio file could not be decoded. Try WAV or MP3.');}this.file.value='';}
 stopMicrophone(){this.micNode?.disconnect();this.stream?.getTracks().forEach(t=>t.stop());this.micNode=null;this.stream=null;this.setAudioSession('playback');}
 async chooseSource(value){const token=this.sourceToken=(this.sourceToken||0)+1;await this.start();if(!this.node)return;if(token!==this.sourceToken)return;this.select.setAttribute('aria-busy','true');try{
  if(value==='microphone'){
   if(!navigator.mediaDevices?.getUserMedia)throw Error('unavailable');this.device.composer.notice('Allow microphone access to connect your input.');
   this.setAudioSession('play-and-record');const stream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:false,noiseSuppression:false,autoGainControl:false,channelCount:{ideal:2}},video:false});
   if(token!==this.sourceToken){stream.getTracks().forEach(t=>t.stop());return;}
   this.stopMicrophone();this.setAudioSession('play-and-record');this.stream=stream;this.micNode=this.context.createMediaStreamSource(stream);this.micNode.connect(this.node);for(const track of stream.getAudioTracks())track.enabled=!this.muted;
   for(const track of stream.getAudioTracks())track.onended=()=>{if(this.stream===stream){this.stopMicrophone();this.selectedSource='drums';this.select.value='drums';this.node.port.postMessage({type:'source',source:null});this.device.composer.notice('Microphone disconnected. Simple drums selected.');}};
   this.node.port.postMessage({type:'source',source:'microphone'});
  }else{this.stopMicrophone();this.node.port.postMessage({type:'source',source:value==='file'?this.upload:null});}
  this.selectedSource=value;this.select.value=value;
 }catch(error){if(token!==this.sourceToken)return;this.setAudioSession(this.stream?'play-and-record':'playback');this.select.value=this.selectedSource;this.device.composer.notice(error.name==='NotAllowedError'?'Microphone permission was not granted. Your previous input is still selected.':error.name==='NotFoundError'?'No microphone was found.': 'Microphone input could not start. Check your browser permission and input device.');}finally{if(token===this.sourceToken)this.select.removeAttribute('aria-busy');}}
 checkpoint(){const id=this.checkpointId=(this.checkpointId||0)+1;this.node?.port.postMessage({type:'checkpoint',id});return id;}
 discard(id){this.node?.port.postMessage({type:'discard',id});}
 rollback(id,state){if(this.node)this.node.port.postMessage({type:'rollback',id,seq:++this.seq,state:structuredClone(state),example:!!this.example});}
 command(method,...args){if(this.node)this.node.port.postMessage({type:'command',seq:++this.seq,method,args});}
 setPaused(value){if(this.paused===value)return;this.paused=value;this.node?.port.postMessage({type:'audio-paused',value});}
 resetExample(){this.lastStep=null;this.node?.port.postMessage({type:'reset-example'});}
 replace(state,example=false){this.setPaused(false);if(this.node)this.node.port.postMessage({type:'replace',seq:++this.seq,state:structuredClone(state),example});}
 sync(now){if(!this.node)return;const d=this.device;if(this.lastDraft!==d.composer.drafting){this.lastDraft=d.composer.drafting;this.node.port.postMessage({type:'draft',value:!!this.lastDraft});}if(d.demo){const step=d.demo.step;if(this.lastStep!==step){this.node.port.postMessage({type:'scenario',seq:++this.seq,card:d.demo.card,index:d.demo.card.steps.indexOf(step)});this.lastStep=step;}}else this.lastStep=null;if(d.demo)this.setPaused(d.demo.frozenTime!==undefined);}
}
