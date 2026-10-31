// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Regenerate checked-in independent Web encoder vectors. This script is not a
// build dependency and never downloads or executes a remote source.
// node tests/fixtures/waveform/generate_web_vectors.mjs /path/to/hello_ymfm/docs/js/ps1_adpcm_encoder.js
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';

if(process.argv.length!==3)throw new Error('Pass the local Web ps1_adpcm_encoder.js reference path');
const reference=resolve(process.argv[2]);
const source=await readFile(reference);
const {encodePs1AdpcmCycle}=await import(pathToFileURL(reference));
const cases=[];
for(const shape of ['sine','triangle','saw','square']){
  const pcm=Float32Array.from({length:112},(_,i)=>{
    const phase=i/112;
    return .8*(shape==='sine'?Math.sin(2*Math.PI*phase):
      shape==='triangle'?2/Math.PI*Math.asin(Math.sin(2*Math.PI*phase)):
      shape==='saw'?2*phase-1:phase<.5?1:-1);
  });
  cases.push([shape,pcm]);
}
cases.push(['silence',new Float32Array(112)]);
cases.push(['negative_dc',new Float32Array(112).fill(-.3125)]);
let seed=0x5931ab;
cases.push(['seeded_noise',Float32Array.from({length:112},()=>{
  seed=(Math.imul(seed,1664525)+1013904223)>>>0;return .95*(seed/0xffffffff*2-1);
})]);
cases.push(['limits_and_ties',Float32Array.from({length:112},(_,i)=>
  i%7===0?-1:i%7===1?1:i%7===2?0:(i%2?-1:1)*(i*13+.5)/32768)]);

const lines=['// SPDX-License-Identifier: BSD-3-Clause',
  '// Generated original PCM and ADPCM reference data; do not hand edit.',
  '// Source: hello_ymfm/docs/js/ps1_adpcm_encoder.js',
  '// Web encoder SHA256: '+createHash('sha256').update(source).digest('hex'),
  '#pragma once','#include <array>','#include <cstdint>',
  'namespace waveform_reference {',
  'struct Vector { const char* name; std::array<std::uint32_t,112> bits; std::array<std::uint8_t,64> adpcm; };',
  `inline constexpr std::array<Vector,${cases.length}> vectors{{`];
for(const [name,pcm] of cases){
  const view=new DataView(pcm.buffer,pcm.byteOffset,pcm.byteLength);
  lines.push(`  {"${name}",{{`);
  const bits=Array.from({length:112},(_,i)=>'0x'+view.getUint32(i*4,true).toString(16).padStart(8,'0')+'u');
  for(let i=0;i<bits.length;i+=8)lines.push('    '+bits.slice(i,i+8).join(',')+',');
  lines.push('  }},{{');
  const {adpcm}=encodePs1AdpcmCycle(pcm);
  for(let i=0;i<adpcm.length;i+=16)lines.push('    '+Array.from(adpcm.slice(i,i+16)).join(',')+',');
  lines.push('  }}},');
}
lines.push('}};','} // namespace waveform_reference','');
await writeFile(new URL('./web_encoder_vectors.h',import.meta.url),lines.join('\n'));
console.log(JSON.stringify({cases:cases.length,framesPerCase:112,bytesPerCase:64,
  encoderSha256:createHash('sha256').update(source).digest('hex')}));
