/*
 * kitty_shell_ext.c - Windows 11 modern context menu entry for kitty.
 *
 * Implements IExplorerCommand ("Open kitty here") hosted from a sparse MSIX
 * package (see AppxManifest.xml next to this file). The modern Windows 11
 * context menu only surfaces entries provided this way; classic registry
 * verbs are relegated to "Show more options". IObjectWithSite is implemented
 * so that right-clicks on a folder's background (where the shell passes no
 * item array) can resolve the folder from the hosting view.
 *
 * Plain C COM: hand-rolled vtables, no ATL/WRL. Compiled with mingw gcc, see
 * make-shellext.sh.
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <stdbool.h>

static HMODULE g_module = NULL;
static volatile LONG g_object_count = 0;

/* {B9AC5C9C-51D5-4A50-A806-B7EEDB1D7BDA} - CLSID_KittyExplorerCommand, must match AppxManifest.xml */
static const GUID CLSID_KittyExplorerCommand =
    {0xb9ac5c9c, 0x51d5, 0x4a50, {0xa8, 0x06, 0xb7, 0xee, 0xdb, 0x1d, 0x7b, 0xda}};
static const GUID KIID_IUnknown =
    {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID KIID_IClassFactory =
    {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID KIID_IExplorerCommand =
    {0xa08ce4d0, 0xfa25, 0x44ab, {0xb5, 0x7c, 0xc7, 0xb1, 0xc3, 0x23, 0xe0, 0xb9}};
static const GUID KIID_IObjectWithSite =
    {0xfc4801a3, 0x2ba9, 0x11cf, {0xa2, 0x29, 0x00, 0xaa, 0x00, 0x3d, 0x73, 0x52}};
static const GUID KIID_IServiceProvider =
    {0x6d5140c1, 0x7436, 0x11ce, {0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa}};
static const GUID KSID_SFolderView =
    {0xab8da501, 0xadc5, 0x4a27, {0xb3, 0xd9, 0xb5, 0x48, 0x1f, 0x93, 0x8f, 0xbd}};
static const GUID KIID_IFolderView =
    {0xcde725b0, 0xccc9, 0x4519, {0x91, 0x7e, 0x32, 0x5d, 0x72, 0xfa, 0xb4, 0xce}};
static const GUID KIID_IShellItem =
    {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};

/* ---------------------------------------------------------------- command */

typedef struct {
    IExplorerCommandVtbl *lpVtbl;
    IObjectWithSiteVtbl *lpSiteVtbl;
    LONG ref;
    IUnknown *site;
} KittyCommand;

#define IMPL_FROM_SITE(This) ((KittyCommand*)(((char*)(This)) - offsetof(KittyCommand, lpSiteVtbl)))

static HRESULT dup_str(const WCHAR *s, LPWSTR *out) {
    size_t n = (lstrlenW(s) + 1) * sizeof(WCHAR);
    *out = CoTaskMemAlloc(n);
    if (!*out) return E_OUTOFMEMORY;
    memcpy(*out, s, n);
    return S_OK;
}

/* Path to kitty.exe: the DLL lives at <install>\shellext\kitty_shell_ext.dll
 * and the launcher at <install>\kitty\launcher\kitty.exe */
static bool kitty_exe_path(WCHAR *buf, DWORD sz) {
    if (!GetModuleFileNameW(g_module, buf, sz)) return false;
    WCHAR *p = wcsrchr(buf, L'\\');
    if (!p) return false;
    *p = 0;  /* strip file name -> ...\shellext */
    p = wcsrchr(buf, L'\\');
    if (!p) return false;
    *p = 0;  /* strip shellext -> install root */
    return SUCCEEDED(StringCchCatW(buf, sz, L"\\kitty\\launcher\\kitty.exe"));
}

static HRESULT STDMETHODCALLTYPE cmd_QueryInterface(IExplorerCommand *This, REFIID riid, void **ppv) {
    KittyCommand *self = (KittyCommand*)This;
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &KIID_IUnknown) || IsEqualIID(riid, &KIID_IExplorerCommand)) {
        *ppv = &self->lpVtbl;
    } else if (IsEqualIID(riid, &KIID_IObjectWithSite)) {
        *ppv = &self->lpSiteVtbl;
    } else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&self->ref);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE cmd_AddRef(IExplorerCommand *This) {
    return InterlockedIncrement(&((KittyCommand*)This)->ref);
}

