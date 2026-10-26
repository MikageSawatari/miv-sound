import assert from 'node:assert/strict';
import { copyFile, mkdir, readFile, writeFile, access } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import test from 'node:test';

const argument = name => {
  const index = process.argv.indexOf(name);
  assert.ok(index >= 0 && process.argv[index + 1], `Pass ${name}`);
  return path.resolve(process.argv[index + 1]);
};
const assets = argument('--assets');
const fixtures = argument('--fixtures');
const root = process.env.MIV_SOUND_DATA_DIR;
assert.ok(root && path.isAbsolute(root) && path.basename(root) === 'effetune_json_storage_ui_compatibility',
  'Use only this CTest disposable data folder');
const pluginDirectory = path.join(root, 'plugin');
const assetModule = relative => import(pathToFileURL(path.join(assets, relative)).href);
const json = async file => JSON.parse(await readFile(file, 'utf8'));

async function stage(version, name) {
  const legacy = path.join(root, 'legacy', version, 'Frieve', 'EffeTunePlugin', name);
  const fixtureVersion = name === 'config.json' ? version : 'v0.14.0';
  await mkdir(path.dirname(legacy), { recursive: true });
  await mkdir(pluginDirectory, { recursive: true });
  await copyFile(path.join(fixtures, fixtureVersion, name), legacy);
  const destination = path.join(pluginDirectory, name);
  await copyFile(legacy, destination);
  assert.equal(await readFile(destination, 'utf8'), await readFile(legacy, 'utf8'), 'copy leaves JSON unchanged');
  return { legacy, destination };
}

async function withStorage(body) {
  const previous = { window: globalThis.window, document: globalThis.document };
  // Only the native API boundary is a double; the serializers below are the
  // actual generated mIV Sound modules. This never opens a browser or APPDATA.
  const admit = file => {
    assert.equal(path.dirname(path.resolve(file)), path.resolve(pluginDirectory));
    return file;
  };
  globalThis.window = {
    localStorage: { setItem() {} },
    electronIntegration: { isElectron: true },
    electronAPI: {
      getPath: async () => pluginDirectory,
      joinPaths: async (...parts) => path.join(...parts),
      fileExists: async file => { try { await access(admit(file)); return true; } catch { return false; } },
      readFile: async file => ({ success: true, content: await readFile(admit(file), 'utf8') }),
      saveFile: async (file, content) => { await writeFile(admit(file), content); return { success: true }; },
      loadConfig: async () => ({ success: true, config: await json(path.join(pluginDirectory, 'config.json')) }),
      saveConfig: async config => {
        await writeFile(path.join(pluginDirectory, 'config.json'), JSON.stringify(config, null, 2));
        return { success: true };
      }
    }
  };
  globalThis.document = { getElementById: () => null, querySelector: () => null };
  try { await body(); }
  finally {
    globalThis.window = previous.window;
    globalThis.document = previous.document;
  }
}

test('copied v0.12-v0.15 settings retain keys through the real UI config writer', async () => {
  const { loadConfig, saveConfig } = await assetModule('js/electron/config-store.js');
  const { normalizeOfflineOutputSettings } = await assetModule('js/audio/offline-output-settings.js');
  for (const version of ['v0.12.0', 'v0.13.0', 'v0.14.0', 'v0.15.0']) {
    const { legacy, destination } = await stage(version, 'config.json');
    const original = await json(legacy);
    await withStorage(async () => {
      const loaded = await loadConfig(true);
      assert.deepEqual(loaded, { ...original, offlineOutput: normalizeOfflineOutputSettings(original.offlineOutput) });
      assert.equal(await saveConfig(true, { language: original.language }), true);
      assert.deepEqual(await json(destination), loaded, 'unrelated saves keep desktop-only and optional legacy keys');
      assert.deepEqual(await loadConfig(true), loaded, 'reload accepts the saved settings');
    });
    assert.deepEqual(await json(legacy), original, 'the old configuration is never rewritten');
  }
});

