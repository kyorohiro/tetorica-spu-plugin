// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Cross-check the native writer against the actual Web reader and public UI.
// Generate cases first: web_instrument_export_test --write-fixtures DIR.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir,stat} from 'node:fs/promises';
import {resolve,join,extname,sep} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import http from 'node:http';

const usage='Usage: node scripts/check_web_instrument_export.mjs --web-root /path/to/hello_ymfm --cases DIR [--engines chromium,webkit] [--out REPORT.json]';
const options={engines:[]};
for(let at=2;at<process.argv.length;++at){const key=process.argv[at];assert(['--web-root','--cases','--engines','--out'].includes(key),usage);const value=process.argv[++at];assert(value,usage);options[key.slice(2)]=key==='--engines'?value.split(','):resolve(value);}
assert(options['web-root']&&options.cases,usage);
assert(options.engines.every(value=>['chromium','webkit'].includes(value)),usage);
const upstream=options['web-root'],casesDir=options.cases;
const load=relative=>import(pathToFileURL(join(upstream,relative)).href);
const {readSpuInstrument,writeSpuInstrument}=await load('docs/synth-spu/spu_synth_instrument.js');
const {preparePs1Waveform}=await load('docs/js/ps1_waveform.js');
const {morphPcm}=await load('docs/synth-spu/spu_synth_morph.js');
const {PS1_REVERB_PRESETS}=await load('docs/js/ps1_parameters.js');
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const plain=value=>JSON.parse(JSON.stringify(value,(_key,item)=>ArrayBuffer.isView(item)?Array.from(item):item));
const manifest=JSON.parse(await readFile(join(casesDir,'manifest.json'),'utf8'));
assert(manifest.cases.length>0&&manifest.cases.length<=100,'Bounded generated case list');
const report={startedAt:new Date().toISOString(),scope:'Actual Web reader, writer, and optional public file-load/save UI. No physical listening or cross-runtime PCM identity claim.',sources:[],pure:[],browsers:[]};
for(const relative of ['docs/synth-spu/spu_synth_instrument.js','docs/synth-spu/spu_synth_morph.js','docs/js/ps1_waveform.js'])report.sources.push({path:relative,sha256:hash(await readFile(join(upstream,relative)))});
const checkedCases=[];
for(const entry of manifest.cases){
  const text=await readFile(join(casesDir,entry.file),'utf8'),instrument=readSpuInstrument(JSON.parse(text));
  // JSON.stringify in the real Web save path canonicalizes signed zero.
  const checked=plain(instrument),p=plain(entry.plugin),s=plain(entry.originalSource);
  assert.equal(checked.version,1,entry.id);assert.equal(checked.schema,'tetorica-spu-synth.instrument');
  for(const field of ['volumeLeft','volumeRight','pitch'])assert.equal(checked.preset[field],s[field],`${entry.id}: authored ${field}, independent of plugin Gain`);
  assert.equal(checked.preset.adsr1,p.envelope.enabled?p.envelope.adsr1:s.adsr1,`${entry.id}: ADSR1`);
  assert.equal(checked.preset.adsr2,p.envelope.enabled?(p.envelope.adsr2&~0x2000)|(s.adsr2&0x2000):s.adsr2,`${entry.id}: ADSR2 reserved bit13`);
  assert.equal(checked.preset.reverb,p.reverb.useToneSend?s.reverb:true,`${entry.id}: effective voice send`);
  assert.deepEqual(checked.noise,p.noise,`${entry.id}: shared noise`);
  assert.deepEqual(checked.pmon,{...p.pmon,enabled:p.pmon.enabled&&!p.noise.enabled,waveform:['sine','triangle','saw','square'][p.pmon.waveform]},`${entry.id}: Noise priority over PMON`);
  const expectedMorph={...p.morph,enabled:p.morph.enabled&&!p.noise.enabled};
  assert.deepEqual(checked.morph,expectedMorph,`${entry.id}: Morph endpoints/raw amount and effective mode`);
  assert.deepEqual(checked.reverb,{preset:p.reverb.enabled?PS1_REVERB_PRESETS[p.reverb.preset].id:'off',volumeLeft:p.reverb.volumeLeft,volumeRight:p.reverb.volumeRight},`${entry.id}: effective reverb`);
  if(expectedMorph.enabled){
    const prepared=preparePs1Waveform(morphPcm(expectedMorph));
    assert.deepEqual(checked.waveform,prepared.pcm,`${entry.id}: actual JS blended Float32 cycle`);
    assert.deepEqual(checked.sample,{adpcm:Array.from(prepared.adpcm),baseFrequencyHz:44100/112,loopStartSamples:0},`${entry.id}: independent JS canonical encoder`);
  }else{
    assert.deepEqual(checked.sample,{adpcm:s.adpcm,baseFrequencyHz:s.baseFrequencyHz,...(s.loopStartSamples===null?{}:{loopStartSamples:s.loopStartSamples})},`${entry.id}: original source bytes/tuning/loop`);
    assert.deepEqual(checked.waveform,s.waveform,`${entry.id}: optional cycle`);
  }
  assert.deepEqual(plain(readSpuInstrument(writeSpuInstrument(instrument.name,instrument))),checked,`${entry.id}: actual Web read/write round trip`);
  checkedCases.push({entry,text,instrument,checked});
  report.pure.push({id:entry.id,sourceKind:entry.sourceKind,bytes:Buffer.byteLength(text),adpcmBytes:checked.sample.adpcm.length,sha256:hash(text),passed:true});
}
console.log(`PASS actual Web reader/writer: ${checkedCases.length} cases, original source metadata and independent JS Morph encoder`);

