// Two bars at 120 BPM: four-on-the-floor kick, snare on beats two and four.
// Synthesized locally; no external samples.
export function makeDrums(rate=48000){const length=rate*4,stems=Array.from({length:4},()=>new Float32Array(length));let seed=7741;const noise=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/2147483648-1;};
 const hit=(lane,beat,duration,voice,gain=1)=>{const start=Math.round(beat*.5*rate);let phase=0,low=0;for(let i=0;i<duration*rate;i++){const t=i/rate;let x=0,n=noise();if(voice==='kick'){phase+=2*Math.PI*(47+112*Math.exp(-t*42))/rate;x=Math.sin(phase)*Math.exp(-t*8)+n*Math.exp(-t*180)*.16;}if(voice==='snare'){low+=.12*(n-low);x=(n-low)*Math.exp(-t*21)*.55+Math.sin(2*Math.PI*184*t)*Math.exp(-t*30)*.35;}if(voice==='hat'){low+=.3*(n-low);x=(n-low)*Math.exp(-t*(duration>.15?19:65))*.24;}if(voice==='rim')x=(Math.sin(t*2*Math.PI*823)+Math.sin(t*2*Math.PI*1317))*.15*Math.exp(-t*80);stems[lane][(start+i)%length]+=x*gain*Math.min(1,t*rate/8);}};
 for(let b=0;b<8;b++)hit(0,b,.45,'kick',.8);
 for(const b of [1,3,5,7])hit(1,b,.27,'snare',.72);
 return stems;
}
