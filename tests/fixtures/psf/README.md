# Original PSF extraction fixtures

These files contain synthetic Tetorica test tones and PS-X EXE headers only.
They contain no game assets, CPU instructions, or executable sound drivers.
Copyright (c) 2026 kyorohiro, BSD-3-Clause (repository LICENSE).

Generated from the parent Tetorica project's original test helpers:

- `test/helpers/spu_psf_fixture.mjs`: `createPsfFixture()` and
  `createPsfConflictFixture('vab' / 'seq')`.
- `test/helpers/spu_vab_fixture.mjs`: `createVabFixture()`.

`standalone.psf` contains banks 23 and 24 at RAM offsets `0x10000` and
`0x20000`. `track.minipsf` refers to `lib\Bank.psflib` followed by
`patch.psflib`, which overlays fine tuning through an uncached RAM alias.
`conflict-*.bin` is the uncompressed EXE text used by the matching PSF so tests
can modify coverage and body conflicts without depending on the parent repo.
The C++ test also creates its own bounded compressed malformed/dependency cases
at runtime. Tests and builds need no parent checkout, downloads, or game files.