// Every attempted physical destination connection is diverted through a zero
// gain gate before the synthesizer starts. This does not replace its DSP.
function mutePhysicalOutput(){
  const connect=AudioNode.prototype.connect,disconnect=AudioNode.prototype.disconnect,sinks=new Map();
  window.__exportCheck={sinks,errors:[],methods:[]};
  AudioNode.prototype.connect=function(destination,...args){
    if(destination===this.context.destination){let sink=sinks.get(this.context);if(!sink){const sum=this.context.createGain(),gate=this.context.createGain();gate.gain.value=0;connect.call(sum,gate);connect.call(gate,destination);sink={sum,gate};sinks.set(this.context,sink);}connect.call(this,sink.sum,...args);return destination;}return connect.call(this,destination,...args);
  };
  AudioNode.prototype.disconnect=function(...args){if(args[0]===this.context.destination&&sinks.has(this.context))args[0]=sinks.get(this.context).sum;return disconnect.apply(this,args);};
  const post=MessagePort.prototype.postMessage;
  MessagePort.prototype.postMessage=function(message,...args){if(typeof message?.method==='string')window.__exportCheck.methods.push(message.method);return post.call(this,message,...args);};
}
let server;
try{
  if(options.engines.length){
    const require=createRequire(join(upstream,'package.json')),playwright=require('playwright');
    const docs=join(upstream,'docs');
    const types={'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.wasm':'application/wasm','.json':'application/json','.svg':'image/svg+xml','.webp':'image/webp','.png':'image/png'};
    server=http.createServer(async(request,response)=>{try{const pathname=decodeURIComponent(new URL(request.url,'http://localhost').pathname);let file=resolve(docs,`.${pathname}`);if(file!==docs&&!file.startsWith(docs+sep))throw Error('Forbidden');if((await stat(file)).isDirectory())file=join(file,'index.html');response.setHeader('Content-Type',types[extname(file)]??'application/octet-stream');response.end(await readFile(file));}catch{response.writeHead(404);response.end('Not found');}});
    await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
    const address=`http://127.0.0.1:${server.address().port}/synth-spu/index.html`;
    for(const engine of options.engines){
      const browser=await playwright[engine].launch({headless:true});
      try{
        for(const {entry,text,instrument,checked} of checkedCases){
          const context=await browser.newContext({acceptDownloads:true}),page=await context.newPage(),errors=[];
          try{
            page.on('pageerror',error=>errors.push(String(error)));
            await page.addInitScript(mutePhysicalOutput);await page.goto(address,{waitUntil:'load'});
            await page.locator('#presetFileInput').setInputFiles({name:entry.file,mimeType:'application/json',buffer:Buffer.from(text)});
            await page.waitForFunction(name=>document.querySelector('#audioStatus')?.textContent===`Loaded ${name}.`,instrument.name,{timeout:20000});
            assert.equal(await page.locator('#instrumentName').inputValue(),instrument.name);
            const downloadPromise=page.waitForEvent('download',{timeout:15000});await page.locator('#savePreset').click();
            const download=await downloadPromise,stream=await download.createReadStream(),chunks=[];for await(const chunk of stream)chunks.push(chunk);
            const reloaded=plain(readSpuInstrument(JSON.parse(Buffer.concat(chunks).toString('utf8'))));
            const expected=structuredClone(checked);
            // The Web's actual public loader intentionally canonicalizes a
            // legacy cycle's bytes while retaining its exact Float32 cycle.
            if(expected.waveform)expected.sample.adpcm=Array.from(preparePs1Waveform(expected.waveform).adpcm);
            assert.deepEqual(reloaded,expected,`${engine}/${entry.id}: real public file load -> engine -> downloaded save`);
            const audit=await page.evaluate(()=>({muted:__exportCheck.sinks.size>0&&[...__exportCheck.sinks.values()].every(sink=>sink.gate.gain.value===0),methods:__exportCheck.methods}));
            assert(audit.muted,`${engine}/${entry.id}: physical output remains muted`);
            assert(audit.methods.includes('getInstrument'),`${engine}/${entry.id}: saved actual engine instrument`);
            assert.deepEqual(errors,[],`${engine}/${entry.id}: no browser exceptions`);
            report.browsers.push({engine,id:entry.id,passed:true,physicalOutputMuted:true,savedFromActualEngine:true,legacyBytesCanonicalized:!assertBytesEqual(checked.sample.adpcm,expected.sample.adpcm)});
            console.log(`PASS ${engine} ${entry.id}: public load/save round trip, physical output muted`);
          }finally{await context.close();}
        }
      }finally{await browser.close();}
    }
  }
  report.completedAt=new Date().toISOString();report.passed=true;
}catch(error){report.completedAt=new Date().toISOString();report.passed=false;report.error=String(error?.stack??error);throw error;}
finally{if(server)await new Promise(resolve=>server.close(resolve));if(options.out){await mkdir(resolve(options.out,'..'),{recursive:true});await writeFile(options.out,JSON.stringify(report,null,2)+'\n');}}
function assertBytesEqual(a,b){return a.length===b.length&&a.every((value,index)=>value===b[index]);}
