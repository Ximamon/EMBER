import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
const require=createRequire('C:/Users/ximam/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');
const sharp=require('sharp');
import { Presentation, PresentationFile, FileBlob } from 'file:///C:/Users/ximam/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';
import { finalizePresentation } from 'file:///C:/Users/ximam/.codex/plugins/cache/openai-primary-runtime/presentations/26.909.12148/skills/presentations/container_tools/artifact_tool_utils.mjs';
const root = 'C:/Users/ximam/Documents/Universidad/EMBER-Hackathon';
const tmp = path.join(root,'tmp/ember-deck');
const out = path.join(root,'output/ember-update');
const skill = 'C:/Users/ximam/.codex/plugins/cache/openai-primary-runtime/presentations/26.909.12148/skills/presentations';
await fs.mkdir(out,{recursive:true});
const titles=['Overnight progress','Real terrain and wind','GPU execution profiling','Fire model development','Next steps'];
const times=['0:00–0:20','0:20–1:25','1:25–2:30','2:30–3:25','3:25–4:00'];
const scripts=[
`Good morning. Here is a quick update on our overnight progress with EMBER. We worked on three areas: adding real terrain and wind data, improving GPU execution efficiency, and developing the fire spread model. I’ll briefly explain where each area stands.`,
`My contribution was improving the real data inputs for our Collserola case. We now include elevation and a wind snapshot from ERA5 reanalysis, alongside the existing fuel map.

The elevation data is aligned with the simulation grid, so differences in height can influence the slope between neighbouring cells. The wind input gives us a direction and speed for a selected date and hour, instead of relying only on manually chosen values.

For now, that wind is constant across the area and throughout the run. This workflow runs on the scalar CPU version; it is not yet integrated with the GPU version.

The main advance is richer, reproducible environmental inputs. These inputs make our test cases more realistic, but they do not yet make the simulation a calibrated wildfire forecast.`,
`Julián continued working on the efficiency of the GPU simulation. These screenshots show execution profiles in NVIDIA Nsight Systems for four MPI processes, labelled zero to three.

Each row shows a sequence of scenarios assigned to one process. The repeated blocks let us inspect how the work is organised, including scenario execution and CUDA synchronisation calls.

The value of these profiles is that they make the execution pattern visible. They help guide optimisation and show where we should measure overhead more carefully.

We do not have a matched before and after benchmark in these screenshots, so I am not claiming a specific speedup. The next measurement should compare the same workload and configuration, checking both execution time and correctness. That will let us quantify the benefit of the changes.`,
`Jesús focused on the fire model. He investigated the two approaches described in issue number four and created two branches from dev: rothermel-fire-model and normalized-fuel-model.

The Rothermel model is already implemented in the first branch. However, it still needs to be compiled and tested, so we should describe it as implementation progress, not as a validated result.

The second branch provides a separate place to explore the normalised fuel approach. We are not claiming that this alternative has already been implemented.

This gives us a clear starting point for the next stage: check that the Rothermel implementation builds, test its behaviour, and then assess how it can fit with the rest of EMBER.`,
`Our proposed next steps are to compile and test the Rothermel model, measure the GPU changes with comparable benchmarks, and move towards integrating the environmental inputs with the other developments.

The three workstreams are progressing, but they are not yet one fully integrated, validated system.

That is our overnight update: richer real data inputs, continued GPU optimisation, and an implemented Rothermel model awaiting validation. Thank you.`
];
const sources=[
'Source: team progress reported by the presenter on 29 September 2026.',
'Sources: docs/real-environment.md and docs/real-terrain.md in the EMBER repository; presenter’s contribution report. Elevation: Copernicus DEM GLO-30. Weather: ERA5 reanalysis delivered through Open-Meteo. No claim of GPU parity or physical calibration.',
'Sources: four original WhatsApp screenshots supplied by the presenter, 29 September 2026, 08:24:07, 08:24:22, 08:24:37 and 08:25:05. Cropped to the scenario and CUDA API timeline rows. Screenshots show ranks 0–3; they are not a before/after benchmark.',
'Source: Jesús’s direct report supplied by the presenter: investigated both approaches in issue #4; created rothermel-fire-model and normalized-fuel-model from dev; implemented Rothermel in the first branch, pending compilation and testing.',
'Proposed next steps based on the reported status; not claims of completed work.'
];
const p=Presentation.create({slideSize:{width:1280,height:720}});
const c={bg:'#12171D',fg:'#F5F2ED',muted:'#B5BDC6',orange:'#FF9B55'};
function txt(s,text,x,y,w,h,size=30,color=c.fg,bold=false){
 const t=s.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 t.text=text;t.text.style={typeface:'Arial',fontSize:size,color,bold,autoFit:'none'};return t;
}
function base(i){const s=p.slides.add();s.background.fill=c.bg;
 txt(s,titles[i],64,49,1148,75,48,c.fg,true);
 txt(s,`EMBER  /  ${i+1}`,64,664,500,26,18,c.muted);
 s.speakerNotes.textFrame.setText(`${times[i]}\n\n${scripts[i]}\n\n${sources[i]}`);return s;}
