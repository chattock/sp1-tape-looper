import {applyHairline} from '../model-theme/hairline.mjs';
import {confirmationCloseup} from './led-patterns.js?v=610';
import {Scenario,gestureSeconds,gestureCues,CAMERA_SETTLE_SECONDS} from './scenarios.js?v=walkthrough7-20261112';
import {AUDIO_ENABLED} from './features.js';
import {AudioPreview} from './audio.js?v=620';
import {GestureComposer} from './gestures.js?v=610';
import {Simulator} from './simulator.js?v=620';
import {loadFirmwareCore,firmwareLEDDisplay,ledFaceBrightness} from './firmware-core.js?v=610';
import * as T from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
const aliases={FN:'Button.Function',PLAY:'Button.Play','VOL−':'Button.VolumeMinus','VOL+':'Button.VolumePlus',T1:'Button.Track1',T2:'Button.Track2',T3:'Button.Track3',T4:'Button.Track4'};
const buttonAliases=Object.entries(aliases);
const views={comboRocker:{p:[-65,26,155],t:[0,0,0]},comboTop:{p:[85,85,135],t:[2,12,0]},comboHold:{p:[90,26,145],t:[2,0,0]},comboSlide:{p:[48,26,145],t:[0,0,2]},front:{p:[64,30,160],t:[0,0,0]},right:{p:[118,20,104],t:[6,0,0]},left:{p:[-120,22,94],t:[-7,9,0]},top:{p:[22,110,93],t:[-3,18,0]},ports:{p:[25,-112,90],t:[-3,-20,0]},tracks:{p:[8,-28,94],t:[-3,-17,3]},trackLights:{p:[10,-17,76],t:[-3,-13,4]},sideLights:{p:[94,13,35],t:[21,1,0]},faders:{p:[13,22,130],t:[-4,0,4]}};
const modelURL=new URL('../assets/sp1.glb?v=24-slim-round-openings',import.meta.url);
function workspaceHost(){
 try{
  if(window.parent.location.origin===location.origin&&window.parent.document.getElementById('guideFrame'))return window.parent;
 }catch{}
 return window;
}
const browsePoseKey=Symbol.for('sp1.model.browsePose');
function loadModelBytes(){
 const host=workspaceHost();
 // The Guide iframe and Companion each own their renderer and share one download.
 const key=Symbol.for(modelURL.href);
 host[key]??=fetch(modelURL).then(response=>{
  if(!response.ok)throw new Error('Model download failed');
  return response.arrayBuffer();
 }).catch(error=>{
  delete host[key];
  throw error;
 });
 return host[key];
}

