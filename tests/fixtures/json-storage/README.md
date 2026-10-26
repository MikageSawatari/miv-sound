# JSON 保存形式の合成 fixture

設計書第8版 §5.5 の JSON ファイル互換性確認用。値はすべて合成で、利用者のデータは使用していない。

| フォルダ | fixture | 根拠 |
| --- | --- | --- |
| v0.12.0 | config.json | v0.12 の共通設定。native SFZ 参照はまだない。 |
| v0.13.0 | config.json、sfz-references.json | frequencyPreviewSound / sfzMaxSizeMiB と、4キーの native SFZ 参照。 |
| v0.14.0 | config.json、effetune_presets.json、effetune_plugin_presets.json、sfz-references.json | 出荷済み mIV の代表。onboarding、controller mapping、short / imported-long プリセット、bankPaths / importedAt。 |
| v0.15.0 | config.json | 現行形の自己往復確認。pipelineColumnMode と動画関連の追加キー。 |

プリセット2表の serializer は4版で同じなので、v0.14 の2ファイルを共通で使う。SFZ は v0.14 と現行が同じ形式で、v0.14 の fixture を共通で使う。

SFZ の @SFZ_ROOT@ は、検証用フォルダに作る音源の絶対パスへ、**旧配置を合成する段階で**展開するための値のテンプレート。JSON のキーや型は変えない。その後の legacy/Frieve/EffeTunePlugin → MIV_SOUND_DATA_DIR/plugin のコピーは byte 単位で一致することを検査する。参照先の SFZ と .bank.xml も検証用フォルダだけに作る。

native テストは実際の ConfigStore / PresetStore / SfzLibrary を使う。UI テストは生成された config-store、MidiMappingStore、PresetManager、PluginPresetStore / Provider、serialization-utils を使い、ファイル API とパラメータを保持する plugin だけを double にする。WebView2 は作らない。

タグと submodule の commit、比較表、再保存による変化は [比較報告](../../../docs/json-storage-compatibility.md) を参照。
