// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual Web v2 interoperability. Never materialize recording PCM as JS arrays.
import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir,readdir,stat} from 'node:fs/promises';
import {resolve,join,extname,sep,dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import http from 'node:http';
const usage='Usage: node scripts/check_web_software_audio.mjs --web-root DIR --cases DIR [--engines chromium,webkit] [--out REPORT.json]';
const options={engines:[]};
for(let at=2;at<process.argv.length;++at){const key=process.argv[at];assert(['--web-root','--cases','--engines','--out'].includes(key),usage);const value=process.argv[++at];assert(value,usage);options[key.slice(2)]=key==='--engines'?value.split(','):resolve(value);}
assert(options['web-root']&&options.cases,usage);assert(options.engines.every(x=>['chromium','webkit'].includes(x)),usage);
const root=options['web-root'];
const load=name=>import(pathToFileURL(join(root,name)).href);
const {readSpuInstrument,writeSpuInstrument}=await load('docs/synth-spu/spu_synth_instrument.js');
const hash=data=>createHash('sha256').update(data).digest('hex');
function pcmHash(pcm){assert(pcm instanceof Int16Array);const digest=createHash('sha256'),chunk=Buffer.alloc(65536);
  for(let at=0;at<pcm.length;at+=chunk.length/2){const count=Math.min(chunk.length/2,pcm.length-at);for(let n=0;n<count;++n)chunk.writeInt16LE(pcm[at+n],n*2);digest.update(chunk.subarray(0,count*2));}return digest.digest('hex');}
function snapshot(instrument){assert.equal(instrument.version,2);const {pcm,...sample}=instrument.sample;
  assert(pcm instanceof Int16Array&&pcm.length>0&&pcm.byteLength<=32*1024*1024,'Plugin32MiB decoded PCM bound');
  const metadata=JSON.parse(JSON.stringify({...instrument,sample}));return {metadata,frames:pcm.length,pcm16Sha256:pcmHash(pcm)};}
let manifest;
try{manifest=JSON.parse(await readFile(join(options.cases,'software-manifest.json'),'utf8'));}catch(error){if(error.code!=='ENOENT')throw error;}
const cases=manifest?.cases??(await readdir(options.cases)).filter(x=>x.endsWith('.spu.json')).map(file=>({id:file,file}));
assert(cases.length>0&&cases.length<=100,'Bounded software fixture list');
const report={startedAt:new Date().toISOString(),scope:'Plugin-generated Web v2, JSON<=48MiB and monoPCM16<=32MiB. Actual Web supports larger inputs; this does not test its full30-minute range. No physical listening or Native/Web rendered-PCM identity claim.',sources:[],pure:[],browsers:[]};
for(const relative of ['docs/synth-spu/spu_synth_instrument.js','docs/synth-spu/spu_synth_long_audio.js'])report.sources.push({path:relative,sha256:hash(await readFile(join(root,relative)))});
let server;
try{
  const fixtures=[];
  for(const entry of cases){
    const bytes=await readFile(join(options.cases,entry.file));assert(bytes.length<=48*1024*1024,'Plugin48MiB JSON bound');
    const instrument=readSpuInstrument(JSON.parse(bytes.toString('utf8'))),expected=snapshot(instrument);
    if(entry.frames!==undefined)assert.equal(expected.frames,entry.frames,entry.id);
    if(entry.pcm16File)assert.equal(expected.pcm16Sha256,hash(await readFile(join(options.cases,entry.pcm16File))),`${entry.id}: independent generated PCM16LE sidecar`);
    assert.deepEqual(snapshot(readSpuInstrument(writeSpuInstrument(instrument.name,instrument))),expected,`${entry.id}: actual Web writer/reader`);
    fixtures.push({entry,expected});report.pure.push({id:entry.id,file:entry.file,jsonBytes:bytes.length,jsonSha256:hash(bytes),...expected,passed:true});
    console.log(`PASS actual Web v2 reader/writer ${entry.id}: ${expected.frames}frames, exact PCM16LE SHA`);
  }
  if(options.engines.length){
    const require=createRequire(join(root,'package.json')),playwright=require('playwright'),docs=join(root,'docs');
    const types={'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.wasm':'application/wasm','.json':'application/json','.svg':'image/svg+xml','.webp':'image/webp','.png':'image/png'};
    server=http.createServer(async(req,res)=>{try{let file=resolve(docs,`.${decodeURIComponent(new URL(req.url,'http://localhost').pathname)}`);if(file!==docs&&!file.startsWith(docs+sep))throw Error('Forbidden');if((await stat(file)).isDirectory())file=join(file,'index.html');res.setHeader('Content-Type',types[extname(file)]??'application/octet-stream');res.end(await readFile(file));}catch{res.writeHead(404);res.end('Not found');}});
    await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
    const url=`http://127.0.0.1:${server.address().port}/synth-spu/index.html`;
    for(const engine of options.engines){const browser=await playwright[engine].launch({headless:true});
      try{for(const {entry,expected} of fixtures){const context=await browser.newContext({acceptDownloads:true}),page=await context.newPage(),errors=[];
        try{page.on('pageerror',error=>errors.push(String(error)));await page.addInitScript(muteOutput);await page.goto(url,{waitUntil:'load'});
          await page.locator('#presetFileInput').setInputFiles(join(options.cases,entry.file));
          await page.waitForFunction(name=>document.querySelector('#audioStatus')?.textContent===`Loaded ${name}.`,expected.metadata.name,{timeout:60000});
          assert.equal(await page.locator('#instrumentName').inputValue(),expected.metadata.name);
          const downloadPromise=page.waitForEvent('download',{timeout:60000});await page.locator('#savePreset').click();
          const download=await downloadPromise,stream=await download.createReadStream(),chunks=[];for await(const chunk of stream)chunks.push(chunk);
          const actual=snapshot(readSpuInstrument(JSON.parse(Buffer.concat(chunks).toString('utf8'))));
          assert.deepEqual(actual,expected,`${engine}/${entry.id}: public file load -> actual engine -> downloaded Save`);
          const audit=await page.evaluate(()=>({muted:__softwareCheck.sinks.size>0&&[...__softwareCheck.sinks.values()].every(x=>x.gate.gain.value===0),saved:__softwareCheck.methods.includes('getInstrument')}));
          assert(audit.muted&&audit.saved,'Actual engine save with physical destination muted');assert.deepEqual(errors,[],'No page exceptions');
          report.browsers.push({engine,id:entry.id,frames:actual.frames,pcm16Sha256:actual.pcm16Sha256,metadataExact:true,physicalOutputMuted:true,savedFromActualEngine:true,passed:true});
          console.log(`PASS ${engine} ${entry.id}: public v2 load/save, PCM hash+metadata exact, physical output muted`);
        }finally{await context.close();}}
      }finally{await browser.close();}}
  }
  report.passed=true;
}catch(error){report.passed=false;report.error=String(error?.stack??error);throw error;}
finally{report.completedAt=new Date().toISOString();if(server)await new Promise(resolve=>server.close(resolve));if(options.out){await mkdir(dirname(options.out),{recursive:true});await writeFile(options.out,JSON.stringify(report,null,2)+'\n');}}
function muteOutput(){const connect=AudioNode.prototype.connect,disconnect=AudioNode.prototype.disconnect,sinks=new Map();window.__softwareCheck={sinks,methods:[]};
  AudioNode.prototype.connect=function(destination,...args){if(destination===this.context.destination){let sink=sinks.get(this.context);if(!sink){const sum=this.context.createGain(),gate=this.context.createGain();gate.gain.value=0;connect.call(sum,gate);connect.call(gate,destination);sink={sum,gate};sinks.set(this.context,sink);}connect.call(this,sink.sum,...args);return destination;}return connect.call(this,destination,...args);};
  AudioNode.prototype.disconnect=function(...args){if(args[0]===this.context.destination&&sinks.has(this.context))args[0]=sinks.get(this.context).sum;return disconnect.apply(this,args);};
  const post=MessagePort.prototype.postMessage;MessagePort.prototype.postMessage=function(value,...args){if(typeof value?.method==='string')__softwareCheck.methods.push(value.method);return post.call(this,value,...args);};}
