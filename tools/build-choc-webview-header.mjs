import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  source: { type: 'string' },
  out: { type: 'string' },
} });
if (!values.source || !values.out) {
  throw new Error('Expected --source and --out for the CHOC WebView header');
}

// Keep the pinned CHOC checkout intact. Its Windows implementation discards
// synchronous and asynchronous creation errors; expose those to the native UI.
let header = (await readFile(values.source, 'utf8')).replace(/\r\n/g, '\n');
function replaceOnce(before, after) {
  if (header.split(before).length !== 2) {
    throw new Error('The pinned CHOC WebView initialisation code has changed');
  }
  header = header.replace(before, after);
}

// One counter for all WebViews in this image, shared by every translation unit.
// DLL hosts read their image's counter through the read-only plugin export.
replaceOnce(`#endif // __webview2_h__

namespace choc::ui
{

//==============================================================================
struct WebView::Pimpl`, `#endif // __webview2_h__

namespace choc::ui
{

inline std::atomic<uint64_t> mivSoundPendingWebViewCallbacks { 0 };

inline uint64_t mivSoundTestPendingWebViewCallbacks() noexcept
{
    return mivSoundPendingWebViewCallbacks.load();
}

// Kept independent of WebView construction so COM argument doubles can test it.
inline HRESULT mivSoundHandlePermissionRequest (ICoreWebView2PermissionRequestedEventArgs* args,
                                               const std::function<bool(int)>& shouldDeny)
{
    if (args == nullptr) return S_OK;
    auto kind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
    if (args->get_PermissionKind (std::addressof (kind)) != S_OK) return S_OK;
    if (shouldDeny && shouldDeny (static_cast<int> (kind)))
        return args->put_State (COREWEBVIEW2_PERMISSION_STATE_DENY);
    if (kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ)
        args->put_State (COREWEBVIEW2_PERMISSION_STATE_ALLOW);
    return S_OK;
}

// A completion remains outstanding until Invoke RETURNS, or its handler dies.
// Environment and controller share an EventHandler, so use separate tokens.
struct MivSoundPendingWebViewCallback
{
    explicit MivSoundPendingWebViewCallback (bool active = true) { if (active) arm(); }
    ~MivSoundPendingWebViewCallback() { finish(); }
    void arm()
    {
        if (! pending.exchange (true)) ++mivSoundPendingWebViewCallbacks;
    }
    void finish()
    {
        if (pending.exchange (false)) --mivSoundPendingWebViewCallbacks;
    }
    struct Invocation
    {
        MivSoundPendingWebViewCallback& token;
        ~Invocation() { token.finish(); }
    };
    std::atomic<bool> pending { false };
};

//==============================================================================
struct WebView::Pimpl`);

replaceOnce('        virtual ~EventHandler() {}', `        virtual ~EventHandler() {}
        MivSoundPendingWebViewCallback environmentCompletion;
        MivSoundPendingWebViewCallback controllerCompletion { false };`);

replaceOnce('        std::function<void(choc::ui::WebView&)> webviewIsReady;',
  `        std::function<void(choc::ui::WebView&)> webviewIsReady;

        /// Windows only: explicit WebView2 storage, preferred over the host default.
        std::wstring userDataFolder;

        /// Windows only: reports a failed environment or controller creation.
        /// May run synchronously during construction or later on the owner STA.
        std::function<void(const std::string&)> webviewInitialisationError;

        /// Windows only: return true to deny a base WebView2 permission kind.
        /// False preserves CHOC's existing policy (including clipboard read).
        std::function<bool(int)> shouldDenyPermission;`);

replaceOnce('    COMPtr<EventHandler> eventHandler;', `    COMPtr<EventHandler> eventHandler;
    EventRegistrationToken permissionToken {};
    bool permissionRegistered = false;`);
replaceOnce('            view->add_PermissionRequested (this, std::addressof (token));',
  `            result = view->add_PermissionRequested (this, std::addressof (ownerPimpl.permissionToken));
            if (FAILED (result))
            {
                ownerPimpl.reportInitialisationError (result);
                view->Release();
                controller->Close();
                return result;
            }
            ownerPimpl.permissionRegistered = true;`);
replaceOnce(`            if (args == nullptr)
                return S_OK;

            auto permissionKind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;

            if (args->get_PermissionKind (std::addressof (permissionKind)) != S_OK)
                return S_OK;

            if (permissionKind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ)
                args->put_State (COREWEBVIEW2_PERMISSION_STATE_ALLOW);

            return S_OK;`, `            if (deletionCheckerRef->deleted) return E_FAIL;
            return mivSoundHandlePermissionRequest (args, ownerPimpl.options.shouldDenyPermission);`);

replaceOnce('if (auto userDataFolder = getUserDataFolder(); ! userDataFolder.empty())',
  'if (auto userDataFolder = options.userDataFolder.empty() ? getUserDataFolder() : options.userDataFolder; ! userDataFolder.empty())');

// This is the Windows Pimpl only. WebViewHost marshals destruction to its
// owner STA; Close must run there before releasing the controller/COM objects.
replaceOnce(`        deletionChecker->deleted = true;

        eventHandler.reset();
        coreWebView.reset();
        coreWebViewController.reset();
        coreWebViewEnvironment.reset();
        hwnd.reset();`,
  `        deletionChecker->deleted = true;

        if (coreWebView && permissionRegistered)
        {
            coreWebView->remove_PermissionRequested (permissionToken);
            permissionRegistered = false;
        }
        if (coreWebViewController)
            coreWebViewController->Close();

        eventHandler.reset();
        coreWebView.reset();
        coreWebViewController.reset();
        coreWebViewEnvironment.reset();
        hwnd.reset();`);

