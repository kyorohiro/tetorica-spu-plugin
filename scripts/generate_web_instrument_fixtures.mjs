// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Run with the original hello_ymfm checkout as the single argument. Only this
// repository's original test fixtures are written; no game assets are used.
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';

if(process.argv.length!==3)throw new Error('Usage: node scripts/generate_web_instrument_fixtures.mjs /path/to/hello_ymfm');
const upstream=resolve(process.argv[2]);
const load=path=>import(pathToFileURL(join(upstream,path)).href);
const {readSpuInstrument,writeSpuInstrument}=await load('docs/synth-spu/spu_synth_instrument.js');
const {readMorph,morphShape,morphPcm}=await load('docs/synth-spu/spu_synth_morph.js');
const {encodePs1Adpcm}=await load('docs/js/ps1_adpcm_encoder.js');
const {preparePs1Waveform}=await load('docs/js/ps1_waveform.js');
const output=fileURLToPath(new URL('../tests/fixtures/web/',import.meta.url));await mkdir(output,{recursive:true});
const sha=value=>createHash('sha256').update(value).digest('hex');
const floatBytes=pcm=>{const bytes=Buffer.alloc(pcm.length*4);pcm.forEach((x,i)=>bytes.writeFloatLE(x,i*4));return bytes;};
const sourceFiles=['docs/synth-spu/spu_synth_instrument.js','docs/synth-spu/spu_synth_morph.js',
 'docs/synth-spu/spu_synth_noise.js','docs/synth-spu/spu_synth_pmon.js','docs/js/ps1_adpcm_encoder.js','docs/js/ps1_waveform.js'];
const references=[];
for(const path of sourceFiles)references.push({path,sha256:sha(await readFile(join(upstream,path)))});
const base={preset:{volumeLeft:-.2,volumeRight:.8,adsr1:0x8f4a,adsr2:0xe789,pitch:0x1234,reverb:true},
 reverb:{preset:'studio-small',volumeLeft:.33,volumeRight:.27}};
const shot=encodePs1Adpcm(Float32Array.from({length:57},(_,i)=>.5*Math.sin(2*Math.PI*i/57))).adpcm;
const arbitrary={...base,sample:{adpcm:shot,baseFrequencyHz:123.5},waveform:null};
const cycle=morphShape('sine'),canonical=preparePs1Waveform(cycle);
const waveform={...base,preset:{...base.preset,adsr1:15,adsr2:0x1fc0},
 sample:{adpcm:canonical.adpcm,baseFrequencyHz:44100/112,loopStartSamples:0},waveform:canonical.pcm};
const legacy={...waveform,sample:{...waveform.sample,adpcm:encodePs1Adpcm(Float32Array.from(cycle),{loopStartSamples:0,maxBytes:64}).adpcm}};
const morph=readMorph({enabled:true,amount:.375});const blended=preparePs1Waveform(morphPcm(morph));
const morphSource={...waveform,sample:{...waveform.sample,adpcm:blended.adpcm},waveform:blended.pcm,morph,
 pmon:{enabled:true,waveform:'triangle',ratio:1.5,depth:.25,decayMs:0},noise:{enabled:false,clock:37}};
const noise={...arbitrary,preset:waveform.preset,noise:{enabled:true,clock:8}};
const marker=new Uint8Array(48);for(let i=0;i<3;++i)marker[i*16]=12;marker[17]=4;marker[33]=3;
const flag4={...arbitrary,sample:{adpcm:marker,baseFrequencyHz:220,loopStartSamples:28}};
const longAudio={...base,sample:{pcm:new Int16Array([0,32767,-32768,123]),baseFrequencyHz:220,loop:false},waveform:null};
const cases=[];
for(const [file,name,source,strip] of [
 ['v1-arbitrary.spu.json','  明るい One shot  ',arbitrary,false],
 ['v1-defaults.spu.json','Defaults',arbitrary,true],
 ['v1-waveform.spu.json','Canonical cycle',waveform,false],
 ['v1-waveform-legacy.spu.json','Legacy cycle',legacy,false],
 ['v1-morph.spu.json','Morph + PMON',morphSource,false],
 ['v1-noise.spu.json','Noise',noise,false],
 ['v1-loop-flag4.spu.json','Structurally valid unsupported loop',flag4,false],
 ['v2-long-audio.spu.json','Unsupported long audio',longAudio,false]]) {
 const value=writeSpuInstrument(name,source);
 if(strip)for(const key of ['pmon','noise','morph','waveform'])delete value[key];
 const checked=readSpuInstrument(JSON.parse(JSON.stringify(value)));
 await writeFile(join(output,file),JSON.stringify(value,null,2)+'\n');
 cases.push({file,version:checked.version,expected:{name:checked.name,preset:checked.preset,reverb:checked.reverb,
   pmon:checked.pmon,noise:checked.noise,morph:checked.morph,waveform:checked.waveform,
   adpcmBytes:checked.version===1?checked.sample.adpcm.length:null,
   adpcmSha256:checked.version===1?sha(checked.sample.adpcm):null,
   baseFrequencyHz:checked.sample.baseFrequencyHz,loopStartSamples:checked.sample.loopStartSamples??null,
   waveformFloat32Sha256:checked.waveform?sha(floatBytes(checked.waveform)):null}});
}
const reference={description:'Original synthesized fixtures validated by the actual Web instrument writer and reader; no copyrighted game data.',
 upstreamCommit:execFileSync('git',['rev-parse','HEAD'],{cwd:upstream,encoding:'utf8'}).trim(),sources:references,cases};
await writeFile(join(output,'reference.json'),JSON.stringify(reference,null,2)+'\n');
console.log(`Generated ${cases.length} original Web fixtures and reference.json`);
