// Filenames carry destinations, never permission to replace an existing take.
function parseName(name){
 const m=/^sp1?[_ -]?song(\d+)[_ -]?track(\d+)(?:[_ -]take(\d+))?(?: \(\d+\))?\.[^.]+$/i.exec(name);
 if(!m)return null;
 const song=Number(m[1]),track=Number(m[2]),take=m[3]===undefined?null:Number(m[3]);
 if(!Number.isInteger(song)||song<1||song>16||!Number.isInteger(track)||track<1||track>4)
  throw Error(name+': use Song 1–16 and Track 1–4');
 if(take!==null&&(!Number.isSafeInteger(take)||take<1))throw Error(name+': invalid take number');
 return {song,track,take};
}
export function legacyDestination(name){const n=parseName(name);return n?{song:n.song,track:n.track}:null;}
export function exportTakeName(path,take){
 if(path.length!==5||path[0]!=='SONGS'||!/^SONG(0[1-9]|1[0-6])$/.test(path[1])||!/^TRACK[1-4]$/.test(path[2])||path[3]!=='TAKES'||!Number.isSafeInteger(take)||take<1)throw Error('Invalid take export destination');
 return `sp1_song${Number(path[1].slice(4))}_track${Number(path[2].slice(5))}_take${take}.wav`;
}
export function routeFiles(files,fallback,{replace=false}={}){
 const routes=files.map(file=>{const named=replace?null:parseName(file.name),to=named||fallback;
  if(!to)throw Error('Choose a destination for '+file.name);
  return {file,song:to.song,track:to.track,named:!!named,takeNumber:named?.take??null};});
 // Numbered exports return in take order even if Finder supplies lexical order
 // (take1, take10, take2). Mixed/unnamed batches keep the user's selection order.
 if(routes.every(r=>r.named)&&routes.some(r=>r.takeNumber!==null))routes.sort((a,b)=>a.song-b.song||a.track-b.track||(a.takeNumber??Infinity)-(b.takeNumber??Infinity));
 return routes;
}