replaceOnce(`                        createCoreWebView2EnvironmentWithOptions (nullptr, userDataFolder.c_str(), nullptr, eventHandler);
                        return true;`,
  `                        const auto result = createCoreWebView2EnvironmentWithOptions (nullptr, userDataFolder.c_str(), nullptr, eventHandler);
                        if (FAILED (result))
                        {
                            reportInitialisationError (result);
                            return false;
                        }
                        return true;`);

replaceOnce('    bool stillInitialising() const  { return ! coreWebView; }',
  `    void reportInitialisationError (HRESULT error)
    {
        if (options.webviewInitialisationError)
            options.webviewInitialisationError (getMessageFromHRESULT (error));
    }

    bool stillInitialising() const  { return ! coreWebView; }`);

replaceOnce(`        HRESULT STDMETHODCALLTYPE Invoke (HRESULT, ICoreWebView2Environment* env) override
        {
            if (env == nullptr || deletionCheckerRef->deleted)
                return E_FAIL;

            if (! ownerPimpl.environmentCreationComplete (env))
                return E_FAIL;

            env->CreateCoreWebView2Controller (ownerPimpl.hwnd, this);
            return S_OK;
        }`,
  `        HRESULT STDMETHODCALLTYPE Invoke (HRESULT result, ICoreWebView2Environment* env) override
        {
            const MivSoundPendingWebViewCallback::Invocation completion { environmentCompletion };
            if (deletionCheckerRef->deleted)
                return E_FAIL;

            if (FAILED (result) || env == nullptr)
            {
                ownerPimpl.reportInitialisationError (FAILED (result) ? result : E_POINTER);
                return E_FAIL;
            }

            if (! ownerPimpl.environmentCreationComplete (env))
            {
                ownerPimpl.reportInitialisationError (E_UNEXPECTED);
                return E_FAIL;
            }

            controllerCompletion.arm();
            result = env->CreateCoreWebView2Controller (ownerPimpl.hwnd, this);
            if (FAILED (result))
                ownerPimpl.reportInitialisationError (result);
            return result;
        }`);

replaceOnce(`        HRESULT STDMETHODCALLTYPE Invoke (HRESULT, ICoreWebView2Controller* controller) override
        {
            if (controller == nullptr || deletionCheckerRef->deleted)
                return E_FAIL;

            ICoreWebView2* view = {};
            controller->get_CoreWebView2 (std::addressof (view));

            if (view == nullptr)
                return E_FAIL;`,
  `        HRESULT STDMETHODCALLTYPE Invoke (HRESULT result, ICoreWebView2Controller* controller) override
        {
            const MivSoundPendingWebViewCallback::Invocation completion { controllerCompletion };
            if (deletionCheckerRef->deleted)
            {
                // WebView2 completes on the creating STA. The owner no longer
                // exists, so close this late controller without touching it.
                if (controller != nullptr)
                    controller->Close();
                return E_FAIL;
            }

            if (FAILED (result) || controller == nullptr)
            {
                ownerPimpl.reportInitialisationError (FAILED (result) ? result : E_POINTER);
                return E_FAIL;
            }

            ICoreWebView2* view = {};
            result = controller->get_CoreWebView2 (std::addressof (view));

            if (FAILED (result) || view == nullptr)
            {
                ownerPimpl.reportInitialisationError (FAILED (result) ? result : E_POINTER);
                return E_FAIL;
            }`);

replaceOnce('        virtual ~ExecuteScriptCompletedCallback() {}',
  `        virtual ~ExecuteScriptCompletedCallback() {}
        MivSoundPendingWebViewCallback scriptCompletion;`);
replaceOnce(`        HRESULT STDMETHODCALLTYPE Invoke (HRESULT hr, LPCWSTR resultJSON) override
        {`, `        HRESULT STDMETHODCALLTYPE Invoke (HRESULT hr, LPCWSTR resultJSON) override
        {
            const MivSoundPendingWebViewCallback::Invocation completion { scriptCompletion };`);

// Validate the generated accounting contract as well as each source anchor.
for (const [snippet, count] of [
  ['inline std::atomic<uint64_t> mivSoundPendingWebViewCallbacks', 1],
  ['const MivSoundPendingWebViewCallback::Invocation completion {', 3],
  ['MivSoundPendingWebViewCallback environmentCompletion;', 1],
  ['MivSoundPendingWebViewCallback controllerCompletion { false };', 1],
  ['MivSoundPendingWebViewCallback scriptCompletion;', 1],
  ['controllerCompletion.arm();', 1],
]) {
  if (header.split(snippet).length !== count + 1)
    throw new Error(`Generated CHOC callback accounting contract failed: ${snippet}`);
}

// Quoted includes must still resolve against the original header's directory.
header = header.replace(/^(\s*#include\s+")([^"]+)(")/gm,
  (_match, prefix, include, suffix) =>
    prefix + path.resolve(path.dirname(values.source), include).replaceAll('\\', '/') + suffix);
const output = '// Generated by tools/build-choc-webview-header.mjs.\n' + header;
let previous;
try {
  previous = await readFile(values.out, 'utf8');
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
if (previous !== output) {
  await mkdir(path.dirname(values.out), { recursive: true });
  await writeFile(values.out, output);
}
