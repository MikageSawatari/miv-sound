# JSON 保存形式の比較と互換性検証（2026-10-27）

設計書第8版 §5.5 の「JSON ファイル側の形の確認」に対応。作業の基準は miv ブランチ ba7f2aa。作業ツリーを切り替えず、git show / git diff / git ls-tree で各タグと、それぞれが固定する external/effetune の commit を読んだ。mIV 本体・submodule・製品コードは変更していない。コミットなし。

## 比較した正確な版

| Mixwright | リポジトリの commit | external/effetune の gitlink |
| --- | --- | --- |
| v0.12.0 | 6f4e2ee0776ec20cb6685774690ab1b803a2d50a | 03bffda352287f4f438d063d9660aad7864e553a |
| v0.13.0 | 230e5ad8ae8f6e866bc923419fbc198ee559c856 | 6791afdfc6f4e6c913b9f48c8189a44e29a09bde |
| v0.14.0 | e4147644305f0d373e362fc1485b4cb9889e5b63 | 01c96f00f8e93595aefab96de4bc4d210e61c53b |
| v0.15.0 | 8c9a0a561d2ee88d2eef6b06bab138bbd639b3c1 | c506bbab8a3dcd75bc5049ebb9b92572b526c557 |
| miv / ba7f2aa | ba7f2aa54ec4c0a7b2e263035351b6415ff23f46 | c506bbab8a3dcd75bc5049ebb9b92572b526c557 |

調査コマンドの例は git ls-tree v0.14.0 external/effetune、git show v0.14.0:src/bridge/sfz_library.cpp、git -C external/effetune show 01c96f00:js/ui/pipeline/preset-manager.js、各隣接版間の git diff。タグ名が同じ別リポジトリの UI を推測せず、必ず親タグの gitlink を使った。

## ファイルの形の比較

| ファイル／担当 | v0.12 | v0.13 | v0.14 | 現在の miv（v0.15） |
| --- | --- | --- | --- | --- |
| config.json / ConfigStore | JSON object のテキストをそのまま保存。64KiBまで。 | 同じ | 同じ | 中身の処理は同じ。配置と保存先不正時の拒否だけを F1 で変更。 |
| effetune_presets.json / PresetStore + UI PresetManager | 名前をキーにした object。保存は {plugins:[short state...]}。読み込みは {pipeline:[long state...]} にも対応。 | 同じ | 同じ | 同じ |
| effetune_plugin_presets.json / PresetStore + UI PluginPresetStore | {effect表示名:{preset名:parameters object}} | 同じ | 同じ | 同じ |
| .effetune_preset の JSON payload / UI export | {name,pipeline:[long state...],timestamp}。timestamp は Date.now()。 | 同じ | 同じ | 同じ。既定のファイル名は F1 の branding、拡張子は維持。 |
| sfz-references.json / SfzLibrary | native registry と SfzLibrary が存在しない。 | 配列。各要素は {id,name,path,root}。 | 配列の各要素に bankPaths、importedAt を追加。旧4キーも読める。 | v0.14 と同じ。F1 の差分は空の registry path の拒否だけ。 |

short state は {nm,en,...parameters,ib?,ob?,ch?}、long state は {name,enabled,parameters,inputBus?,outputBus?,channel?}。既定 routing は省略できる。既存の channel alias の正規化も4版で同じ。プリセットの保存対象は現在の pipeline であり、これらの表を StateCodec の formatVersion:1 / pipelineA / pipelineB と混同しない。

config と2つの preset ファイルは、native store では JSON の意味を解釈せず文字列を読む／書く。config/save の bridge は object を要求し、UI 側も preset の表を object として扱う。SFZ だけは native の型付き load / save で要素を再構成する。これらのファイル自体には版番号の envelope はない。後述の midiController.version は内部の controller 設定の版である。

Windows の旧配置は config / SFZ が %APPDATA%/Frieve/EffeTunePlugin。プリセットは %APPDATA%/effetune フォルダがあればそのフォルダ、なければ Frieve/EffeTunePlugin を2ファイルとも使う。現行はすべて MIV_SOUND_DATA_DIR/plugin。今回の合成旧配置は Frieve 側を代表にし、どちらを移行元に選ぶかという mIV 側の処理は実装していない。JSON の形は元のフォルダでは変わらない。

欠落時も、config は {}、preset は UI が空の表として扱い、native SFZ のない v0.12 からの移行は現行 SfzLibrary の list が空配列を返す。テストでは SFZ の欠落から読み取りだけで registry JSON を新設しないことも確認した（lock ファイルは新しい検証用 plugin フォルダ内に作られる）。

## 設定キーと UI 側の書き込み

