import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import test from 'node:test';

test('CHOC callback accounting generation rejects missing and duplicate source anchors', async () => {
  const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
  const source = readFileSync(path.join(repository, 'third_party/choc/choc/gui/choc_WebView.h'), 'utf8')
    .replaceAll('\r\n', '\n');
  const dataRoot = process.env.MIV_SOUND_DATA_DIR;
  assert.ok(dataRoot && path.isAbsolute(dataRoot), 'CTest provides isolated temporary storage');
  mkdirSync(dataRoot, { recursive: true });
  const temporary = mkdtempSync(path.join(dataRoot, 'header-contract-'));
  try {
    const input = path.join(temporary, 'choc_WebView.h');
    const output = path.join(temporary, 'generated.h');
    let generation = 0;
    const generate = async text => {
      writeFileSync(input, text);
      rmSync(output, { force: true });
      const generator = path.join(repository, 'tools/build-choc-webview-header.mjs');
      const previousArguments = process.argv;
      process.argv = [process.execPath, generator, '--source', input, '--out', output];
      try { await import(`${pathToFileURL(generator).href}?contract=${++generation}`); }
      finally { process.argv = previousArguments; }
    };
    await generate(source);
    const generated = readFileSync(output, 'utf8');
    assert.equal(generated.split('inline std::atomic<uint64_t> mivSoundPendingWebViewCallbacks').length - 1, 1);
    assert.equal(generated.split('const MivSoundPendingWebViewCallback::Invocation completion {').length - 1, 3);
    assert.match(generated, /controllerCompletion\.arm\(\);\s+result = env->CreateCoreWebView2Controller/);
    assert.match(generated, /mivSoundHandlePermissionRequest \(args, ownerPimpl\.options\.shouldDenyPermission\)/);
    assert.match(generated, /add_PermissionRequested \(this, std::addressof \(ownerPimpl\.permissionToken\)\)/);
    const unregister = generated.indexOf('coreWebView->remove_PermissionRequested (permissionToken)');
    assert.ok(unregister > 0 && unregister < generated.indexOf('coreWebViewController->Close();'));
    for (const anchor of [
      '#endif // __webview2_h__',
      '        virtual ~EventHandler() {}',
      '            view->add_PermissionRequested (this, std::addressof (token));',
      '    COMPtr<EventHandler> eventHandler;',
      '        virtual ~ExecuteScriptCompletedCallback() {}',
      '        HRESULT STDMETHODCALLTYPE Invoke (HRESULT, ICoreWebView2Environment* env) override',
      '        HRESULT STDMETHODCALLTYPE Invoke (HRESULT, ICoreWebView2Controller* controller) override',
      '        HRESULT STDMETHODCALLTYPE Invoke (HRESULT hr, LPCWSTR resultJSON) override',
    ]) {
      assert.equal(source.split(anchor).length - 1, 1, anchor);
      for (const mutated of [source.replace(anchor, '// missing anchor'), source + '\n' + source]) {
        await assert.rejects(generate(mutated), /pinned CHOC WebView initialisation code has changed/,
          `must reject changed source anchor: ${anchor}`);
        assert.equal(existsSync(output), false, 'a rejected generator must not produce an output');
      }
    }
  } finally {
    assert.equal(path.dirname(temporary), path.resolve(dataRoot));
    rmSync(temporary, { recursive: true, force: true });
  }
});
