# 画面鍵盤のOctaveとPitch（2026-11-02 JST）

開始`dc7d57b`、実装前TODO `3abafff`。v0.0.0 Preview公開後の開発buildに、画面鍵盤のOctave切替とPitch wheelを追加した。
SPU Synthのitch.io devlogは同日、ユーザーが掲載済みと報告した。

## 操作

- **Octave − / ＋**：初期MIDI48〜84（C3〜C6）の37鍵を、開始位置0 / 12 / … / 84へ動かす。上下限では対応buttonを無効化する。DAW入力のnote番号は変更しない。
- **Pitch**：Morph横の縦wheel。画面鍵盤と同じMIDI channel 1へ14-bit Bendを送り、±2半音の範囲で保持音にも反映する。SPU Noiseの音程は変わらない。
- **離す・取消・mouse exit・editor close**：UIで操作中のnote / PitchだけをNoteOff / centerへ戻す。gestureがないeditor開閉でDAW由来のBendを変更しない。

Octaveはinstance内のeditor設定で、開き直しには引き継ぐがproject /presetには保存しない。
Pitch表示はローカルwheelの位置を示し、外部MIDIのBendは追跡しない。同じchannelの複数controllerは後に処理された値を使う。
host parameter ID0〜25・range・state schema10・SDK pin・音源を維持した。Gainは0〜100%のまま。

## NoteOffとlifecycle

upstream `IVKeyboardControl::SetNoteRange()`は押下bitmapを消すが、元noteのNoteOffを送らない。
専用subclassで古いrangeのままpointer操作を取り消してからrangeを変更する。
mouse exit後の古いdragを無視し、mouse up / closeでも同じ取消経路を使う。

全controller resetを行う既存`resetControllerCache()`で、専用atomic epochも進める。
reset / deactivate / restore / overflow / VST3 bypass後の古いpointer eventは、noteやBendを送らず破棄する。
UI timerは各control自身のepochが古い時だけ表示をresetする。
独立レビューで、reset直後・timer前に始まった新しいgestureまで消すraceを発見し、この判定へ修正した。
UI eventとhost lifecycleの完全な同時実行を網羅するstress試験は行っていない。

Pitchは標準wheelのbend-range menuを使わず、±2固定の小さな専用controlとした。
通常のUI MIDI FIFOを通し、host parameterや音色stateへ一時的なwheel位置を保存しない。
frameworkのQWERTY handlerはinstance間でstatic状態を共有するため、今回PC keyboard演奏は追加していない。

## 検証

- macOS APP / AU / VST3をbuildし、CTest30件、portable Python112件が成功した。
- 新unitはOctave上下限 /int限界 /37鍵範囲 /instance独立と、Pitch全16,384値の到達性 /単調性 /両端 /中心 /取消 /重複終了 /不正入力を確認した。
- 最終buildの実AU（48kHz）・実VST3（44.1 /48 /96kHz）のMIDI /legacy state /保存復元 /PCM回帰が成功し、公式VST3 validatorは47 passed /0 failed。
- Standalone画面でOctave0→+1→+3、上限button無効化、0への復帰を確認した。Pitch drag後はcenter `+0.00`、鍵盤drag後は離鍵表示となり、Pitch /Morph /範囲表示の重なりがないことを確認した。
- NoteOff-before-rangeとepoch判定はsource reviewで検査した。単体model /headless host /Standalone画面確認を、実DAW独自UIや聴感の検証とは区別する。

最初のsandbox内AU検証はローカルfactory登録で停止した。同じ最終bundleをsandbox外で直接hostし、全検査が成功した。インストール先のAUは変更していない。
CI結果・source /log /bundle hashは[機械可読記録](keyboard_performance_20261102.json)に記録する。

## 残る確認

実DAW内の独自UI、MIDIとの併用・録音、操作中のeditor close / restore、実deviceの聴感・長時間動作は追試する。
公開済み`v0.0.0` tag / assetsと手元installed AUは今回更新しない。次の配布は別versionとして判断する。