export class Device {
 constructor(host,{onReady=()=>{},onInteract=()=>{},onError=()=>{}}={}){this.host=host;this.sim=new Simulator();this.coreReady=loadFirmwareCore();this.onReady=onReady;this.onInteract=onInteract;this.onError=onError;this.nodes={};this.base={};this.targets={};this.lights={};this.held=new Set();this.pointers=new Map();this.pressTimes=new Map();this.cues=new Map();this.cueTrail=new Map();this.faderValues=[1,1,1,1];this.buttonPosition=new T.Vector3();this.rockRotation=new T.Quaternion();this.rockAxis=new T.Vector3(0,1,0);this.white=new T.Color(1,1,1);this.reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;this.demo=null;this.live=true;this.composer=new GestureComposer(this);this.audio=new AudioPreview(this);this.init()}
 async init(){this.loadTimer=setTimeout(()=>this.fail(new Error('3D loading timed out')),25000);try{this.scene=new T.Scene();this.camera=new T.PerspectiveCamera(30,1,15,420);this.camera.position.set(64,30,160);this.renderer=new T.WebGLRenderer({alpha:true,antialias:true,powerPreference:'high-performance'});this.renderer.debug.onShaderError=()=>{this.shaderFailed=true};this.renderer.setPixelRatio(Math.min(devicePixelRatio,2));this.renderer.setClearColor(0,0);this.renderer.toneMapping=T.ACESFilmicToneMapping;this.renderer.toneMappingExposure=1.12;this.host.append(this.renderer.domElement);this.renderer.domElement.addEventListener('webglcontextlost',e=>this.contextLost(e));this.renderer.domElement.addEventListener('webglcontextrestored',()=>this.contextRestored());this.renderer.domElement.setAttribute('aria-label','Interactive SP-1. Drag the body to rotate. Press buttons or drag faders. Use F for FN, P for PLAY, 1–4 for tracks and arrows for the rocker.');this.renderer.domElement.setAttribute('role','img');
 this.orbit=new OrbitControls(this.camera,this.renderer.domElement);this.orbit.enableDamping=true;this.orbit.dampingFactor=.085;this.orbit.enablePan=false;this.orbit.minDistance=65;this.orbit.maxDistance=230;this.orbit.addEventListener('start',()=>{this.introElapsed=null;this.camGoal=null});
 const bytes=await loadModelBytes();
 const localBytes=bytes instanceof ArrayBuffer?bytes:new Uint8Array(bytes).slice().buffer;
 const g=await new GLTFLoader().parseAsync(localBytes,'');this.assembly=new T.Group();this.assembly.rotation.x=Math.PI/2;this.assembly.scale.setScalar(1000);this.model=g.scene;this.assembly.add(this.model);this.scene.add(this.assembly);g.scene.traverse(o=>{const a=g.parser.associations.get(o),name=a?.nodes!==undefined?g.parser.json.nodes[a.nodes].name:o.name;if(name){this.nodes[name]=o;this.base[name]={p:o.position.clone(),q:o.quaternion.clone()};o.userData.originalName=name}if(!o.isMesh)return;o.castShadow=true;o.receiveShadow=true;o.material=o.material.clone();o.material.envMapIntensity=.5;
 // Surface ink and optical windows must not compete with the housing depth or cast shadows.
 if(name?.startsWith('LED.')||name?.startsWith('Print.FunctionDot')||name==='Print.PlayTriangle'){
 const color=name.startsWith('LED.Side')?0x777b71:name==='Print.PlayTriangle'?0xc83936:0x333631;
 o.material=new T.MeshBasicMaterial({color,polygonOffset:true,polygonOffsetFactor:-2,polygonOffsetUnits:-3,depthWrite:false,toneMapped:false,side:T.FrontSide});o.castShadow=false;o.receiveShadow=false;o.renderOrder=5;
 if(name.startsWith('LED.')){
 if(!this.ledGlowTexture){const c=document.createElement('canvas');c.width=c.height=64;const ctx=c.getContext('2d'),g=ctx.createRadialGradient(32,32,0,32,32,32);g.addColorStop(0,'rgba(255,255,255,.9)');g.addColorStop(.3,'rgba(255,255,255,.5)');g.addColorStop(1,'rgba(255,255,255,0)');ctx.fillStyle=g;ctx.fillRect(0,0,64,64);this.ledGlowTexture=new T.CanvasTexture(c);}
 const glow=new T.Sprite(new T.SpriteMaterial({map:this.ledGlowTexture,color:0xffffff,transparent:true,opacity:0,depthWrite:false,blending:T.AdditiveBlending,toneMapped:false}));o.geometry.computeBoundingBox();o.geometry.boundingBox.getCenter(glow.position);const normal=o.geometry.getAttribute('normal');if(normal)glow.position.addScaledVector(new T.Vector3().fromBufferAttribute(normal,0),.00008);glow.scale.setScalar(.0018);glow.renderOrder=6;o.add(glow);
 this.lights[name]={mesh:o,glow,off:new T.Color(color),value:0};
 }
 }
 if(name?.startsWith('Print.')&&!name.startsWith('Print.FunctionDot')&&name!=='Print.PlayTriangle'){o.material.polygonOffset=true;o.material.polygonOffsetFactor=-2;o.material.polygonOffsetUnits=-3;o.material.depthWrite=false;o.castShadow=false;o.receiveShadow=false;o.renderOrder=5;}
 if(name?.includes('.Carrier')){o.material.color.setRGB(.035,.038,.029);o.material.roughness=.86;o.material.metalness=0;o.material.envMapIntensity=.12;}
 if(name?.includes('InnerWall')||name?.includes('EndWall')||name?.includes('SlotBed'))o.material.envMapIntensity=.08;
 });
 const host=workspaceHost();if(host.document.getElementById('guideFrame'))this.sharedBrowseHost=host;this.ray=new T.Raycaster();this.ndc=new T.Vector2();this.bind();this.resize();this.updateModelTheme=applyHairline(this);this.go('front',true);if(!this.reduced){const r=Math.hypot(64,30,160);this.camera.position.set(0,0,r);this.orbit.update()}await this.coreReady;await this.prepareFrame();this.ready=true;clearTimeout(this.loadTimer);this.introElapsed=this.reduced||this.sharedBrowseHost?null:0;this.onReady();this.last=performance.now();if(!this.animationStarted){this.animationStarted=true;requestAnimationFrame(t=>this.frame(t));}
 }catch(e){this.fail(e)}}
 async prepareFrame(){if(this.contextUnavailable||this.renderer.getContext().isContextLost())throw new Error('Graphics context unavailable');for(const name of [...Object.values(aliases),'Housing.Backplate','Print.RearCredits','Print.RearModel'])if(!this.nodes[name])throw new Error('Missing model part: '+name);await this.renderer.compileAsync(this.scene,this.camera);if(this.renderer.getContext().isContextLost())throw new Error('Graphics context interrupted');this.updateModelTheme?.();this.renderer.render(this.scene,this.camera);if(this.shaderFailed)throw new Error('3D shader compilation failed');}
 fail(error){clearTimeout(this.loadTimer);this.ready=false;console.error(error);this.onError(error)}
 contextLost(event){event.preventDefault();this.contextUnavailable=true;this.ready=false;document.body.dataset.model='recovering';document.querySelector('#fallback').hidden=false;document.querySelector('#loadStatus').textContent='Restoring 3D…';for(const cue of this.cues.values())cue.hidden=true;this.onInteract({type:'context_lost',control:'renderer'});clearTimeout(this.loadTimer);this.loadTimer=setTimeout(()=>this.fail(new Error('Graphics recovery timed out')),15000);}
 async contextRestored(){this.contextUnavailable=false;this.shaderFailed=false;try{if(!this.model)return;this.resize();await this.prepareFrame();this.ready=true;clearTimeout(this.loadTimer);document.body.dataset.model='ready';document.querySelector('#fallback').hidden=true;document.querySelector('#loadStatus').textContent='';this.last=performance.now();if(!this.animationStarted){this.animationStarted=true;requestAnimationFrame(t=>this.frame(t))}this.onInteract({type:'context_restored',control:'renderer'});}catch(e){this.fail(e)}}
 resize(){if(!this.renderer)return;const w=this.host.clientWidth,h=this.host.clientHeight;if(!w||!h||w===this.renderWidth&&h===this.renderHeight)return;this.renderWidth=w;this.renderHeight=h;this.renderer.setSize(w,h,false);this.camera.aspect=w/h;this.camera.fov=2*Math.atan(Math.tan(Math.PI/12)/Math.min(1,w/h))*180/Math.PI;this.camera.updateProjectionMatrix()}
 go(view,immediate=false){this.introElapsed=null;const v=views[view]||views.front;this.camGoal={p:new T.Vector3(...v.p),t:new T.Vector3(...v.t)};if(immediate||this.reduced){this.camera.position.copy(this.camGoal.p);this.orbit.target.copy(this.camGoal.t);this.camGoal=null;this.orbit.update()}}
 reset(){if(this.audio)this.audio.example=false;this.composer?.cancel();this.audio?.resetExample();this.scenario=null;this.sim.reset();this.audio?.replace(this.sim);this.powerGesture=null;this.bootAt=null;this.powerSpent=false;this.powered=true;this.demo=null;this.held.clear();this.pressTimes.clear();this.gestureHolds=new Set();this.demoFaders=[];this.pointers.clear();this.rockTarget=0;this.simLast=null;this.orbit.enabled=true;this.targets={};this.faderValues=[1,1,1,1];this.lightTargets={};for(const n in this.base){const o=this.nodes[n];o.position.copy(this.base[n].p);o.quaternion.copy(this.base[n].q)}}
 setDemo(step,card){const introHeld=this.demo?.card===card&&card.steps.indexOf(step)===this.scenario?.index+1?(step.hold||[]).filter(key=>this.held.has(key)):[];if(!this.demo)this.composer.cancel();this.cueTrail.clear();this.demo={step,card,start:performance.now(),introHeld};{if(!this.scenario||this.scenario.card!==card)this.scenario=new Scenario(card);this.scenario.begin(card.steps.indexOf(step));this.sim=Object.assign(new Simulator(),structuredClone(this.scenario.sim));}this.held=new Set(introHeld);this.demoCueKinds=new Map(introHeld.map(key=>[key==='UP'||key==='DOWN'?'ROCKER':key,'hold']));this.targets={};this.faderValues=[...this.scenario.from];this.go(this.demoView(step));}
 demoView(step){
  const holds=step.hold||[];
  const controls=[...holds,...this.scenario.events.filter(([,type,,down])=>type==='button'&&down).map(([, ,key])=>key)];
  const sideHeld=controls.includes('FN')||controls.includes('PLAY');
  const faders=Object.keys(step.faders||{}).length;
  if(sideHeld&&step.view==='sideLights')return 'right';
  if(step.view==='comboHold')return 'comboHold';
  if(controls.some(key=>key.startsWith('VOL')))return sideHeld?'comboTop':'top';
  if((sideHeld||faders||holds.some(key=>/^T[1-4]$/.test(key)))&&controls.some(key=>key==='UP'||key==='DOWN'))return 'comboRocker';
  if(sideHeld&&(faders||controls.some(key=>/^T[1-4]$/.test(key))||['faders','tracks','trackLights'].includes(step.view)))return faders?'comboSlide':'comboHold';
  if(faders)return 'faders';
  if(controls.some(key=>/^T[1-4]$/.test(key))&&!step.view)return 'tracks';
  return step.view||'front';
 }
 stopDemo(){this.cueTrail.clear();this.demo=null;this.targets={};this.held.clear();this.lightTargets={}}
 updateDemo(now){if(this.scenario){
 const presentation=this.demo.frozenTime??(now-this.demo.start)/1000,t=presentation-CAMERA_SETTLE_SECONDS;
 if(t<0){this.held=new Set(this.demo.introHeld||[]);this.gestureHolds=new Set(this.held);this.rockTarget=this.held.has('UP')?-1:this.held.has('DOWN')?1:0;this.demoFaders=[];this.demoCueKinds=new Map([...this.held].map(key=>[key==='UP'||key==='DOWN'?'ROCKER':key,'hold']));return;}
 // Finish the camera move before drawing or executing the teaching action.
 if(this.camGoal){this.camera.position.copy(this.camGoal.p);this.orbit.target.copy(this.camGoal.t);this.camGoal=null;this.orbit.update();}
 const index=this.scenario.index,local=this.scenario.advance(Math.min(t,gestureSeconds(this.demo.card,index)));
 this.demoCueKinds=gestureCues(this.demo.card,index,t,this.scenario.events);
 this.demoFaders=t>=.35&&t<1.55?Object.keys(this.demo.step.faders||{}).map(n=>'F'+n):[];
 if(!this.audio.authoritative)this.sim=Object.assign(new Simulator(),structuredClone(local));
 const s=this.sim;this.held=new Set(s.keys.keys());this.gestureHolds=new Set([...s.keys].filter(([k,v])=>s.now-v.at>=180).map(([k])=>k));this.rockTarget=s.has('UP')?-1:s.has('DOWN')?1:0;this.faderValues=[...s.faders];this.lightTargets=s.lights();const replay=confirmationCloseup(this.demo.step,t);if(replay)Object.assign(this.lightTargets,replay);
 }}