static ULONG STDMETHODCALLTYPE cmd_Release(IExplorerCommand *This) {
    KittyCommand *self = (KittyCommand*)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) {
        if (self->site) IUnknown_Release(self->site);
        HeapFree(GetProcessHeap(), 0, self);
        InterlockedDecrement(&g_object_count);
    }
    return r;
}

static HRESULT STDMETHODCALLTYPE cmd_GetTitle(IExplorerCommand *This, IShellItemArray *items, LPWSTR *name) {
    (void)This; (void)items;
    return dup_str(L"Open in kitty", name);
}

static HRESULT STDMETHODCALLTYPE cmd_GetIcon(IExplorerCommand *This, IShellItemArray *items, LPWSTR *icon) {
    (void)This; (void)items;
    WCHAR buf[MAX_PATH + 8];
    if (!kitty_exe_path(buf, MAX_PATH)) return E_FAIL;
    return dup_str(buf, icon);
}

static HRESULT STDMETHODCALLTYPE cmd_GetToolTip(IExplorerCommand *This, IShellItemArray *items, LPWSTR *tip) {
    (void)This; (void)items; (void)tip;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cmd_GetCanonicalName(IExplorerCommand *This, GUID *guid) {
    (void)This;
    *guid = CLSID_KittyExplorerCommand;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_GetState(IExplorerCommand *This, IShellItemArray *items, BOOL ok_to_be_slow, EXPCMDSTATE *state) {
    (void)This; (void)items; (void)ok_to_be_slow;
    *state = ECS_ENABLED;
    return S_OK;
}

/* Resolve the directory to open: the first selected item if there is one,
 * otherwise the folder of the hosting view (background right-click). */
static bool resolve_directory(KittyCommand *self, IShellItemArray *items, WCHAR *out, DWORD sz) {
    IShellItem *item = NULL;
    if (items && SUCCEEDED(IShellItemArray_GetItemAt(items, 0, &item)) && item) {
        LPWSTR path = NULL;
        HRESULT hr = IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path);
        IShellItem_Release(item);
        if (SUCCEEDED(hr) && path) {
            bool ok = SUCCEEDED(StringCchCopyW(out, sz, path));
            CoTaskMemFree(path);
            return ok;
        }
    }
    if (self->site) {
        IServiceProvider *sp = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface(self->site, &KIID_IServiceProvider, (void**)&sp)) && sp) {
            IFolderView *fv = NULL;
            HRESULT hr = IServiceProvider_QueryService(sp, &KSID_SFolderView, &KIID_IFolderView, (void**)&fv);
            IServiceProvider_Release(sp);
            if (SUCCEEDED(hr) && fv) {
                IShellItem *folder = NULL;
                hr = IFolderView_GetFolder(fv, &KIID_IShellItem, (void**)&folder);
                IFolderView_Release(fv);
                if (SUCCEEDED(hr) && folder) {
                    LPWSTR path = NULL;
                    hr = IShellItem_GetDisplayName(folder, SIGDN_FILESYSPATH, &path);
                    IShellItem_Release(folder);
                    if (SUCCEEDED(hr) && path) {
                        bool ok = SUCCEEDED(StringCchCopyW(out, sz, path));
                        CoTaskMemFree(path);
                        return ok;
                    }
                }
            }
        }
    }
    return false;
}

