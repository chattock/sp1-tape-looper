// Display-only session state. Never restores permission, device handles or verification.
export const SESSION_KEY='sp1-install-reminder-v1';
export function reminder(raw){try{const x=JSON.parse(raw);if(x?.schema!==1||!/^([0-9a-f]{16})$/.test(x.identity))return null;return {schema:1,identity:x.identity,at:String(x.at??'').slice(0,40),phase:String(x.phase??'').slice(0,80),interrupted:x.busy===true}}catch{return null}}
export class ProgressMeter {
 reset(){this.phase=null;this.start=0;this.last=0;this.value=null}
 constructor(){this.reset()}
 update(p,now){const phase=p.phase??'backup',done=p.done??p.bytes??0,total=p.total;
 if(!Number.isFinite(done)||done<0)throw Error('Invalid progress');
 if(phase!==this.phase||done<this.last){this.phase=phase;this.start=now}
 const elapsed=Math.max(0,(now-this.start)/1000),rate=elapsed>=1?done/elapsed:null;
 this.last=done;this.value={phase,done,total:Number.isFinite(total)&&total>0?total:null,elapsed,rate,remaining:rate>0&&total>=done&&elapsed>=5?(total-done)/rate:null};return this.value;
 }
}
export function coverageText(c){return [
 'Original controller: unavailable — no verified factory backup or restore path.',
 c.music?'Music/settings: files verified for this device during this session.':'Music/settings: no completed backup verified in this session.',
 c.recovery?'Interrupted-install recovery: original plan saved and read back. Keep its journal.':'Interrupted-install recovery: no newly prepared plan verified in this session.',
 'Radio: checked separately on the Bluetooth setup page; no radio backup is inferred here.',
 'These are separate backups, not a complete factory-restoration package.'
 ].join('\n')}
