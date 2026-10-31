# Original synthetic VAB regression fixture

`spu-synth-demo.vab`, `demo.vh` and `demo.vb` are exact copies of the original
synthetic import demos in the parent `hello_ymfm` repository at
`docs/playground/samples/spu-import/`. They contain no game code, game samples or
external recordings. The original BSD-3-Clause `LICENSE` is preserved verbatim.

The complete bank is 3744 bytes; its split header/body are 3616/128 bytes. Sparse
Programs 3 and 11 contain three Tones and reference two Samples. The sample-size
table reserves a 16-byte body prefix. The Sample slices are 80/32 bytes, with
first END boundaries at 64/16 bytes. Unreferenced trailing bytes are deliberate.

`expected.json` records the source repository and exact Git commit, SHA-256 of
every binary/license and the three source files, complete parsed metadata, and
seven prepared-Tone results computed by the existing JavaScript importer. Cases
cover the three original Tones, asymmetric volume/pan/tuning/reverb metadata,
nonzero flag-4 loop start, loop fallback to zero, and an earlier one-shot END.
`expected.h` is the bounded C++ representation generated from those same results.

To reproduce against a checkout of the recorded parent source:

```sh
node tests/check_vab_fixture_parity.mjs --parent /path/to/hello_ymfm
```

To deliberately refresh copies and expected results:

```sh
node tests/check_vab_fixture_parity.mjs --parent /path/to/hello_ymfm --write
```

The checker imports `docs/playground/playground_ps1_vab.js` and
`docs/js/ps1synth.js`, verifies complete/split equality, and records the resulting
metadata and exact original ADPCM bytes. It does not run the C++ parser, an SPU,
or a DAW. Ordinary C++ builds/tests use only the checked-in fixture; they do not
require Node or the parent workspace.

A matching split header/body size is structural validation, not a checksum of
the original pair. An unrelated body of the same length cannot be identified
from the VAB format alone. The flag-4 preparation case establishes metadata and
byte-preservation parity, not Native SDK loop-playback support.