| 版／範囲 | キー・構造と差分 |
| --- | --- |
| 共通 | language、theme、columns、startupView、pipelineStartup、startupPreset、visualSync、spectrumOverlayQuality、spectrumOverlayPeakHold。powerSaving は {mode,silenceThresholdDb,fullSuspendDelaySeconds,skipDisplayDspWhenHidden}、offlineOutput は {format,sampleRate,wavSampleFormat,flacSampleFormat}。キーは省略可能。 |
| 共通の controller | midiController:{version:1,devices:[{key,protocol}],mappings:[{id,device,source,target,map}]}。source の種類ごとのフィールド、target の type/instance/param/element、map の lo/hi/sensitivity/dir/buttonMode/behavior/amount を保存する。validator / writer は4版で同じ。fixture に有効な note → masterBypass の mapping を含めた。 |
| 共通の desktop 設定 | autoLaunch、startMinimized、minimizeToTray、checkForUpdatesOnStartup、hardwareAcceleration、libraryStartupView 等。プラグインで非対応の機能でも、保存済みのキーを native store / config の shallow merge が削除することはない。 |
| v0.12 → v0.13 | frequencyPreviewSound（sine / bandpassNoise）、sfzMaxSizeMiB が追加。VST shim は v0.12 の startupView 強制 effects から、v0.13 以降は effects / visualizer を許す。spectrumOverlayQuality の hq は nativeSpectrumTap が利用可能な場合だけ UI に渡す。 |
| v0.13 → v0.14 | onboardingTourShown が追加され、初回案内を表示したことを保存する。configIntegration.js の設定画面の保存構造自体は同じ。 |
| v0.14 → v0.15 | pipelineColumnMode（auto / manual）、libraryIncludeVideo、videoAutoRotate、desktop の browser 設定が追加。browser は {userAgent,openTabsExternally,startPage}。miv の生成 UI は desktop browser のカテゴリを除外する。Visualizer HDR の設定は localStorage であり config.json のキーではない。 |

config-store.js は load 時に offlineOutput を正規化し、save 時は既存の snapshot と patch を shallow merge する。theme mirror は localStorage 用であり、保存済みの config のキーを削除する処理ではない。設定画面は個々の patch をこの共通 writer へ渡す。

検証した blob 一致の主な証拠:

- ConfigStore は上流4タグすべて e091ab2c、PresetStore はすべて 502369a6。
- UI config-store.js はすべて a5e697d4、MidiMappingStore はすべて ff45fa16。
- UI PluginPresetStore はすべて c211115a、serialization-utils.js はすべて 2f2f360f、pipeline-state-codec.js はすべて 09ee4558。
- offline-output-settings.js、power-policy.js、theme-registry.mjs、language-options.js も4版で一致。
- UI pipeline PresetManager の v0.13 → v0.14 の差分は、load 後に updateAllPluginDisplayState を呼ぶ1行のみ。v0.14 と v0.15 は同一。
- SfzLibrary の v0.14 / v0.15 はともに b6ef9f68。miv 側の変更は registry path が空の場合の拒否で、形式の変更ではない。

## SFZ のディスク形式と UI 形式

ディスクの id は24文字の小文字16進文字列。path は選択された .sfz の絶対パス、root はその読み取りを許したフォルダの絶対パスで、path は root 配下でなければならない。コピーした registry の場所が変わっても、参照先の音源を移したり絶対パスを書き換えたりしない。

v0.14 の bankPaths は root からの相対パスの配列（*.bank.xml）、importedAt は UTC の文字列（通常 YYYY-MM-DDTHH:mm:ss.sssZ）。load は両方の欠落を許し、save は空の [] / "" も明示して書く。select で再選択した場合は bankPaths の再走査と importedAt の更新を行うため、今回の「中身を変えない再保存」は select ではなく、存在しない有効 ID の remove による load → save で確認した。

UI bridge に渡るのはディスクの absolute path / root そのものではなく、id、name、selectedPath（root 相対）、bankPaths、必要なら importedAt。v0.13 の public entry は id/name/selectedPath、v0.14 以降は bankPaths と任意の importedAt が追加される。UI NativeSfzLibraryService の list は表示用に id/name/importedAt を取り、prepare は native の list を再取得して selectedPath / bankPaths を使う。UI に別の registry JSON writer はない。

## 再保存と旧版への持ち戻しで変わる点

1. **config と preset 表をそのまま保存する場合**、native store は全キー・値を保持する。UI の他の設定への patch 保存も、desktop 専用キーや controller 設定を維持する。config の offlineOutput が省略されていれば、UI の load/save で4つの既定キーが追加される。この正規化は v0.12 から同じである。
2. **v0.13 の SFZ registry を現行で保存**すると bankPaths:[] と importedAt:"" が追加される。元の4キー、ID、参照先、音源の内容は変わらない。現行・v0.14 の形式の既存6キーは保持される。
3. **v0.14 の SFZ registry を v0.13 側で再保存**すると、v0.13 の load/save が4キーだけを再構成するので bankPaths / importedAt が消える（旧コードから確認。旧 DLL は実行していない）。再び新しい版へ戻しても、これらは空としてしか復元できない。bankPaths は AriaBank 定義の解決に使うため、これを必要とする音源では再選択等が必要になる。単なるプリセットの .effetune_preset 書き出しは registry 自体を含まないので、この降格変換を自動で起こすものではない。
4. **long 形式を読み込んで同じ名前で UI 保存**すると、{pipeline:[...],name,timestamp} から {plugins:[...]} へ変わり、name / timestamp の metadata は消える。パラメータ、有効状態、routing は保持する。これは v0.12 からの共通動作であり、各旧版が両形式を読める。export は再び long 形式を作り、timestamp は新しい値になる。
5. **旧 columns は保持されるが表示の意味は変わる**。v0.15 の PipelineColumnManager は pipelineColumnMode がない場合に auto とし、旧 columns をそのまま列数としては使わない。manual を選ぶと既存値を使える。fixture の columns:4 が1列幅の環境で auto=1、manual=4 となることを実コードで確認した。移行のために旧 JSON へ manual を書き足す処理は追加していない。
6. **別領域との資産の関連付けは JSON の構造互換とは別**。SFZ Note Player のプリセットは sf に参照 ID を保存する。参照 registry を共有しない別の DAW / miv 側にその ID がなければ、プリセットだけでは音源は復元しない。また v0.12 には native SFZ がない。個々の新しいエフェクトを旧版が実行できることまでは、保存表の形式一致から保証しない。

