import * as T from 'three';
import {LineSegments2} from 'three/addons/lines/LineSegments2.js';
import {LineSegmentsGeometry} from 'three/addons/lines/LineSegmentsGeometry.js';
import {LineMaterial} from 'three/addons/lines/LineMaterial.js';
function controlOutline(mesh,name){
 const p=mesh.geometry.attributes.position,volume=name.startsWith('Button.Volume');
 let axis=volume?2:1,negative=volume,plane=null;
 if(name==='Rocker.Well'){axis=1;negative=true;plane=-.002101;}
 if(name==='Rocker.Paddle'){axis=0;negative=true;}
 if(name.startsWith('Speaker.Port')){axis=2;negative=true;plane=-.032-mesh.position.z;}
 if(name.startsWith('LED.Side')||name==='Side.ApertureLiner'){axis=0;plane=.023501;}
 if(name==='Left.ApertureLiner'){axis=1;negative=true;plane=-.000451;}
 if(/^Port\..+\.Liner$/.test(name)){axis=1;plane=.032-mesh.position.z;}
 if(name==='USB.Interior'){axis=1;plane=.032-mesh.position.z;}
 const [u,v]=[0,1,2].filter(a=>a!==axis);
 const points=[],seen=new Set();let face=negative?Infinity:-Infinity;
 for(let i=0;i<p.count;i++){
  const xyz=[p.getX(i),p.getY(i),p.getZ(i)];face=negative?Math.min(face,xyz[axis]):Math.max(face,xyz[axis]);
  const key=xyz[u].toFixed(8)+','+xyz[v].toFixed(8);
  if(!seen.has(key)){seen.add(key);points.push([xyz[u],xyz[v]]);}
 }
 points.sort((a,b)=>a[0]-b[0]||a[1]-b[1]);
 const cross=(a,b,c)=>(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
 const half=list=>{const h=[];for(const q of list){while(h.length>1&&cross(h[h.length-2],h[h.length-1],q)<=1e-14)h.pop();h.push(q);}return h;};
 const a=half(points),b=half([...points].reverse());a.pop();b.pop();const hull=a.concat(b),positions=[];
 for(let i=0;i<hull.length;i++)for(const q of [hull[i],hull[(i+1)%hull.length]]){const xyz=[0,0,0];xyz[axis]=plane??face;xyz[u]=q[0];xyz[v]=q[1];positions.push(...xyz);}
 return positions;
}
function housingContours(array,mesh,controls){
 const inverse=mesh.matrixWorld.clone().invert();
 const boxes=controls.map(control=>{control.geometry.computeBoundingBox();return control.geometry.boundingBox.clone().applyMatrix4(control.matrixWorld).applyMatrix4(inverse).expandByScalar(.0007);});
 const out=[],start=new T.Vector3(),end=new T.Vector3();
 for(let i=0;i<array.length;i+=6){start.fromArray(array,i);end.fromArray(array,i+3);
  // The USB opening has a bevel built into the housing itself. Remove that
  // complete rim; the dedicated flat port supplies its single outer contour.
  const usbRim=p=>p.z>.029&&p.x>-.019&&p.x<-.009&&Math.abs(p.y)<.002;
  if(usbRim(start)&&usbRim(end))continue;
  const rockerRim=p=>p.x<-.020&&p.z>-.027&&p.z<-.014&&Math.abs(p.y)<.0035;
  if(rockerRim(start)&&rockerRim(end))continue;
  if(start.y<0&&end.y<0)continue;if(!boxes.some(box=>box.containsPoint(start)&&box.containsPoint(end)))out.push(...array.slice(i,i+6));}
 return out;
}

// The outer housing is convex. Project its rim to find the visible joins
// between the front and rear contours under the actual perspective camera.
function hull(points){
 const sorted=[...points].sort((a,b)=>a.u-b.u||a.v-b.v);
 const turn=(a,b,c)=>(b.u-a.u)*(c.v-a.v)-(b.v-a.v)*(c.u-a.u);
 const half=ordered=>{
  const edge=[];
  for(const point of ordered){
   while(edge.length>1&&turn(edge.at(-2),edge.at(-1),point)<=0)edge.pop();
   edge.push(point);
  }
  return edge.slice(0,-1);
 };
 return half(sorted).concat(half(sorted.reverse()));
}
function housingRim(meshes){
 const layers=new Map();
 for(const mesh of meshes){
  const positions=mesh.geometry.attributes.position;
  for(let i=0;i<positions.count;i++){
   const x=positions.getX(i),y=positions.getY(i),z=positions.getZ(i);
   const layer=Math.round(y*1e7);
   if(!layers.has(layer))layers.set(layer,new Map());
   layers.get(layer).set(`${x}/${z}`,{u:x,v:z,point:new T.Vector3(x,y,z)});
  }
 }
 return [...layers.values()].flatMap(layer=>{
  const points=[...layer.values()];
  return (points.length>2?hull(points):points).map(({point})=>({point,u:0,v:0}));
 });
}

export function applyHairline(device){
 const model=device.model,nodes=device.nodes,lines=[],meshes=[];
 model.traverse(o=>{if(o.isMesh)meshes.push({o,name:o.userData.originalName||o.name});});
 model.updateMatrixWorld(true);
 const controls=meshes.filter(({name})=>/^(Button\.|Speaker\.(Port|Liner)|LED\.|Port\..+\.Liner|USB\.Interior|Side\.ApertureLiner|Left\.ApertureLiner)/.test(name||'')).map(({o})=>o);
 for(const {o,name} of meshes){
  if(/^(Housing\.Backplate|Screw[12](\.|$)|Print\.Rear)/.test(name||''))o.visible=false;
  o.castShadow=false;o.receiveShadow=false;o.renderOrder=0;
  o.material=new T.MeshBasicMaterial({color:0xf6f5f1,polygonOffset:true,polygonOffsetFactor:4,polygonOffsetUnits:8});
  // Draw only the recognizable exterior features. Keep all original surfaces
  // for correct occlusion, but omit rails, wells, screws, print and port internals.
  const exterior=/^(Housing\.(Aluminium|Polymer)|Button\..+|Fader[1-4]\.Cap|LED\.(Track|Side)[1-4]|Rocker\.(Paddle|Well)|Speaker\.Port\d+|Port\.(Headphone|Sync)\.Liner|USB\.Interior|Print\.(PlayTriangle|FunctionDot(?:\.001)?))$/;
  if(!exterior.test(name||''))continue;
  const simpleControl=/^(Button\.|Fader[1-4]\.Cap$|Rocker\.(Paddle|Well)$|LED\.|Speaker\.Port|Port\.|USB\.Interior$|(Side|Left)\.ApertureLiner$)/.test(name||'');
  const edges=simpleControl?null:new T.EdgesGeometry(o.geometry,40);
  const geometry=new LineSegmentsGeometry().setPositions(simpleControl?controlOutline(o,name):name.startsWith('Housing.')?housingContours(edges.attributes.position.array,o,controls):edges.attributes.position.array);
  // A few depth edges anchor the side buttons to the housing without
  // bringing back the doubled bevel outlines around their faces.
  if(name==='Button.Play'||name==='Button.Function'){
   o.geometry.computeBoundingBox();const b=o.geometry.boundingBox;
   const x0=b.min.x,x1=b.max.x,z0=b.min.z,z1=b.max.z,face=b.max.y,seat=b.min.y;
   const bridges=[];
   for(const x of [x0,x1])for(const z of [z0,z1])bridges.push(x,face,z,x,seat,z);
   const perimeter=controlOutline(o,name);
   geometry.setPositions([...perimeter,...bridges]);
  }
  if(name==='Rocker.Paddle'){
   o.geometry.computeBoundingBox();const b=o.geometry.boundingBox,bridges=[];
   for(const y of [b.min.y,b.max.y])for(const z of [b.min.z,b.max.z])bridges.push(b.min.x,y,z,b.max.x,y,z);
   geometry.setPositions([...controlOutline(o,name),...bridges]);
  }
  edges?.dispose();
  const line=new LineSegments2(geometry,new LineMaterial({color:0x171819,linewidth:1.15,worldUnits:false,alphaToCoverage:false}));
  line.material.depthWrite=false;
  // Bias strokes toward the camera without disabling hidden-surface testing.
  line.material.onBeforeCompile=shader=>{shader.vertexShader=shader.vertexShader.replace("gl_Position = clip;", `gl_Position = clip; gl_Position.z -= ${simpleControl ? 0.001 : 0.0005} * gl_Position.w;`);};
  o.add(line);lines.push({line,name:name||''});
 }
 const rim=housingRim([nodes['Housing.Aluminium'],nodes['Housing.Polymer']]);
 const corners=new LineSegments2(new LineSegmentsGeometry().setPositions(new Float32Array(rim.length*6)),new LineMaterial({color:0x171819,linewidth:1.15,worldUnits:false,alphaToCoverage:false}));
 corners.geometry.instanceCount=0;
 corners.frustumCulled=false;
 corners.material.depthWrite=false;
 corners.material.onBeforeCompile=shader=>{shader.vertexShader=shader.vertexShader.replace('gl_Position = clip;','gl_Position = clip; gl_Position.z -= 0.0005 * gl_Position.w;');};
 model.add(corners);lines.push({line:corners,name:'CornerContours'});
 // A plain rear face replaces the inset backplate and its screw recesses.
 {
  const shape=new T.Shape(),w=.047,h=.064,r=.0018,x=w/2,y=h/2;
  shape.moveTo(-x+r,-y);shape.lineTo(x-r,-y);shape.quadraticCurveTo(x,-y,x,-y+r);shape.lineTo(x,y-r);shape.quadraticCurveTo(x,y,x-r,y);shape.lineTo(-x+r,y);shape.quadraticCurveTo(-x,y,-x,y-r);shape.lineTo(-x,-y+r);shape.quadraticCurveTo(-x,-y,-x+r,-y);shape.closePath();
  const back=new T.Mesh(new T.ShapeGeometry(shape,32),new T.MeshBasicMaterial({color:0xf6f5f1,side:T.DoubleSide}));
  back.renderOrder=-1;back.rotation.x=Math.PI/2;back.position.y=-.0045;model.add(back);
  const points=shape.getPoints(32),positions=[];
  for(let i=0;i<points.length-1;i++)positions.push(points[i].x,points[i].y,0,points[i+1].x,points[i+1].y,0);
  const outline=new LineSegments2(new LineSegmentsGeometry().setPositions(positions),new LineMaterial({color:0x171819,linewidth:1.15,worldUnits:false,alphaToCoverage:false}));
  outline.position.z=.00001;outline.material.depthWrite=false;outline.material.onBeforeCompile=shader=>{shader.vertexShader=shader.vertexShader.replace('gl_Position = clip;','gl_Position = clip; gl_Position.z -= 0.0005 * gl_Position.w;');};back.add(outline);lines.push({line:outline,name:'RearOutline'});
 }
 // Seal the USB-C illustration at its outer opening so contacts and the
 // inner tongue cannot interrupt the outline when viewed from below.
 {
  const port=nodes['USB.Interior'],positions=controlOutline(port,'USB.Interior');
  const shape=new T.Shape();
  for(let i=0;i<positions.length;i+=6){const x=positions[i],z=positions[i+2];if(i===0)shape.moveTo(x,z);else shape.lineTo(x,z);}
  shape.closePath();
  const fill=new T.Mesh(new T.ShapeGeometry(shape),new T.MeshBasicMaterial({color:0xf6f5f1,side:T.DoubleSide}));
  fill.renderOrder=-1;fill.rotation.x=Math.PI/2;fill.position.y=.032-port.position.z;port.add(fill);
 }
 // Flat slot floors hide recessed construction edges without covering the rim
 // or the moving cap. Derived from each original slot bed's dimensions.
 for(let n=1;n<=4;n++){
  const bed=nodes['Fader'+n+'.SlotBed'];bed.geometry.computeBoundingBox();
  const box=bed.geometry.boundingBox,w=box.max.x-box.min.x,h=box.max.z-box.min.z,r=w/2,y=h/2-r;
  const shape=new T.Shape();shape.moveTo(-r,-y);shape.lineTo(-r,y);shape.absarc(0,y,r,Math.PI,0,true);shape.lineTo(r,-y);shape.absarc(0,-y,r,0,-Math.PI,true);shape.closePath();
  const floor=new T.Mesh(new T.ShapeGeometry(shape,32),new T.MeshBasicMaterial({color:0xf6f5f1,side:T.DoubleSide}));
  floor.renderOrder=-1;floor.rotation.x=-Math.PI/2;floor.position.set(bed.position.x,.00448,bed.position.z);model.add(floor);
 }

 model.traverse(o=>{if(o.isLineSegments2){o.raycast=()=>{};o.renderOrder=10;o.material.toneMapped=false;}if(o.isMesh)o.material.toneMapped=false;});
 device.renderer.shadowMap.enabled=false;
 for(const light of Object.values(device.lights)){light.off.set(0xf6f5f1);light.glow.visible=false;}
 const indicators=lines.filter(({name})=>device.lights[name]).map(({line,name})=>({
  light:device.lights[name],stroke:line.material,last:-1
 }));
 const ink=new T.Color(0x171819),orange=new T.Color(0xed5420);
 const projected=new T.Vector3(),projection=new T.Matrix4(),previous=new T.Matrix4(),paper=new T.Color(0xf6f5f1),lit=new T.Color(0xc84920);let width=0,height=0;
 return ()=>{
  if(width!==device.renderWidth||height!==device.renderHeight){width=device.renderWidth;height=device.renderHeight;for(const {line} of lines)line.material.resolution.set(width||1,height||1);}
  // LED state remains visible as an orange fill, while unlit LEDs stay outlined.
  for(const indicator of indicators){
   const {light,stroke}=indicator;
   const intensity=Math.min(1,Math.sqrt(light.value)*1.45);
   light.mesh.material.color.copy(paper).lerp(lit,intensity);
   if(intensity===indicator.last)continue;
   indicator.last=intensity;
   stroke.color.copy(ink).lerp(orange,intensity);
   stroke.linewidth=1.15+intensity*1.35;
  }
  device.camera.updateMatrixWorld();
  projection.multiplyMatrices(device.camera.projectionMatrix,device.camera.matrixWorldInverse).multiply(model.matrixWorld);
  if(projection.equals(previous))return;
  previous.copy(projection);
  for(const vertex of rim){
   projected.copy(vertex.point).applyMatrix4(projection);
   vertex.u=projected.x;vertex.v=projected.y;
  }
  const outline=hull(rim),geometry=corners.geometry;
  const starts=geometry.attributes.instanceStart,ends=geometry.attributes.instanceEnd;
  let count=0;
  for(let i=0;i<outline.length;i++){
   const a=outline[i].point,b=outline[(i+1)%outline.length].point;
   // The existing rim strokes cover edges within either face.
   if(Math.abs(a.y-b.y)<1e-7)continue;
   starts.setXYZ(count,a.x,a.y,a.z);
   ends.setXYZ(count,b.x,b.y,b.z);
   count++;
  }
  geometry.instanceCount=count;
  starts.data.needsUpdate=true;
 };
}


export function createHairline(device){
 const meshState=mesh=>({
  material:mesh.material,visible:mesh.visible,castShadow:mesh.castShadow,
  receiveShadow:mesh.receiveShadow,renderOrder:mesh.renderOrder
 });
 const originals=new Set();
 const surfaces=[];
 device.model.traverse(node=>{
  originals.add(node);
  if(node.isMesh)surfaces.push({node,standard:meshState(node)});
 });
 const shadows=device.renderer.shadowMap.enabled;
 const lights=Object.values(device.lights).map(light=>({
  light,off:light.off.clone(),glow:light.glow.visible
 }));
 const update=applyHairline(device);
 for(const surface of surfaces)surface.hairline=meshState(surface.node);
 const additions=[];
 device.model.traverse(node=>{
  if(!originals.has(node)&&originals.has(node.parent))additions.push({node,parent:node.parent});
 });
 return {
  update,
  setEnabled(enabled){
   for(const surface of surfaces)Object.assign(surface.node,enabled?surface.hairline:surface.standard);
   for(const {node,parent} of additions){
    if(enabled)parent.add(node);
    else node.removeFromParent();
   }
   device.renderer.shadowMap.enabled=!enabled&&shadows;
   for(const {light,off,glow} of lights){
    light.off.set(enabled?0xf6f5f1:off);
    light.glow.visible=!enabled&&glow;
   }
  }
 };
}
