import fs from 'node:fs/promises';
import {PresentationFile,FileBlob} from 'file:///C:/Users/ximam/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';
const root='C:/Users/ximam/Documents/Universidad/EMBER-Hackathon';
const p=await PresentationFile.importPptx(await FileBlob.load(root+'/output/ember-update/EMBER_Overnight_Update.pptx'));
for(let i=0;i<p.slides.items.length;i++){
 const image=await p.export({slide:p.slides.items[i],format:'png',scale:2});
 await fs.writeFile(`${root}/tmp/ember-deck/pdf-slide-${i+1}.png`,new Uint8Array(await image.arrayBuffer()));
}
console.log(`Rendered ${p.slides.items.length} slides at double resolution.`);