 frame(now){requestAnimationFrame(t=>this.frame(t));const dt=Math.min((now-this.last)/1000,.1);this.last=now;if(!this.ready||document.hidden||document.body.hidden||!this.host.getClientRects().length){this.browseVisible=false;return;}this.resize();this.syncBrowsePose();const k=this.reduced?1:1-Math.exp(-dt*(this.liveSmoothingRate??13));if(this.demo)this.updateDemo(now);else this.updatePractice(now);this.audio.sync(now);if(this.introElapsed!=null){this.introElapsed+=dt;const u=Math.min(1,Math.max(0,(this.introElapsed-1)/2.2)),ease=u*u*u*(u*(u*6-15)+10),angle=Math.atan2(64,160)*ease,radius=Math.hypot(64,30,160),elevation=Math.atan2(30,Math.hypot(64,160))*ease;this.camera.position.set(Math.sin(angle)*Math.cos(elevation)*radius,Math.sin(elevation)*radius,Math.cos(angle)*Math.cos(elevation)*radius);this.orbit.target.set(0,0,0);if(u===1){this.camera.position.set(...views.front.p);this.introElapsed=null}}if(this.camGoal){const c=1-Math.exp(-dt*4.8);this.camera.position.lerp(this.camGoal.p,c);this.orbit.target.lerp(this.camGoal.t,c);if(this.camera.position.distanceTo(this.camGoal.p)<.03){this.camera.position.copy(this.camGoal.p);this.orbit.target.copy(this.camGoal.t);this.camGoal=null}}this.orbit.update();
 for(const [key,name] of buttonAliases){const o=this.nodes[name],b=this.base[name];if(!o)continue;const p=this.buttonPosition.copy(b.p),a=this.held.has(key)?1:0;if(name.startsWith('Button.Track'))p.y-=.00035*a;else if(name.startsWith('Button.Volume'))p.z+=.0003*a;else p.x-=.0005*a;o.position.lerp(p,k)}
 for(let n=1;n<=4;n++){const o=this.nodes['Fader'+n];if(o)o.position.z=T.MathUtils.lerp(o.position.z,.005225-this.faderValues[n-1]*.01025,k)}
 const rock=this.nodes.Rocker;if(rock){const q=this.rockRotation.setFromAxisAngle(this.rockAxis,(this.rockTarget||0)*.045).premultiply(this.base.Rocker.q);rock.quaternion.slerp(q,k)}
 for(const [name,l] of Object.entries(this.lights)){const v=this.lightTargets?.[name]||0;l.value=this.physicalLEDs?Math.min(1,v*10):firmwareLEDDisplay(v,!!this.sim.bright,name.startsWith('LED.Side')?'Side':'Track');l.mesh.material.color.copy(l.off).lerp(this.white,ledFaceBrightness(l.value));const bright=this.sim.bright||(this.demo?.step.pattern==='brightness'&&v>.9);l.glow.material.opacity=0;l.glow.scale.setScalar(bright?.0022:.0016);}
 this.saveBrowsePose();this.renderCues();this.updateModelTheme?.();this.renderer.render(this.scene,this.camera);
 }
 syncBrowsePose(){
  if(!this.sharedBrowseHost)return;
  const visible=!document.body.classList.contains('demonstrating')&&(!window.frameElement||window.frameElement.getClientRects().length>0);
  const pose=this.sharedBrowseHost[browsePoseKey];
  if(visible&&!this.browseVisible&&!pose&&!this.reduced){
   // Run the intro once for a fresh workspace, whichever model is visible first.
   this.camGoal=null;this.camera.position.set(0,0,Math.hypot(64,30,160));this.orbit.target.set(0,0,0);this.orbit.update();
   this.introElapsed=0;
  }
  if(visible&&!this.browseVisible&&pose){
   this.introElapsed=null;
   this.camGoal=null;
   const damping=this.orbit.enableDamping;
   this.orbit.enableDamping=false;
   this.orbit.update();
   this.orbit.enableDamping=damping;
   this.camera.position.fromArray(pose.position);
   this.orbit.target.fromArray(pose.target);
   this.orbit.update();
  }
  this.browseVisible=visible;
 }
 saveBrowsePose(){
  if(!this.browseVisible)return;
  const pose=this.sharedBrowseHost[browsePoseKey]??={position:[],target:[]};
  this.camera.position.toArray(pose.position);
  this.orbit.target.toArray(pose.target);
 }
 touchFader(e){
 if(!['touch','pen'].includes(e.pointerType))return null;
 const r=this.renderer.domElement.getBoundingClientRect();let best=null;
 for(let i=1;i<=4;i++){
  const cap=this.nodes['Fader'+i+'.Cap'],node=this.nodes['Fader'+i];if(!cap||!node)continue;
  const point=new T.Box3().setFromObject(cap).getCenter(new T.Vector3()),p=point.clone().project(this.camera);
  if(p.z < -1||p.z>1)continue;
  const distance=Math.hypot(e.clientX-(r.left+(p.x+1)*r.width/2),e.clientY-(r.top+(1-p.y)*r.height/2));
  if(distance>22||(best&&distance>=best.distance))continue;
  const ray=new T.Raycaster();ray.setFromCamera(new T.Vector2(p.x,p.y),this.camera);
  let visible=ray.intersectObject(this.model,true)[0]?.object;while(visible&&visible!==node)visible=visible.parent;
  if(visible===node)best={key:'F'+i,node,point,distance};
 }return best;
 }
 hit(e){const touch=this.touchFader(e);if(touch)return touch;const r=this.renderer.domElement.getBoundingClientRect();this.ndc.set((e.clientX-r.left)/r.width*2-1,-(e.clientY-r.top)/r.height*2+1);this.ray.setFromCamera(this.ndc,this.camera);const hits=this.ray.intersectObject(this.model,true);for(const h of hits){let n=h.object;while(n&&n!==this.model){const name=n.userData.originalName||'';const key=Object.entries(aliases).find(([,v])=>v===name)?.[0];if(key)return {key,node:n,point:h.point};if(/^Fader[1-4]/.test(name))return {key:'F'+name[5],node:this.nodes['Fader'+name[5]],point:h.point};if(name==='Rocker'||name.startsWith('Rocker.')&&name!=='Rocker.Well')return{key:'ROCKER',node:this.nodes.Rocker,point:h.point};n=n.parent} // Stop at a visible housing face; do not click controls through the back.
 if(h.object.userData.originalName?.startsWith('Housing.'))break;
 }return null}
 bind(){const c=this.renderer.domElement;c.addEventListener('pointerdown',e=>{if(!this.ready)return;this.audio.start();this.introElapsed=null;const h=this.hit(e);if(!h)return;if(this.demo)this.onInteract({type:'takeover',control:h.key});if(this.demo)return;e.preventDefault();e.stopImmediatePropagation();c.setPointerCapture(e.pointerId);this.orbit.enabled=false;this.camGoal=null;const state={...h,start:performance.now(),y:e.clientY,x:e.clientX,initial:this.faderValues[Number(h.key[1])-1]};this.pointers.set(e.pointerId,state);if(h.key==='ROCKER'){const pt=this.model.worldToLocal(h.point.clone());this.rockTarget=-pt.z>.0205?-1:1;state.rock=this.rockTarget}else if(!h.key.startsWith('F')||h.key==='FN'){this.held.add(h.key)}state.simKey=h.key==='ROCKER'?(state.rock<0?'UP':'DOWN'):h.key;if(!this.composer.begin(state,e)){this.sim.button(state.simKey,true);this.audio.command('button',state.simKey,true);}this.onInteract({type:'press',control:h.key});},true);
 c.addEventListener('pointermove',e=>{const d=this.pointers.get(e.pointerId);if(d){e.preventDefault();e.stopImmediatePropagation();if(this.composer.move(d,e))return;if(/^F[1-4]$/.test(d.key)){const n=Number(d.key[1]),o=this.nodes['Fader'+n],par=o.parent,r=c.getBoundingClientRect();const top=new T.Vector3(this.base['Fader'+n].p.x,.006,-.005025),bot=new T.Vector3(this.base['Fader'+n].p.x,.006,.005225);par.localToWorld(top);par.localToWorld(bot);top.project(this.camera);bot.project(this.camera);const dx=(top.x-bot.x)*r.width/2,dy=-(top.y-bot.y)*r.height/2;this.faderValues[n-1]=T.MathUtils.clamp(d.initial+((e.clientX-d.x)*dx+(e.clientY-d.y)*dy)/(dx*dx+dy*dy),0,1);this.sim.fader(n-1,this.faderValues[n-1]);this.audio.command('fader',n-1,this.faderValues[n-1]);this.onInteract({type:'fader',control:d.key,value:this.faderValues[n-1]});}return}if(!this.demo)c.style.cursor=this.hit(e)?'pointer':'grab'},true);
 const end=e=>{const d=this.pointers.get(e.pointerId);if(!d)return;e.stopImmediatePropagation();this.pointers.delete(e.pointerId);if(c.hasPointerCapture(e.pointerId))c.releasePointerCapture(e.pointerId);this.held.delete(d.key);this.rockTarget=0;this.orbit.enabled=this.pointers.size===0;if(!this.composer.end(d,e)){this.composer.latched.delete(d.simKey);if(e.type!=='pointercancel'){this.sim.button(d.simKey,false);this.audio.command('button',d.simKey,false);}else {this.sim.cancel();this.audio.command('cancel');}}this.composer.render();this.onInteract({type:'release',control:d.key});};c.addEventListener('pointerup',end,true);c.addEventListener('pointercancel',end,true);window.addEventListener('blur',()=>{this.composer.cancel();this.sim.cancel();this.powerGesture=null;this.pressTimes.clear();this.pointers.clear();this.held.clear();this.rockTarget=0;this.orbit.enabled=true});}
 applyPanelState({held,rocker,lights,physicalLEDs=false}){if(held){this.held.clear();for(const key of held)this.held.add(key);}if(rocker!==undefined)this.rockTarget=rocker;if(lights)this.lightTargets=lights;this.physicalLEDs=physicalLEDs;}
 updatePractice(now){
  const dt=this.simLast==null?0:Math.min(100,Math.max(0,now-this.simLast));this.simLast=now;
  if(!this.composer.drafting&&!this.audio.authoritative)this.sim.tick(this.sim.now+dt);this.applyPanelState({lights:this.sim.lights()});
  if(this.sim.lastMessage!==this.lastSimMessage){this.lastSimMessage=this.sim.lastMessage;if(this.composer.latched.size&&!this.composer.running)this.composer.notice(this.lastSimMessage);this.onInteract({type:'state',message:this.lastSimMessage});}
 }
 enterLiveContext(card,step){this.audio.example=true;const faders=[...this.faderValues],held=[...this.held];this.stopDemo();this.sim.faders=faders;for(const key of held)if(['FN','PLAY','T1','T2','T3','T4'].includes(key))this.sim.keys.set(key,{at:this.sim.now,used:true});this.simLast=null;this.held=new Set(this.sim.keys.keys());this.pressTimes=new Map([...this.held].map(key=>[key,performance.now()-500]));this.faderValues=[...this.sim.faders];this.audio.replace(this.sim,true);}
 manualPress(key,on){if(on)this.audio.start();this.introElapsed=null;if(this.demo)this.onInteract({type:'takeover',control:key});if(this.demo)return;if(on){this.held.add(key);this.pressTimes.set(key,performance.now())}else{this.held.delete(key);this.pressTimes.delete(key)}if(key==='UP'||key==='DOWN')this.rockTarget=on?(key==='UP'?-1:1):0;this.sim.button(key,on);this.audio.command('button',key,on);this.onInteract({type:on?'press':'release',control:key});}
 setFader(n,v){this.introElapsed=null;if(this.demo)this.onInteract({type:'takeover',control:'F'+n});if(this.demo)return;this.faderValues[n-1]=v;this.sim.fader(n-1,v);this.audio.command('fader',n-1,v);}
 renderCues(){
 const now=performance.now(),active=new Set(this.demo?this.demoCueKinds?.keys():this.held);if(this.demo)for(const key of this.demoFaders||[])active.add(key);for(const d of this.pointers.values())active.add(d.key);if(this.rockTarget)active.add('ROCKER');
 const kinds=new Map();
 for(const key of active){if(this.demo){kinds.set(key,this.demoCueKinds?.get(key)||'hold');continue;}const pointer=[...this.pointers.values()].find(p=>p.key===key);const holding=this.demo?((this.demo.step.hold||[]).includes(key)||(key==='ROCKER'&&!!this.demo.step.rock)||this.gestureHolds?.has(key)):pointer?(now-pointer.start)>.25*1000:this.pressTimes.has(key)&&(now-this.pressTimes.get(key)>.25*1000);const kind=/^F[1-4]$/.test(key)?'slide':holding?'hold':'tap';kinds.set(key,kind);if(kind==='tap')this.cueTrail.set(key,now);else this.cueTrail.delete(key);}
 for(const [key,time] of this.demo?[]:this.cueTrail){if(now-time<1000){active.add(key);if(!kinds.has(key))kinds.set(key,'tap')}else this.cueTrail.delete(key);}
 for(const key of active){const name=aliases[key]||(/^F[1-4]$/.test(key)?'Fader'+key[1]+'.Cap':key==='ROCKER'?'Rocker.Paddle':null);const o=this.nodes[name];if(!o)continue;let el=this.cues.get(key);if(!el){el=document.createElement('i');el.className='touch-cue';el.setAttribute('aria-hidden','true');this.host.append(el);this.cues.set(key,el)}
 const point=new T.Box3().setFromObject(o).getCenter(new T.Vector3()),normal=new T.Vector3(key==='FN'||key==='PLAY'?1:key==='ROCKER'?-1:0,key.startsWith('VOL')?1:0,aliases[key]?.startsWith('Button.Track')||/^F[1-4]$/.test(key)?1:0),direction=this.camera.position.clone().sub(point).normalize();
 el.dataset.kind=kinds.get(key)||'tap';
 point.project(this.camera);const visible=normal.dot(direction)>(AUDIO_ENABLED&&this.demo&&this.held.has(key)?-.65:-.28)&&Math.abs(point.x)<1&&Math.abs(point.y)<1;el.hidden=!visible;el.style.left=(point.x*.5+.5)*this.host.clientWidth+'px';el.style.top=(-point.y*.5+.5)*this.host.clientHeight+'px';
 }for(const [key,el] of this.cues)if(!active.has(key))el.hidden=true;this.composer.alignArrow();
 }
 diagnostics(){return {ready:this.ready,controls:Object.values(aliases).every(n=>!!this.nodes[n]),leds:Object.keys(this.lights).length,carrier:this.nodes['Fader1.Carrier']?.material.color.toArray(),aperture:!!this.nodes['Side.UnidentifiedAperture'],flatMark:!!this.nodes['Side.FlatMark']}}
}
