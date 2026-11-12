// Export the reviewed guide data; pass a web root to use an isolated deployment.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(process.argv[2]||path.join(here,'../../web'));
const {cards,categories}=await import(pathToFileURL(path.join(root,'field-guide/content.js')));
const {rockerModes}=await import(pathToFileURL(path.join(root,'field-guide/rocker-guide.js')));
const {hiddenIds}=await import(pathToFileURL(path.join(root,'field-guide/guide-layout.js')));
const data={revision:process.argv[3]||new Date().toISOString().slice(0,10),firmware:'4.0',categories,cards,rockerModes,hiddenIds:[...hiddenIds]};
fs.writeFileSync(path.join(here,'content.json'),JSON.stringify(data,null,2)+'\n');
console.log(`Exported ${cards.length} lessons, ${rockerModes.length} rocker modes; revision ${data.revision}.`);
