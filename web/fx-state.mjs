// Shared display/control metadata. Never changes device state or predicts DSP output.
export const FX=[
 {name:'Filter',tap:'bypass + reset damping',secondary:'Filter damping'},
 {name:'Chorus / Flanger',key:'chorusType',types:['Chorus','Flanger'],tap:'type',secondary:'Span',period:5},
 {name:'Distortion',key:'distortionType',types:['Soft','Hard','Fold','Off'],tap:'type'},
 {name:'Gate',key:'gatePattern',types:['Gallop','Halves','Slow gear'],tap:'pattern',period:0},
 {name:'Crush / Lo-fi / Radio',key:'crushType',types:['Crush','Lo-fi','Radio'],tap:'type'},
 {name:'Ring mod',tap:'off'},
 {name:'Auto-wah',tap:'off + reset damping',secondary:'Wah damping'},
 {name:'Echo',tap:'division',secondary:'Echo feedback',period:1},
 {name:'Phaser',tap:'division',secondary:'Phaser feedback',period:2},
 {name:'Sweep',tap:'division',period:3},
 {name:'Tremolo',tap:'division',secondary:'Tremolo shape',shape:'Stereo spread',period:4},
 {name:'Reverb / Freeze',key:'reverbType',types:['Reverb','Freeze'],tap:'type',secondary:'Reverb damping'},
 {name:'Tape drive',tap:'off'},
 {name:'Tone / Compression',key:'toneMode',types:['Tone','Compression'],tap:'Tone / Compression'},
 {name:'Noise',key:'noiseType',types:['Cassette','Vinyl','Pink','Minidisc'],tap:'source'},
 {name:'Wobble',key:'wobbleType',types:['Wow + flutter','Wow','Flutter'],tap:'type',period:6}
];
export const modernFX=d=>!!d&&d.version>=3;
export function effectInfo(id,d){
 const f=FX[id],modern=modernFX(d);let type=f.key&&d?.[f.key]!=null?f.types?.[d[f.key]]:null,name=f.name,tap=f.tap;
 if(id===13){if(d&&d.toneMode==null){name='Tone';tap='flat + compression off';}else name=type||f.name;}
 if(id===3&&!modern)type=d?.gatePattern===2?'Off':f.types[d?.gatePattern];
 if([1,4,11,14,15].includes(id)&&!modern)type=null;
 if(id===1){name=type||f.name;if(d&&!modern){name='Chorus';tap='dry';}}
 if(id===4){name=type||f.name;if(d&&!modern){name='Bitcrush';tap='off';}}
 if(id===3&&type)name=type==='Slow gear'?'Slow gear':'Gate · '+type;
 if(id===11){name=type||f.name;if(d&&!modern){name='Reverb';tap='dry';}}
 if(id===14&&type)name=type+' noise';if(id===15&&type)name=type;
 if(d&&!modern&&[14,15].includes(id))tap='off';
 return {...f,name,type,tap,secondary:id===13&&d&&d.toneMode==null?'Compression':id===1?((type||'Chorus')+' span'):f.secondary};
}
export function effectSettings(id,d,physicalLane){
 const f=effectInfo(id,d),rows=[],pct=x=>(x/255*100).toFixed(1)+'%';
 if(id===13&&d.toneMode!=null)rows.push(['Compression',pct(d.secondary[13])]);
 if(f.secondary)rows.push([f.secondary,pct(d.secondary[id])]);if(f.shape)rows.push([f.shape,pct(d.shape[id])]);
 if(f.type)rows.push([id===14?'Source':'Type',id===11&&d.reverbType===1?(d.reverbFrozen?'Freeze · holding':'Freeze · empty'):f.type]);
 if(id===7)rows.push(['Division',['1/16','Dotted 1/16','1/8','Off'][d.echoDivision]??'Unknown']);
 if(id>=8&&id<=10)rows.push(['Division',['Beat','Half','Quarter','Off'][d.lfoDivision[id-8]]??'Unknown']);
 if(f.period!==undefined)rows.push(['Tapped period',d.tapFrames[f.period]?`${(d.tapFrames[f.period]/48).toFixed(1)} ms`:'Grid']);
 if(d.secondaryLane===physicalLane+1){const label=d.shapeHeld?f.shape:f.secondary;if(label)rows.push(['Editing',label]);}
 return rows;
}
export function effectChanged(id,a,b){
 const f=FX[id];return !!(f.key&&a[f.key]!==b[f.key])||(id===11&&a.reverbFrozen!==b.reverbFrozen)||(id>=8&&id<=10&&a.lfoDivision?.[id-8]!==b.lfoDivision?.[id-8])||(f.period!==undefined&&a.tapFrames?.[f.period]!==b.tapFrames?.[f.period]);
}
export const FX_CYCLES={'1:2':2,'1:3':3,'1:4':3,'2:1':3,'2:4':3,'3:1':3,'3:2':3,'3:3':3,'3:4':2,'4:2':2,'4:3':4,'4:4':3};
export const FX_DEFAULT_CYCLES=Object.fromEntries(Object.keys(FX_CYCLES).map(k=>[k,k==='3:3'?2:0]));