サイズ上限は共通で config 64KiB、preset write 8MiB、SFZ registry 1MiB / 10000項目。合成 fixture は代表的な範囲のもので、上限サイズの全組み合わせは試していない。v0.13 の SFZ に空の追加 metadata を書くぶんサイズが増えるので、1MiBに近い旧 registry は再保存時に上限に達し得る（コード上の制限）。コピー処理で元の JSON を変更して解決する方針にはしていない。

## fixture と自動テスト

[fixture の説明](../tests/fixtures/json-storage/README.md) に配置を記載。値はすべて合成し、利用者の実データを使っていない。

- v0.12 の config.json。
- v0.13 の config.json と4キーの sfz-references.json。
- v0.14 の config.json、2つの preset 表、6キーの sfz-references.json。設定・プリセット・SFZ の3種類すべてをこの版で代表させた。
- v0.15 の config.json。現行に追加されたキーの読み書きも確認する。

合計8 JSON。プリセットと v0.14以降の SFZ は同じ形なので重複させず共通で使う。SFZ の @SFZ_ROOT@ は旧配置の合成時に、検証用の音源フォルダの絶対パスへ展開する。**その後の旧配置から plugin へのコピーは無改変**であることを byte 比較する。

| CTest | 確認内容 |
| --- | --- |
| effetune_json_storage_compatibility_tests | ConfigStore / PresetStore の default path が MIV_SOUND_DATA_DIR/plugin となること、旧配置からコピーした JSON の load/save/reopen と全内容保持。SfzLibrary には config の親の sfz-references.json を渡し、list の public 形式、IDによる音源の bounded read、旧4キーからの保存、6キーの保持、再読込を確認。v0.12 の registry 欠落も確認。原本は変更しない。 |
| effetune_json_storage_ui_compatibility | 生成された現行 UI モジュールで config と controller mapping を load/save、2つの preset 表を読込・保存。short / long の pipeline を適用し、enabled / parameter / routing を照合、export payload の再読込、long→short の上書き保存、effect preset の保存時に enable/routing が混ざらないこと、old columns の新しい表示規則を確認。 |

UI テストの double はファイル API、DOM の入口、パラメータ保持用の最小 plugin。config-store.js、MidiMappingStore、PresetManager、PluginPresetStore / PluginPresetProvider、serialization-utils、PipelineColumnManager は生成物の実コードを使う。native テストは実際の store / SFZ library を使う。いずれも WebView2 を作らず、MIV_SOUND_DATA_DIR の使い捨て領域にだけ書く。node:test は子プロセス起動を必要としない入口で実行する。

## 変更ファイルと結果

- tests/CMakeLists.txt: native と UI の2つの CTest を追加。既存のテスト用 MIV_SOUND_DATA_DIR の割当ループを利用し、各120秒の期限を設定。
- tests/unit/json_storage_compatibility_tests.cpp: native のコピー／読込／再保存の検証。
- tests/esm/json-storage-compatibility.test.mjs: 実 UI serializer の検証。
- tests/fixtures/json-storage/: 8 JSON と README。
- docs/json-storage-compatibility.md: この比較表と結果。

configure と新しい native テストのビルド成功。既存のサンドボックス向けローカル SDK 指定を configure にだけ使い、cache の override は空に戻した。native のコンパイル／リンクは CMake/Ninja の生成した MSVC コマンドを直接実行した。

サンドボックスで **3/3成功**:

- effetune_json_storage_compatibility_tests
- effetune_json_storage_ui_compatibility
- effetune_vst_unit_tests

今回の2テストを加えて全件は **113件**。全件は実行していない。旧 DLL の実行、実 WebView2、IndexedDB / OPFS / localStorage の移行、実際の mIV の移行関数の検証は今回の JSON 調査の範囲に含めていない。これらは設計書の別の F2 受け入れ条件に残る。

ログ: build/miv-json-storage-configure.log、build/miv-json-storage-build-progress.log、build/miv-json-storage-build.log、build/miv-json-storage-tests.log。コミットなし。