// This small plugin double holds parameter values; serialization and preset
// application are performed by the real PresetManager and serialization-utils.
function createPlugin(name) {
  return {
    name, enabled: true, inputBus: null, outputBus: null, channel: null, parameters: {},
    setEnabled(value) { this.enabled = value; },
    setParameters(values) {
      const { enabled, type, nm, en, id, ib, ob, ch, ...parameters } = values;
      if (enabled !== undefined) this.enabled = enabled;
      Object.assign(this.parameters, parameters);
    },
    setSerializedParameters(values) {
      const { nm, en, id, ib, ob, ch, ...parameters } = values;
      if (en !== undefined) this.enabled = en;
      this.setParameters(parameters);
    },
    getSerializableParameters() { return { ...structuredClone(this.parameters), enabled: this.enabled }; },
    updateParameters() {}, cleanup() {}
  };
}

async function pipelineManager() {
  const { PresetManager } = await assetModule('js/ui/pipeline/preset-manager.js');
  const { getSerializablePluginStateShort: short, getSerializablePluginStateLong: long } =
    await assetModule('js/utils/serialization-utils.js');
  const audioManager = {
    pipeline: [], pipelineA: [], pipelineB: [], currentPipeline: 'A', masterBypass: false,
    updateCurrentPipeline(plugins) { this.pipeline = plugins; this.pipelineA = plugins; },
    setMasterBypass(value) { this.masterBypass = value; }
  };
  const manager = {
    audioManager, expandedPlugins: new Set(),
    pluginManager: { createPlugin, isPluginAvailable: name => ['Volume', 'DC Offset'].includes(name) },
    historyManager: { withHistorySuppressed: callback => callback(), saveState() {} },
    core: {
      audioManager,
      getSerializablePluginState: (plugin, useShort, deepCopy) => useShort ? short(plugin) : long(plugin, deepCopy),
      updatePipelineUI() {}, updateWorkletPlugins() {}, updateWorkletPlugin() {}, updateAllPluginDisplayState() {}
    }
  };
  manager.core.pipelineManager = manager;
  return { manager, presets: new PresetManager(manager), snapshot: () => audioManager.pipeline.map(plugin => long(plugin)) };
}

test('copied v0.14 pipeline presets load, save and export in the shared old/new forms', async () => {
  const { legacy, destination } = await stage('v0.14.0', 'effetune_presets.json');
  const original = await json(legacy);
  await withStorage(async () => {
    const { presets, snapshot } = await pipelineManager();
    assert.deepEqual(await presets.getPresets(), original);
    await presets.persistPresets(await presets.getPresets());
    assert.deepEqual(await json(destination), original, 'whole-table saves preserve the long form and its metadata');
    assert.equal(await presets.loadPreset('Short gain'), true);
    assert.deepEqual(snapshot(), [
      { name: 'Volume', enabled: true, parameters: { vl: -6 }, inputBus: 0, outputBus: 1, channel: 'L' },
      { name: 'DC Offset', enabled: false, parameters: { of: 0.125 } }
    ]);
    assert.equal(await presets.savePreset('Short gain', { showSuccessMessage: false }), true);
    assert.deepEqual(await json(destination), original, 'saving the short preset preserves all parameters and routing');

    assert.equal(await presets.loadPreset('Imported long'), true);
    const before = snapshot();
    assert.deepEqual(before, original['Imported long'].pipeline);
    const exported = presets.getCurrentPresetData();
    assert.equal(exported.name, 'Imported long');
    assert.deepEqual(exported.pipeline, before);
    assert.ok(Number.isFinite(exported.timestamp));
    assert.equal(await presets.loadPreset(exported), true, 'reimport the .effetune_preset JSON payload');
    assert.deepEqual(snapshot(), before);
    assert.equal(await presets.savePreset('Imported long', { showSuccessMessage: false }), true);
    const saved = await json(destination);
    assert.deepEqual(saved['Imported long'], { plugins: [{ vl: -3, nm: 'Volume', en: false, ib: 1, ob: 0, ch: 'R' }] },
      'saving an imported long preset replaces its wrapper with the long-supported short form');
    assert.equal(await presets.loadPreset('Imported long'), true);
    assert.deepEqual(snapshot(), before, 'long -> short -> load retains parameter, enable and routing semantics');
    assert.deepEqual(saved['Short gain'], original['Short gain']);
  });
  assert.deepEqual(await json(legacy), original);
});

