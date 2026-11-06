import {panelState} from './panel-state.mjs?v=484';
import {Device} from './field-guide/device.js?v=610';
// Keep the guide's exact renderer/model/materials; suppress the simulated controls.
export class ConnectedDevice extends Device {
 bind(){this.liveSmoothingRate=48;this.renderer.domElement.setAttribute('aria-label','SP-1 connected model. Drag to rotate; scroll or pinch to zoom.');}
 setSnapshot(snapshot){this.observed=snapshot;this.applyPanelState(panelState(snapshot?.details?.panel));this.host.classList.toggle('unlinked',!snapshot);if(snapshot)for(let i=0;i<4;i++)if(snapshot.faderValid&(1<<i))this.faderValues[i]=Math.min(1,snapshot.faders[i]/3700);}
 updatePractice(){
  // Snapshot targets update on receipt, not on every animation frame.
  this.lightTargets??={}; // LED phase is not transmitted.
 }
 renderCues(){} // No teaching overlays or per-held-button layout work in the live viewer.
 manualPress(){} setFader(){}
}
