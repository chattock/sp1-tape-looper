// Navigation only. These predicates never grant write/recovery authority.
export function filesystemReady({libraryVerified,basicRuntimeVerified,layout,libraryMode}){return !!(libraryVerified&&!basicRuntimeVerified&&(layout==='native'||libraryMode==='paged'));}
export function runtimeRoute({filesystem,radioChecked,radioNeedsReview,pairingConfirmed,audioConfirmed,finalCheckRequested}){
 if(!filesystem)return 'filesystem';
 if(finalCheckRequested&&radioChecked&&pairingConfirmed&&audioConfirmed)return 'complete';
 if(radioChecked)return 'complete';
 return radioNeedsReview?'radio-review':'bluetooth';
}
export function testAdmission({filesystem,verifiedRuntime}){return !!filesystem&&!!verifiedRuntime;}