let s=base(0);
txt(s,'EMBER',64,196,1130,117,100,c.orange,true);
txt(s,'Real inputs. GPU execution. Fire modelling.',68,348,1120,70,39);
txt(s,'Team progress update  ·  29 September 2026',68,462,1100,42,25,c.muted);
s=base(1);
txt(s,'Elevation',64,181,470,55,36,c.orange,true);
txt(s,'Real heights aligned with\nthe Collserola simulation grid',64,249,535,102,30);
txt(s,'Wind',690,181,490,55,36,c.orange,true);
txt(s,'ERA5 reanalysis snapshot\nfor a selected date and hour',690,249,520,102,30);
txt(s,'Richer inputs for reproducible test cases',64,433,1145,65,38,c.fg,true);
txt(s,'Current route: scalar CPU • Uniform, constant wind per run',64,533,1140,44,25,c.muted);
txt(s,'The simulation is not yet a calibrated wildfire forecast',64,591,1140,40,24,c.orange);
s=base(2);
txt(s,'Julián  /  Profiling scenario execution across four MPI processes',64,130,1145,42,26,c.muted);
const stamps=['08.24.07','08.24.22','08.24.37','08.25.05'];
for(let i=0;i<4;i++){
 const y=201+i*99;
 txt(s,`MPI ${i}`,64,y+18,123,44,27,c.orange,true);
 const blob=await fs.readFile(`C:/Users/ximam/Downloads/WhatsApp Image 2026-09-29 at ${stamps[i]}.jpeg`);
 const cropped=await sharp(blob).extract({left:395,top:314,width:1175,height:91}).png().toBuffer();
 s.images.add({blob:new Uint8Array(cropped),contentType:'image/png',alt:`Nsight Systems MPI rank ${i}: batch scenarios and CUDA API activity`,fit:'contain',position:{left:205,top:y,width:1007,height:78}});
}
txt(s,'Execution traces guide optimisation; speedup still needs a matched benchmark',64,608,1155,40,24,c.muted);
s=base(3);
txt(s,'Jesús  /  Two approaches investigated in issue #4',64,148,1140,45,28,c.muted);
txt(s,'rothermel-fire-model',64,246,1150,55,39,c.orange,true);
txt(s,'Implemented • Compilation and testing pending',64,312,1145,51,30);
txt(s,'normalized-fuel-model',64,417,1150,55,39,c.orange,true);
txt(s,'Branch created for the alternative approach',64,483,1145,51,30);
txt(s,'Both branches created from dev',64,582,1145,40,25,c.muted);
s=base(4);
const next=[['01','Compile and test Rothermel'],['02','Benchmark the GPU changes'],['03','Work towards integration']];
next.forEach(([n,t],i)=>{txt(s,n,64,204+i*116,100,57,39,c.orange,true);txt(s,t,186,204+i*116,1030,57,39);});
txt(s,'Proposed next steps • Integration and validation remain ahead',64,590,1150,45,25,c.muted);
let md='# EMBER — Four-minute speaker script\n\nTarget: 4:00 at approximately 120 words per minute. Timings are rehearsal targets; pause briefly at paragraph breaks.\n\n';
scripts.forEach((v,i)=>{md+=`## Slide ${i+1} — ${titles[i]} (${times[i]})\n\n${v}\n\n`;});
await fs.writeFile(path.join(out,'EMBER_4min_Script.md'),md);
console.log('Word counts:',scripts.map(v=>v.split(/\s+/).length),'Total:',scripts.join(' ').split(/\s+/).length);
const candidate=path.join(tmp,'candidate.pptx');
await (await PresentationFile.exportPptx(p)).save(candidate);
await finalizePresentation({workspaceDir:root,candidatePath:candidate,finalPath:path.join(out,'EMBER_Overnight_Update.pptx'),pythonExecutable:'C:/Users/ximam/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-bullet-geometry','--validate-heading-fit'],explicitTotalSlideCount:5,fontPolicy:{basis:'design',families:['Arial']},verifyArtifactToolImport:true,receiptPath:path.join(tmp,'validation.json')});
const final=await PresentationFile.importPptx(await FileBlob.load(path.join(out,'EMBER_Overnight_Update.pptx')));
for(let i=0;i<5;i++){const slide=final.slides.items[i];const img=await final.export({slide,format:'png',scale:1});await fs.writeFile(path.join(tmp,`slide-${i+1}.png`),new Uint8Array(await img.arrayBuffer()));}
console.log('Final deck and all five slide previews exported.');
