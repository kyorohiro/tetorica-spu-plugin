# iPlug2 bootstrap attribution

The plugin bootstrap, macOS menu and resource headers are adapted from
[iPlug2OOS TemplateProject](https://github.com/iPlug2/iPlug2OOS/tree/9b214c1121a0da1e8db14b86a565c942a3bfea7e/TemplateProject).
The bundled Roboto-Regular.ttf is copied unchanged from the same template.

The instrument configuration and keyboard use follow the
[iPlug2 IPlugInstrument example](https://github.com/iPlug2/iPlug2/tree/b64192fe18afd9bc9a1fe324db5aceb48f4a0eee/Examples/IPlugInstrument).
iPlug2 copyright and license are retained in
`vendor/iPlug2OOS/iPlug2/LICENSE.txt`.

The Tetorica wrapper replaces the example's gain effect with the native SPU
instrument and adds a fixed-capacity MIDI scheduler. No upstream framework
source is modified. The current development version includes built-in Sine,
embedded VAB instruments, per-channel bend/sustain, ADSR editing and shared
Reverb. Ordinary host parameters store Gain, ADSR and Reverb alongside the
versioned embedded instrument state. Native SDK revisions are pinned in
`dependencies.lock.json`; the Reverb history-clear API was added upstream and
consumed through a new fixed revision rather than an uncommitted vendor patch.