static HRESULT STDMETHODCALLTYPE cmd_Invoke(IExplorerCommand *This, IShellItemArray *items, IBindCtx *bctx) {
    (void)bctx;
    KittyCommand *self = (KittyCommand*)This;
    WCHAR exe[MAX_PATH + 8];
    if (!kitty_exe_path(exe, MAX_PATH)) return E_FAIL;
    WCHAR dir[MAX_PATH + 8];
    bool have_dir = resolve_directory(self, items, dir, MAX_PATH);

    WCHAR cmdline[2 * MAX_PATH + 64];
    if (have_dir)
        StringCchPrintfW(cmdline, ARRAYSIZE(cmdline), L"\"%s\" --directory \"%s\"", exe, dir);
    else
        StringCchPrintfW(cmdline, ARRAYSIZE(cmdline), L"\"%s\"", exe);

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        NULL, have_dir ? dir : NULL, &si, &pi))
        return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_GetFlags(IExplorerCommand *This, EXPCMDFLAGS *flags) {
    (void)This;
    *flags = ECF_DEFAULT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_EnumSubCommands(IExplorerCommand *This, IEnumExplorerCommand **out) {
    (void)This; (void)out;
    return E_NOTIMPL;
}

static IExplorerCommandVtbl kitty_command_vtbl = {
    cmd_QueryInterface, cmd_AddRef, cmd_Release,
    cmd_GetTitle, cmd_GetIcon, cmd_GetToolTip, cmd_GetCanonicalName,
    cmd_GetState, cmd_Invoke, cmd_GetFlags, cmd_EnumSubCommands,
};

/* ------------------------------------------------------- IObjectWithSite */

static HRESULT STDMETHODCALLTYPE site_QueryInterface(IObjectWithSite *This, REFIID riid, void **ppv) {
    KittyCommand *self = IMPL_FROM_SITE(This);
    return cmd_QueryInterface((IExplorerCommand*)self, riid, ppv);
}

static ULONG STDMETHODCALLTYPE site_AddRef(IObjectWithSite *This) {
    return cmd_AddRef((IExplorerCommand*)IMPL_FROM_SITE(This));
}

static ULONG STDMETHODCALLTYPE site_Release(IObjectWithSite *This) {
    return cmd_Release((IExplorerCommand*)IMPL_FROM_SITE(This));
}

static HRESULT STDMETHODCALLTYPE site_SetSite(IObjectWithSite *This, IUnknown *site) {
    KittyCommand *self = IMPL_FROM_SITE(This);
    if (self->site) IUnknown_Release(self->site);
    self->site = site;
    if (site) IUnknown_AddRef(site);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_GetSite(IObjectWithSite *This, REFIID riid, void **ppv) {
    KittyCommand *self = IMPL_FROM_SITE(This);
    if (!self->site) { *ppv = NULL; return E_FAIL; }
    return IUnknown_QueryInterface(self->site, riid, ppv);
}

static IObjectWithSiteVtbl kitty_site_vtbl = {
    site_QueryInterface, site_AddRef, site_Release, site_SetSite, site_GetSite,
};

/* --------------------------------------------------------- class factory */

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IClassFactory *This, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &KIID_IUnknown) || IsEqualIID(riid, &KIID_IClassFactory)) {
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE factory_AddRef(IClassFactory *This) { (void)This; return 2; }
static ULONG STDMETHODCALLTYPE factory_Release(IClassFactory *This) { (void)This; return 1; }

static HRESULT STDMETHODCALLTYPE factory_CreateInstance(IClassFactory *This, IUnknown *outer, REFIID riid, void **ppv) {
    (void)This;
    if (outer) return CLASS_E_NOAGGREGATION;
    KittyCommand *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KittyCommand));
    if (!c) return E_OUTOFMEMORY;
    c->lpVtbl = &kitty_command_vtbl;
    c->lpSiteVtbl = &kitty_site_vtbl;
    c->ref = 1;
    InterlockedIncrement(&g_object_count);
    HRESULT hr = cmd_QueryInterface((IExplorerCommand*)c, riid, ppv);
    cmd_Release((IExplorerCommand*)c);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_LockServer(IClassFactory *This, BOOL lock) {
    (void)This;
    if (lock) InterlockedIncrement(&g_object_count);
    else InterlockedDecrement(&g_object_count);
    return S_OK;
}

static IClassFactoryVtbl kitty_factory_vtbl = {
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_CreateInstance, factory_LockServer,
};
static IClassFactory kitty_factory = { &kitty_factory_vtbl };

/* ---------------------------------------------------------- dll exports */

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv) {
    if (IsEqualCLSID(clsid, &CLSID_KittyExplorerCommand))
        return factory_QueryInterface(&kitty_factory, riid, ppv);
    *ppv = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) {
    return g_object_count == 0 ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
