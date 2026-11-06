// New clean installs need no previous-image journal. Their durable authority is
// on the controller; target/identity are reconstructed and verified there.
// Keep the complete existing journal policy for legacy pending transactions.
export function radioTransaction607(legacy,{exportOriginal=false}={}){
 let selected=null,transaction=null;const diagnostics={};
 async function policy({clean}){if(selected!==null&&selected!==clean)throw Error('Bluetooth policy changed during setup');selected=clean;if(!clean||exportOriginal)transaction=await legacy()}
 async function save(name,data){
  if(transaction)return transaction.save(name,data);
  // Bounded nonsecret summaries are optional; never retain snapshots/key logs.
  if(!['compatibility.json','summary.json','failure-summary.json'].includes(name))return;
  const text=JSON.stringify(data);if(text.length<=32768)diagnostics[name]=data;
 }
 return {policy,save,prepareRecovery:async(...args)=>{if(selected===false)return transaction.prepareRecovery?.(...args);if(selected!==true)throw Error('Bluetooth write policy was not established')},complete:async()=>{if(selected===false)await transaction.complete?.()},get clean(){return selected===true},get dir(){return transaction?.dir},get name(){return transaction?.name||'controller-forward-recovery'},diagnostics};
}