test('copied v0.14 per-effect presets retain other groups and exclude enable/routing on save', async () => {
  const { legacy, destination } = await stage('v0.14.0', 'effetune_plugin_presets.json');
  const original = await json(legacy);
  await withStorage(async () => {
    const { PluginPresetStore } = await assetModule('js/ui/pipeline/plugin-preset-store.js');
    const { PluginPresetProvider } = await assetModule('js/ui/pipeline/plugin-preset-dialog.js');
    const { manager } = await pipelineManager();
    const plugin = createPlugin('Volume');
    Object.assign(plugin, { enabled: false, inputBus: 1, outputBus: 0, channel: 'R' });
    const parameters = plugin.getSerializableParameters.bind(plugin);
    plugin.getSerializableParameters = () => ({ ...parameters(), ib: plugin.inputBus, ob: plugin.outputBus, ch: plugin.channel });
    manager.audioManager.updateCurrentPipeline([plugin]);
    const store = new PluginPresetStore();
    const provider = new PluginPresetProvider(manager.core, plugin, store);
    assert.deepEqual(await store.readPresets(), original);
    assert.equal(await provider.applyUserPreset('Quiet'), true);
    assert.equal(plugin.parameters.vl, -6);
    assert.equal(await provider.saveUserPreset('Quiet'), true);
    assert.deepEqual(await json(destination), original);
    assert.deepEqual([plugin.enabled, plugin.inputBus, plugin.outputBus, plugin.channel], [false, 1, 0, 'R']);
    assert.deepEqual(await new PluginPresetStore().getForPlugin('Volume'), original.Volume);
  });
  assert.deepEqual(await json(legacy), original);
});

test('old column counts remain stored but v0.15 defaults to automatic display mode', async () => {
  await stage('v0.14.0', 'config.json');
  await withStorage(async () => {
    const { loadConfig, saveConfig } = await assetModule('js/electron/config-store.js');
    const { PipelineColumnManager } = await assetModule('js/ui/pipeline/pipeline-column-manager.js');
    window.appConfig = await loadConfig(true);
    const columns = Object.create(PipelineColumnManager.prototype);
    columns.getAvailablePipelineWidth = () => 1064;
    columns.getColumnGap = () => 10;
    assert.equal(window.appConfig.columns, 4);
    assert.equal(columns.isAutomatic(), true, 'the new missing-key default changes display behaviour');
    assert.equal(columns.getEffectiveColumnCount(4), 1);
    assert.equal(await saveConfig(true, { pipelineColumnMode: 'manual' }), true);
    assert.equal(columns.getEffectiveColumnCount(4), 4, 'manual mode restores the existing column preference');
    assert.equal((await json(path.join(pluginDirectory, 'config.json'))).columns, 4);
  });
});

test('v0.14 controller mappings survive validation and the real config persistence path', async () => {
  const { legacy, destination } = await stage('v0.14.0', 'config.json');
  const original = await json(legacy);
  await withStorage(async () => {
    const { MidiMappingStore } = await assetModule('js/midi/midi-mapping-store.js');
    const mappings = new MidiMappingStore({ isElectron: true });
    assert.deepEqual(await mappings.initialize(), original.midiController);
    assert.equal(await mappings.persist(), true);
    assert.deepEqual(await json(destination), original, 'controller saves retain all unrelated config keys');
  });
  assert.deepEqual(await json(legacy), original);
});
