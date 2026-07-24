//========================================================================
// Windows 11 acrylic, as a Windows.UI.Composition effect graph.
//
// Why the window is built this way
// --------------------------------
// A DesktopWindowTarget always draws above the window's redirection surface,
// so an acrylic sprite attached to an ordinary OpenGL window would cover the
// terminal. The way out is the one Windows Terminal takes: give the window no
// redirection surface at all (WS_EX_NOREDIRECTIONBITMAP) and put every pixel
// inside the composition tree.
//
//   DesktopWindowTarget
//     ContainerVisual
//       SpriteVisual  acrylic effect brush     <- bottom
//       SpriteVisual  swapchain surface brush  <- top, the OpenGL output
//
// OpenGL renders into the swapchain back buffer through WGL_NV_DX_interop2.
// The back buffer is bound as an FBO once and never unbound, so kitty's
// renderer, which only ever draws to framebuffer 0, needs no changes.
//
// The acrylic layer is the background. kitty must not paint the default
// background as well, or the tint lands twice and the window reads as solid.
// platform_bg_alpha() in state.c drops kitty's own alpha to zero for that
// reason.
//
// The effect graph is the 19H1 AcrylicBrush recipe from microsoft-ui-xaml,
// reproduced through IGraphicsEffectD2D1Interop:
//
//   Composite(SourceOver)
//     dest: Blend(23)                  tint, flipped per the WinUI enum bug
//             bg: Blend(22)            luminosity, same flip
//                   bg: host backdrop  pre-blurred by the shell
//                   fg: Flood(luminosity colour)
//             fg: Flood(tint colour)
//     src:  Opacity(0.02) -> Border(Wrap) -> 256x256 noise
//
// Requires WGL_NV_DX_interop2, which AMD and NVIDIA ship and Intel does not
// reliably. _glfwWin32AcrylicCreate fails cleanly when it is missing and the
// caller falls back to a plain opaque window.
//========================================================================

#define COBJMACROS

// Windows.UI.Composition and the D2D effect property enums this file uses are
// Windows 10 RS1 and later. Has to precede every Windows header.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000002  // NTDDI_WIN10_RS1
#endif

#include "internal.h"

#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <windows.foundation.h>
#include <windows.ui.h>
#include <windows.ui.composition.h>
#include <windows.graphics.effects.h>

// d2d1 has no C bindings, but its enums and effect property indices do compile
// as C, and that is all this file needs from it. Nothing here calls into D2D
// and nothing links against d2d1.
#include <d2d1_1.h>
#include <d2d1effects_2.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "win32_acrylic.h"
#include "win32_acrylic_noise.h"

// AcrylicBrush.h. There is no blur radius: the host backdrop brush arrives
// pre-blurred by the shell, and AcrylicBrush likewise skips its own Gaussian
// on that path.
static const float sc_noiseOpacity = 0.02f;

// Set KITTY_ACRYLIC_DEBUG=1 to trace which step gives up. Every failure here
// is silent by design, since the window still works without the material.
static bool acrylicDebug(void) {
    static int cached = -1;
    if (cached < 0) { const char* v = getenv("KITTY_ACRYLIC_DEBUG"); cached = v && *v && *v != '0'; }
    return cached != 0;
}
#define ADBG(...) do { if (acrylicDebug()) { fprintf(stderr, "acrylic: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } } while (0)

#define CHECK(expr) do { \
    HRESULT _hr = (expr); \
    if (FAILED(_hr)) { ADBG("%s failed: 0x%08x", #expr, (unsigned)_hr); return _hr; } \
} while (0)



typedef __x_ABI_CWindows_CUI_CComposition_CICompositor Compositor;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionBrush CompositionBrush;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionEffectFactory CompositionEffectFactory;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionEffectBrush CompositionEffectBrush;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionGraphicsDevice CompositionGraphicsDevice;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionDrawingSurface CompositionDrawingSurface;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionSurface CompositionSurface;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionSurfaceBrush CompositionSurfaceBrush;
typedef __x_ABI_CWindows_CUI_CComposition_CICompositionTarget CompositionTarget;
typedef __x_ABI_CWindows_CUI_CComposition_CIContainerVisual ContainerVisual;
typedef __x_ABI_CWindows_CUI_CComposition_CIVisualCollection VisualCollection;
typedef __x_ABI_CWindows_CUI_CComposition_CIVisual Visual;
typedef __x_ABI_CWindows_CUI_CComposition_CISpriteVisual SpriteVisual;
typedef __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffect GraphicsEffect;
typedef __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffectSource GraphicsEffectSource;
typedef __x_ABI_CWindows_CFoundation_CIPropertyValue PropertyValue;
typedef __x_ABI_CWindows_CFoundation_CIPropertyValueStatics PropertyValueStatics;

typedef struct __x_ABI_CWindows_CFoundation_CNumerics_CVector2 Vector2;
typedef struct __x_ABI_CWindows_CFoundation_CNumerics_CVector3 Vector3;
typedef struct __x_ABI_CWindows_CFoundation_CSize SizeF;
typedef struct __x_ABI_CWindows_CUI_CColor UIColor;

/* mingw's windows.ui.composition.h carries 43 of the SDK's 184 interfaces, and
 * these five are not among them. Declared here so one source builds under both
 * mingw and MSVC. Slot order and IIDs match the Windows SDK header. */
#define DECL_INSPECTABLE_HEAD(iface)                                           \
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(iface *, REFIID, void **);        \
  ULONG(STDMETHODCALLTYPE *AddRef)(iface *);                                   \
  ULONG(STDMETHODCALLTYPE *Release)(iface *);                                  \
  HRESULT(STDMETHODCALLTYPE *GetIids)(iface *, ULONG *, IID **);               \
  HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(iface *, HSTRING *);         \
  HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(iface *, TrustLevel *)

typedef struct CompositionBackdropBrush CompositionBackdropBrush;
typedef struct CompositionBackdropBrushVtbl {
  DECL_INSPECTABLE_HEAD(CompositionBackdropBrush);
} CompositionBackdropBrushVtbl;
struct CompositionBackdropBrush { const CompositionBackdropBrushVtbl *lpVtbl; };

typedef struct Compositor3 Compositor3;
typedef struct Compositor3Vtbl {
  DECL_INSPECTABLE_HEAD(Compositor3);
  HRESULT(STDMETHODCALLTYPE *CreateHostBackdropBrush)(Compositor3 *, CompositionBackdropBrush **);
} Compositor3Vtbl;
struct Compositor3 { const Compositor3Vtbl *lpVtbl; };

typedef struct EffectSourceParameter EffectSourceParameter;
typedef struct EffectSourceParameterVtbl {
  DECL_INSPECTABLE_HEAD(EffectSourceParameter);
  HRESULT(STDMETHODCALLTYPE *get_Name)(EffectSourceParameter *, HSTRING *);
} EffectSourceParameterVtbl;
struct EffectSourceParameter { const EffectSourceParameterVtbl *lpVtbl; };

typedef struct EffectSourceParameterFactory EffectSourceParameterFactory;
typedef struct EffectSourceParameterFactoryVtbl {
  DECL_INSPECTABLE_HEAD(EffectSourceParameterFactory);
  HRESULT(STDMETHODCALLTYPE *Create)(EffectSourceParameterFactory *, HSTRING, EffectSourceParameter **);
} EffectSourceParameterFactoryVtbl;
struct EffectSourceParameterFactory { const EffectSourceParameterFactoryVtbl *lpVtbl; };

typedef struct Visual2 Visual2;
typedef struct Visual2Vtbl {
  DECL_INSPECTABLE_HEAD(Visual2);
  HRESULT(STDMETHODCALLTYPE *get_ParentForTransform)(Visual2 *, Visual **);
  HRESULT(STDMETHODCALLTYPE *put_ParentForTransform)(Visual2 *, Visual *);
  HRESULT(STDMETHODCALLTYPE *get_RelativeOffsetAdjustment)(Visual2 *, Vector3 *);
  HRESULT(STDMETHODCALLTYPE *put_RelativeOffsetAdjustment)(Visual2 *, Vector3);
  HRESULT(STDMETHODCALLTYPE *get_RelativeSizeAdjustment)(Visual2 *, Vector2 *);
  HRESULT(STDMETHODCALLTYPE *put_RelativeSizeAdjustment)(Visual2 *, Vector2);
} Visual2Vtbl;
struct Visual2 { const Visual2Vtbl *lpVtbl; };

#define PIXELFORMAT_B8G8R8A8_UINTNORMALIZED 87
#define ALPHAMODE_PREMULTIPLIED 1
#define COMPOSITIONSTRETCH_NONE 0

static const GUID kIID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 } };
static const GUID kIID_IInspectable = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90 } };
static const GUID kIID_Compositor = { 0xb403ca50, 0x7f8c, 0x4e83, { 0x98,0x5f,0xcc,0x45,0x06,0x00,0x36,0xd8 } };
static const GUID kIID_Compositor3 = { 0xc9dd8ef0, 0x6eb1, 0x4e3c, { 0xa6,0x58,0x67,0x5d,0x9c,0x64,0xd4,0xab } };
static const GUID kIID_CompositionBrush = { 0xab0d7608, 0x30c0, 0x40e9, { 0xb5,0x68,0xb6,0x0a,0x6b,0xd1,0xfb,0x46 } };
static const GUID kIID_CompositionEffectFactory = { 0xbe5624af, 0xba7e, 0x4510, { 0x98,0x50,0x41,0xc0,0xb4,0xff,0x74,0xdf } };
static const GUID kIID_CompositionEffectBrush = { 0xbf7f795e, 0x83cc, 0x44bf, { 0xa4,0x47,0x3e,0x3c,0x07,0x17,0x89,0xec } };
static const GUID kIID_CompositionEffectSourceParameterFactory = { 0xb3d9f276, 0xaba3, 0x4724, { 0xac,0xf3,0xd0,0x39,0x74,0x64,0xdb,0x1c } };
static const GUID kIID_CompositionGraphicsDevice = { 0xfb22c6e1, 0x80a2, 0x4667, { 0x99,0x36,0xdb,0xea,0xf6,0xee,0xfe,0x95 } };
static const GUID kIID_CompositionSurface = { 0x1527540d, 0x42c7, 0x47a6, { 0xa4,0x08,0x66,0x8f,0x79,0xa9,0x0d,0xfb } };
static const GUID kIID_CompositionTarget = { 0xa1bea8ba, 0xd726, 0x4663, { 0x81,0x29,0x6b,0x5e,0x79,0x27,0xff,0xa6 } };
static const GUID kIID_ContainerVisual = { 0x02f6bc74, 0xed20, 0x4773, { 0xaf,0xe6,0xd4,0x9b,0x4a,0x93,0xdb,0x32 } };
static const GUID kIID_VisualCollection = { 0x8b745505, 0xfd3e, 0x4a98, { 0x84,0xa8,0xe9,0x49,0x46,0x8c,0x6b,0xcb } };
static const GUID kIID_Visual = { 0x117e202d, 0xa859, 0x4c89, { 0x87,0x3b,0xc2,0xaa,0x56,0x67,0x88,0xe3 } };
static const GUID kIID_Visual2 = { 0x3052b611, 0x56c3, 0x4c3e, { 0x8b,0xf3,0xf6,0xe1,0xad,0x47,0x3f,0x06 } };
static const GUID kIID_GraphicsEffect = { 0xcb51c0ce, 0x8fe6, 0x4636, { 0xb2,0x02,0x86,0x1f,0xaa,0x07,0xd8,0xf3 } };
static const GUID kIID_GraphicsEffectSource = { 0x2d8f9ddc, 0x4339, 0x4eb9, { 0x92,0x16,0xf9,0xde,0xb7,0x56,0x58,0xa2 } };
static const GUID kIID_PropertyValueStatics = { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96,0xb9,0x8d,0x96,0xc5,0xc1,0xe8,0x58 } };
static const GUID kIID_PropertyValue = { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a,0x9b,0x82,0x65,0x4e,0xde,0x7e,0x62 } };
static const GUID kIID_CompositorInterop = { 0x25297d5c, 0x3ad4, 0x4c9c, { 0xb5,0xcf,0xe3,0x6a,0x38,0x51,0x23,0x30 } };
static const GUID kIID_CompositorDesktopInterop = { 0x29e691fa, 0x4567, 0x4dca, { 0xb3,0x19,0xd0,0xf2,0x07,0xeb,0x68,0x07 } };
static const GUID kIID_CompositionDrawingSurfaceInterop = { 0xfd04e6e3, 0xfe0c, 0x4c3c, { 0xab,0x19,0xa0,0x76,0x01,0xa5,0x76,0xee } };
static const GUID kIID_GraphicsEffectD2D1Interop = { 0x2fc57384, 0xa068, 0x44d7, { 0xa3,0x31,0x30,0x98,0x2f,0xcf,0x71,0x77 } };

/* The D2D effect CLSIDs live in the SDK's dxguid.lib but not in mingw's
 * dxguid.a, so they are spelled out rather than linked. Values from
 * d2d1effects.h. */
static const GUID kCLSID_D2D1Flood = { 0x61c23c20, 0xae69, 0x4d8e, { 0x94,0xcf,0x50,0x07,0x8d,0xf6,0x38,0xf2 } };
static const GUID kCLSID_D2D1Composite = { 0x48fc9f51, 0xf6ac, 0x48f1, { 0x8b,0x58,0x3b,0x28,0xac,0x46,0xf7,0x6d } };
static const GUID kCLSID_D2D1Blend = { 0x81c5b77b, 0x13f8, 0x4cdd, { 0xad,0x20,0xc8,0x90,0x54,0x7a,0xc6,0x5d } };
static const GUID kCLSID_D2D1Border = { 0x2a2d49c0, 0x4acf, 0x43c7, { 0x8c,0x6a,0x7c,0x4a,0x27,0x87,0x4d,0x27 } };
static const GUID kCLSID_D2D1Opacity = { 0x811d79a4, 0xde28, 0x4454, { 0x80,0x94,0xc6,0x46,0x85,0xf8,0xbd,0x4c } };

typedef enum GRAPHICS_EFFECT_PROPERTY_MAPPING {
  GRAPHICS_EFFECT_PROPERTY_MAPPING_UNKNOWN,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORX,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORY,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORZ,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORW,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_RECT_TO_VECTOR4,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_RADIANS_TO_DEGREES,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_COLORMATRIX_ALPHA_MODE,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_COLOR_TO_VECTOR3,
  GRAPHICS_EFFECT_PROPERTY_MAPPING_COLOR_TO_VECTOR4
} GRAPHICS_EFFECT_PROPERTY_MAPPING;

typedef struct IGraphicsEffectD2D1Interop IGraphicsEffectD2D1Interop;
typedef struct IGraphicsEffectD2D1InteropVtbl {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(IGraphicsEffectD2D1Interop *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(IGraphicsEffectD2D1Interop *);
  ULONG(STDMETHODCALLTYPE *Release)(IGraphicsEffectD2D1Interop *);
  HRESULT(STDMETHODCALLTYPE *GetEffectId)(IGraphicsEffectD2D1Interop *, GUID *);
  HRESULT(STDMETHODCALLTYPE *GetNamedPropertyMapping)(IGraphicsEffectD2D1Interop *, LPCWSTR, UINT *, GRAPHICS_EFFECT_PROPERTY_MAPPING *);
  HRESULT(STDMETHODCALLTYPE *GetPropertyCount)(IGraphicsEffectD2D1Interop *, UINT *);
  HRESULT(STDMETHODCALLTYPE *GetProperty)(IGraphicsEffectD2D1Interop *, UINT, PropertyValue **);
  HRESULT(STDMETHODCALLTYPE *GetSource)(IGraphicsEffectD2D1Interop *, UINT, GraphicsEffectSource **);
  HRESULT(STDMETHODCALLTYPE *GetSourceCount)(IGraphicsEffectD2D1Interop *, UINT *);
} IGraphicsEffectD2D1InteropVtbl;
struct IGraphicsEffectD2D1Interop { const IGraphicsEffectD2D1InteropVtbl *lpVtbl; };

typedef struct ICompositorInterop ICompositorInterop;
typedef struct ICompositorInteropVtbl {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(ICompositorInterop *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(ICompositorInterop *);
  ULONG(STDMETHODCALLTYPE *Release)(ICompositorInterop *);
  HRESULT(STDMETHODCALLTYPE *CreateCompositionSurfaceForHandle)(ICompositorInterop *, HANDLE, CompositionSurface **);
  HRESULT(STDMETHODCALLTYPE *CreateCompositionSurfaceForSwapChain)(ICompositorInterop *, IUnknown *, CompositionSurface **);
  HRESULT(STDMETHODCALLTYPE *CreateGraphicsDevice)(ICompositorInterop *, IUnknown *, CompositionGraphicsDevice **);
} ICompositorInteropVtbl;
struct ICompositorInterop { const ICompositorInteropVtbl *lpVtbl; };

typedef struct ICompositorDesktopInterop ICompositorDesktopInterop;
typedef struct ICompositorDesktopInteropVtbl {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(ICompositorDesktopInterop *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(ICompositorDesktopInterop *);
  ULONG(STDMETHODCALLTYPE *Release)(ICompositorDesktopInterop *);
  HRESULT(STDMETHODCALLTYPE *CreateDesktopWindowTarget)(ICompositorDesktopInterop *, HWND, BOOL, IUnknown **);
  HRESULT(STDMETHODCALLTYPE *EnsureOnThread)(ICompositorDesktopInterop *, DWORD);
} ICompositorDesktopInteropVtbl;
struct ICompositorDesktopInterop { const ICompositorDesktopInteropVtbl *lpVtbl; };

typedef struct ICompositionDrawingSurfaceInterop ICompositionDrawingSurfaceInterop;
typedef struct ICompositionDrawingSurfaceInteropVtbl {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(ICompositionDrawingSurfaceInterop *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(ICompositionDrawingSurfaceInterop *);
  ULONG(STDMETHODCALLTYPE *Release)(ICompositionDrawingSurfaceInterop *);
  HRESULT(STDMETHODCALLTYPE *BeginDraw)(ICompositionDrawingSurfaceInterop *, const RECT *, REFIID, void **, POINT *);
  HRESULT(STDMETHODCALLTYPE *EndDraw)(ICompositionDrawingSurfaceInterop *);
  HRESULT(STDMETHODCALLTYPE *Resize)(ICompositionDrawingSurfaceInterop *, SIZE);
  HRESULT(STDMETHODCALLTYPE *Scroll)(ICompositionDrawingSurfaceInterop *, const RECT *, const RECT *, int, int);
  HRESULT(STDMETHODCALLTYPE *ResumeDraw)(ICompositionDrawingSurfaceInterop *);
  HRESULT(STDMETHODCALLTYPE *SuspendDraw)(ICompositionDrawingSurfaceInterop *);
} ICompositionDrawingSurfaceInteropVtbl;
struct ICompositionDrawingSurfaceInterop { const ICompositionDrawingSurfaceInteropVtbl *lpVtbl; };

typedef struct DispatcherQueueOptionsC {
  DWORD dwSize;
  int threadType;
  int apartmentType;
} DispatcherQueueOptionsC;
HRESULT WINAPI CreateDispatcherQueueController(DispatcherQueueOptionsC options, void **controller);

static HRESULT MakeString(const WCHAR *text, HSTRING *out) {
  return WindowsCreateString(text, (UINT32)wcslen(text), out);
}

static PropertyValueStatics *g_propertyValueStatics;

static HRESULT GetPropertyValueStatics(void) {
  HSTRING name = NULL;
  HRESULT hr;
  if (g_propertyValueStatics) return S_OK;
  hr = MakeString(L"Windows.Foundation.PropertyValue", &name);
  if (FAILED(hr)) return hr;
  hr = RoGetActivationFactory(name, &kIID_PropertyValueStatics, (void **)&g_propertyValueStatics);
  WindowsDeleteString(name);
  return hr;
}

/* --- the effect node, unchanged from acrylic_c.c ------------------------- */
typedef struct NamedProperty {
  const WCHAR *name;
  UINT index;
  GRAPHICS_EFFECT_PROPERTY_MAPPING mapping;
} NamedProperty;

#define MAX_SOURCES 2
#define MAX_PROPS 3

typedef struct EffectNode {
  GraphicsEffect effect;
  GraphicsEffectSource source;
  IGraphicsEffectD2D1Interop interop;

  LONG refCount;
  GUID clsid;
  HSTRING name;

  GraphicsEffectSource *sources[MAX_SOURCES];
  UINT sourceCount;
  PropertyValue *properties[MAX_PROPS];
  UINT propertyCount;
  const NamedProperty *named;
  UINT namedCount;
} EffectNode;

#define NODE_FROM_EFFECT(p) ((EffectNode *)((char *)(p) - offsetof(EffectNode, effect)))
#define NODE_FROM_SOURCE(p) ((EffectNode *)((char *)(p) - offsetof(EffectNode, source)))
#define NODE_FROM_INTEROP(p) ((EffectNode *)((char *)(p) - offsetof(EffectNode, interop)))

static HRESULT Node_QueryInterface(EffectNode *n, REFIID riid, void **ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualGUID(riid, &kIID_IUnknown) || IsEqualGUID(riid, &kIID_IInspectable) ||
      IsEqualGUID(riid, &kIID_GraphicsEffect)) {
    *ppv = &n->effect;
  } else if (IsEqualGUID(riid, &kIID_GraphicsEffectSource)) {
    *ppv = &n->source;
  } else if (IsEqualGUID(riid, &kIID_GraphicsEffectD2D1Interop)) {
    *ppv = &n->interop;
  } else {
    return E_NOINTERFACE;
  }
  InterlockedIncrement(&n->refCount);
  return S_OK;
}

static ULONG Node_Release(EffectNode *n) {
  const LONG remaining = InterlockedDecrement(&n->refCount);
  if (remaining == 0) {
    UINT i;
    for (i = 0; i < n->sourceCount; ++i)
      if (n->sources[i]) n->sources[i]->lpVtbl->Release(n->sources[i]);
    for (i = 0; i < n->propertyCount; ++i)
      if (n->properties[i]) n->properties[i]->lpVtbl->Release(n->properties[i]);
    if (n->name) WindowsDeleteString(n->name);
    CoTaskMemFree(n);
  }
  return (ULONG)remaining;
}

static HRESULT Node_GetIids(ULONG *count, IID **iids) {
  IID *out = (IID *)CoTaskMemAlloc(sizeof(IID) * 2);
  if (!out) return E_OUTOFMEMORY;
  out[0] = kIID_GraphicsEffect;
  out[1] = kIID_GraphicsEffectSource;
  *count = 2;
  *iids = out;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE Effect_QueryInterface(GraphicsEffect *This, REFIID riid, void **ppv) { return Node_QueryInterface(NODE_FROM_EFFECT(This), riid, ppv); }
static ULONG STDMETHODCALLTYPE Effect_AddRef(GraphicsEffect *This) { return (ULONG)InterlockedIncrement(&NODE_FROM_EFFECT(This)->refCount); }
static ULONG STDMETHODCALLTYPE Effect_Release(GraphicsEffect *This) { return Node_Release(NODE_FROM_EFFECT(This)); }
static HRESULT STDMETHODCALLTYPE Effect_GetIids(GraphicsEffect *This, ULONG *count, IID **iids) { (void)This; return Node_GetIids(count, iids); }
static HRESULT STDMETHODCALLTYPE Effect_GetRuntimeClassName(GraphicsEffect *This, HSTRING *out) { (void)This; return MakeString(L"Windows.Graphics.Effects.IGraphicsEffect", out); }
static HRESULT STDMETHODCALLTYPE Effect_GetTrustLevel(GraphicsEffect *This, TrustLevel *level) { (void)This; *level = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE Effect_get_Name(GraphicsEffect *This, HSTRING *value) { return WindowsDuplicateString(NODE_FROM_EFFECT(This)->name, value); }
static HRESULT STDMETHODCALLTYPE Effect_put_Name(GraphicsEffect *This, HSTRING value) {
  EffectNode *n = NODE_FROM_EFFECT(This);
  if (n->name) WindowsDeleteString(n->name);
  return WindowsDuplicateString(value, &n->name);
}

static const __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffectVtbl g_effectVtbl = {
  Effect_QueryInterface, Effect_AddRef, Effect_Release,
  Effect_GetIids, Effect_GetRuntimeClassName, Effect_GetTrustLevel,
  Effect_get_Name, Effect_put_Name
};

static HRESULT STDMETHODCALLTYPE Source_QueryInterface(GraphicsEffectSource *This, REFIID riid, void **ppv) { return Node_QueryInterface(NODE_FROM_SOURCE(This), riid, ppv); }
static ULONG STDMETHODCALLTYPE Source_AddRef(GraphicsEffectSource *This) { return (ULONG)InterlockedIncrement(&NODE_FROM_SOURCE(This)->refCount); }
static ULONG STDMETHODCALLTYPE Source_Release(GraphicsEffectSource *This) { return Node_Release(NODE_FROM_SOURCE(This)); }
static HRESULT STDMETHODCALLTYPE Source_GetIids(GraphicsEffectSource *This, ULONG *count, IID **iids) { (void)This; return Node_GetIids(count, iids); }
static HRESULT STDMETHODCALLTYPE Source_GetRuntimeClassName(GraphicsEffectSource *This, HSTRING *out) { (void)This; return MakeString(L"Windows.Graphics.Effects.IGraphicsEffectSource", out); }
static HRESULT STDMETHODCALLTYPE Source_GetTrustLevel(GraphicsEffectSource *This, TrustLevel *level) { (void)This; *level = BaseTrust; return S_OK; }

static const __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffectSourceVtbl g_sourceVtbl = {
  Source_QueryInterface, Source_AddRef, Source_Release,
  Source_GetIids, Source_GetRuntimeClassName, Source_GetTrustLevel
};

static HRESULT STDMETHODCALLTYPE Interop_QueryInterface(IGraphicsEffectD2D1Interop *This, REFIID riid, void **ppv) { return Node_QueryInterface(NODE_FROM_INTEROP(This), riid, ppv); }
static ULONG STDMETHODCALLTYPE Interop_AddRef(IGraphicsEffectD2D1Interop *This) { return (ULONG)InterlockedIncrement(&NODE_FROM_INTEROP(This)->refCount); }
static ULONG STDMETHODCALLTYPE Interop_Release(IGraphicsEffectD2D1Interop *This) { return Node_Release(NODE_FROM_INTEROP(This)); }
static HRESULT STDMETHODCALLTYPE Interop_GetEffectId(IGraphicsEffectD2D1Interop *This, GUID *id) { *id = NODE_FROM_INTEROP(This)->clsid; return S_OK; }
static HRESULT STDMETHODCALLTYPE Interop_GetNamedPropertyMapping(IGraphicsEffectD2D1Interop *This, LPCWSTR name, UINT *index, GRAPHICS_EFFECT_PROPERTY_MAPPING *mapping) {
  EffectNode *n = NODE_FROM_INTEROP(This);
  UINT i;
  for (i = 0; i < n->namedCount; ++i) {
    if (_wcsicmp(name, n->named[i].name) == 0) {
      *index = n->named[i].index;
      *mapping = n->named[i].mapping;
      return S_OK;
    }
  }
  return E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE Interop_GetPropertyCount(IGraphicsEffectD2D1Interop *This, UINT *count) { *count = NODE_FROM_INTEROP(This)->propertyCount; return S_OK; }
static HRESULT STDMETHODCALLTYPE Interop_GetProperty(IGraphicsEffectD2D1Interop *This, UINT index, PropertyValue **value) {
  EffectNode *n = NODE_FROM_INTEROP(This);
  if (index >= n->propertyCount) return E_INVALIDARG;
  *value = n->properties[index];
  (*value)->lpVtbl->AddRef(*value);
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE Interop_GetSource(IGraphicsEffectD2D1Interop *This, UINT index, GraphicsEffectSource **source) {
  EffectNode *n = NODE_FROM_INTEROP(This);
  if (index >= n->sourceCount) return E_INVALIDARG;
  *source = n->sources[index];
  (*source)->lpVtbl->AddRef(*source);
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE Interop_GetSourceCount(IGraphicsEffectD2D1Interop *This, UINT *count) { *count = NODE_FROM_INTEROP(This)->sourceCount; return S_OK; }

static const IGraphicsEffectD2D1InteropVtbl g_interopVtbl = {
  Interop_QueryInterface, Interop_AddRef, Interop_Release,
  Interop_GetEffectId, Interop_GetNamedPropertyMapping,
  Interop_GetPropertyCount, Interop_GetProperty,
  Interop_GetSource, Interop_GetSourceCount
};

static EffectNode *NodeCreate(const GUID *clsid, const WCHAR *name) {
  EffectNode *n = (EffectNode *)CoTaskMemAlloc(sizeof(EffectNode));
  if (!n) return NULL;
  ZeroMemory(n, sizeof(*n));
  n->effect.lpVtbl = (void *)&g_effectVtbl;
  n->source.lpVtbl = (void *)&g_sourceVtbl;
  n->interop.lpVtbl = &g_interopVtbl;
  n->refCount = 1;
  n->clsid = *clsid;
  if (name) MakeString(name, &n->name);
  return n;
}

static void NodeAddSource(EffectNode *n, GraphicsEffectSource *s) {
  n->sources[n->sourceCount++] = s;
  s->lpVtbl->AddRef(s);
}

static HRESULT NodeAddPropertyFromInspectable(EffectNode *n, IInspectable *insp) {
  PropertyValue *pv = NULL;
  HRESULT hr;
  if (!insp) return E_FAIL;
  hr = insp->lpVtbl->QueryInterface(insp, &kIID_PropertyValue, (void **)&pv);
  insp->lpVtbl->Release(insp);
  if (FAILED(hr)) return hr;
  n->properties[n->propertyCount++] = pv;
  return S_OK;
}

static HRESULT NodeAddPropSingle(EffectNode *n, float value) {
  IInspectable *insp = NULL;
  CHECK(g_propertyValueStatics->lpVtbl->CreateSingle(g_propertyValueStatics, value, &insp));
  return NodeAddPropertyFromInspectable(n, insp);
}

static HRESULT NodeAddPropUInt32(EffectNode *n, UINT32 value) {
  IInspectable *insp = NULL;
  CHECK(g_propertyValueStatics->lpVtbl->CreateUInt32(g_propertyValueStatics, value, &insp));
  return NodeAddPropertyFromInspectable(n, insp);
}

static HRESULT NodeAddPropSingleArray(EffectNode *n, float *values, UINT32 count) {
  IInspectable *insp = NULL;
  CHECK(g_propertyValueStatics->lpVtbl->CreateSingleArray(g_propertyValueStatics, count, values, &insp));
  return NodeAddPropertyFromInspectable(n, insp);
}

static GraphicsEffectSource *Src(EffectNode *n) { return &n->source; }

static const NamedProperty kFloodProps[] = {
  { L"Color", D2D1_FLOOD_PROP_COLOR, GRAPHICS_EFFECT_PROPERTY_MAPPING_COLOR_TO_VECTOR4 }
};

static EffectNode *MakeColorSource(const WCHAR *name, UIColor color) {
  EffectNode *n = NodeCreate(&kCLSID_D2D1Flood, name);
  float v[4];
  if (!n) return NULL;
  v[0] = color.R / 255.0f;
  v[1] = color.G / 255.0f;
  v[2] = color.B / 255.0f;
  v[3] = color.A / 255.0f;
  NodeAddPropSingleArray(n, v, 4);
  n->named = kFloodProps;
  n->namedCount = ARRAYSIZE(kFloodProps);
  return n;
}

static const NamedProperty kCompositeProps[] = {
  { L"Mode", D2D1_COMPOSITE_PROP_MODE, GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT }
};

static EffectNode *MakeComposite(D2D1_COMPOSITE_MODE mode, GraphicsEffectSource *destination, GraphicsEffectSource *source) {
  EffectNode *n = NodeCreate(&kCLSID_D2D1Composite, NULL);
  if (!n) return NULL;
  NodeAddSource(n, destination);
  NodeAddSource(n, source);
  NodeAddPropUInt32(n, (UINT32)mode);
  n->named = kCompositeProps;
  n->namedCount = ARRAYSIZE(kCompositeProps);
  return n;
}

static const NamedProperty kBlendProps[] = {
  { L"Mode", D2D1_BLEND_PROP_MODE, GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT }
};

static EffectNode *MakeBlend(UINT32 mode, GraphicsEffectSource *background, GraphicsEffectSource *foreground) {
  EffectNode *n = NodeCreate(&kCLSID_D2D1Blend, NULL);
  if (!n) return NULL;
  NodeAddSource(n, background);
  NodeAddSource(n, foreground);
  NodeAddPropUInt32(n, mode);
  n->named = kBlendProps;
  n->namedCount = ARRAYSIZE(kBlendProps);
  return n;
}

static const NamedProperty kBorderProps[] = {
  { L"ExtendX", D2D1_BORDER_PROP_EDGE_MODE_X, GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT },
  { L"ExtendY", D2D1_BORDER_PROP_EDGE_MODE_Y, GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT }
};

static EffectNode *MakeBorder(D2D1_BORDER_EDGE_MODE extendX, D2D1_BORDER_EDGE_MODE extendY, GraphicsEffectSource *source) {
  EffectNode *n = NodeCreate(&kCLSID_D2D1Border, NULL);
  if (!n) return NULL;
  NodeAddSource(n, source);
  NodeAddPropUInt32(n, (UINT32)extendX);
  NodeAddPropUInt32(n, (UINT32)extendY);
  n->named = kBorderProps;
  n->namedCount = ARRAYSIZE(kBorderProps);
  return n;
}

static const NamedProperty kOpacityProps[] = {
  { L"Opacity", D2D1_OPACITY_PROP_OPACITY, GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT }
};

static EffectNode *MakeOpacity(const WCHAR *name, float opacity, GraphicsEffectSource *source) {
  EffectNode *n = NodeCreate(&kCLSID_D2D1Opacity, name);
  if (!n) return NULL;
  NodeAddSource(n, source);
  NodeAddPropSingle(n, opacity);
  n->named = kOpacityProps;
  n->namedCount = ARRAYSIZE(kOpacityProps);
  return n;
}

/* --- colour derivation, transcribed from AcrylicBrush.cpp ---------------- */
typedef struct Hsv { double h, s, v; } Hsv;

static double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Hsv RgbToHsv(double r, double g, double b) {
  Hsv hsv;
  const double mx = r >= g ? (r >= b ? r : b) : (g >= b ? g : b);
  const double mn = r <= g ? (r <= b ? r : b) : (g <= b ? g : b);
  const double chroma = mx - mn;
  hsv.h = 0.0; hsv.s = 0.0; hsv.v = mx;
  if (chroma != 0.0) {
    if (r == mx) hsv.h = 60 * (g - b) / chroma;
    else if (g == mx) hsv.h = 120 + 60 * (b - r) / chroma;
    else hsv.h = 240 + 60 * (r - g) / chroma;
    if (hsv.h < 0.0) hsv.h += 360.0;
    hsv.s = chroma / mx;
  }
  return hsv;
}

static void HsvToRgb(Hsv hsv, double *r, double *g, double *b) {
  double hue = hsv.h, s, v, chroma, h1, x, m, rr, gg, bb;
  while (hue >= 360.0) hue -= 360.0;
  while (hue < 0.0) hue += 360.0;
  s = ClampD(hsv.s, 0.0, 1.0);
  v = ClampD(hsv.v, 0.0, 1.0);
  chroma = v * s;
  h1 = hue / 60.0;
  x = chroma * (1 - fabs(fmod(h1, 2.0) - 1));
  m = v - chroma;
  if (h1 < 1)      { rr = chroma; gg = x;      bb = 0; }
  else if (h1 < 2) { rr = x;      gg = chroma; bb = 0; }
  else if (h1 < 3) { rr = 0;      gg = chroma; bb = x; }
  else if (h1 < 4) { rr = 0;      gg = x;      bb = chroma; }
  else if (h1 < 5) { rr = x;      gg = 0;      bb = chroma; }
  else             { rr = chroma; gg = 0;      bb = x; }
  *r = rr + m; *g = gg + m; *b = bb + m;
}

static BYTE ToByte(double unit) { return (BYTE)(floor(ClampD(unit, 0.0, 1.0) * 255.0 + 0.5)); }

static double GetTintOpacityModifier(UIColor tintColor) {
  const double midPoint = 0.50;
  const double whiteMaxOpacity = 0.45;
  const double midPointMaxOpacity = 0.90;
  const double blackMaxOpacity = 0.85;
  const Hsv hsv = RgbToHsv(tintColor.R / 255.0, tintColor.G / 255.0, tintColor.B / 255.0);
  double opacityModifier = midPointMaxOpacity;

  if (hsv.v != midPoint) {
    double lowestMaxOpacity = midPointMaxOpacity;
    double maxDeviation = midPoint;
    double maxOpacitySuppression, deviation, normalizedDeviation;

    if (hsv.v > midPoint) { lowestMaxOpacity = whiteMaxOpacity; maxDeviation = 1 - maxDeviation; }
    else if (hsv.v < midPoint) lowestMaxOpacity = blackMaxOpacity;

    maxOpacitySuppression = midPointMaxOpacity - lowestMaxOpacity;
    deviation = fabs(hsv.v - midPoint);
    normalizedDeviation = deviation / maxDeviation;

    if (hsv.s > 0) {
      const double damped = 1 - (hsv.s * 2);
      maxOpacitySuppression *= damped > 0.0 ? damped : 0.0;
    }
    opacityModifier = midPointMaxOpacity - maxOpacitySuppression * normalizedDeviation;
  }
  return opacityModifier;
}

static UIColor GetEffectiveTintColor(unsigned int rgb, float opacity) {
  UIColor c;
  c.A = 255;
  c.R = (BYTE)((rgb >> 16) & 0xFF);
  c.G = (BYTE)((rgb >> 8) & 0xFF);
  c.B = (BYTE)(rgb & 0xFF);
  c.A = (BYTE)floor(c.A * opacity * GetTintOpacityModifier(c) + 0.5);
  return c;
}
static UIColor GetEffectiveLuminosityColor(unsigned int rgb, float opacity) {
  const double minHsvV = 0.125, maxHsvV = 0.965;
  const double minLuminosityOpacity = 0.15, maxLuminosityOpacity = 1.03;
  UIColor tint, out;
  Hsv hsv;
  double r, g, b, mappedTintOpacity;

  tint.A = 255;
  tint.R = (BYTE)((rgb >> 16) & 0xFF);
  tint.G = (BYTE)((rgb >> 8) & 0xFF);
  tint.B = (BYTE)(rgb & 0xFF);
  tint.A = (BYTE)floor(tint.A * opacity + 0.5);

  hsv = RgbToHsv(tint.R / 255.0, tint.G / 255.0, tint.B / 255.0);
  hsv.v = ClampD(hsv.v, minHsvV, maxHsvV);
  HsvToRgb(hsv, &r, &g, &b);

  mappedTintOpacity = (tint.A / 255.0) * (maxLuminosityOpacity - minLuminosityOpacity) + minLuminosityOpacity;

  out.A = ToByte(mappedTintOpacity < 1.0 ? mappedTintOpacity : 1.0);
  out.R = ToByte(r); out.G = ToByte(g); out.B = ToByte(b);
  return out;
}
static HRESULT CreateNoiseBrush(Compositor *compositor, ID3D11Device *d3dDevice, CompositionSurfaceBrush **out) {
  ICompositorInterop *interop = NULL;
  CompositionGraphicsDevice *graphicsDevice = NULL;
  CompositionDrawingSurface *drawingSurface = NULL;
  ICompositionDrawingSurfaceInterop *surfaceInterop = NULL;
  CompositionSurface *surface = NULL;
  IWICImagingFactory *wic = NULL;
  IWICStream *stream = NULL;
  IWICBitmapDecoder *decoder = NULL;
  IWICBitmapFrameDecode *frame = NULL;
  IWICFormatConverter *converter = NULL;
  IDXGISurface *dxgiSurface = NULL;
  ID3D11Texture2D *atlasTexture = NULL;
  ID3D11DeviceContext *context = NULL;
  CompositionSurfaceBrush *brush = NULL;
  SizeF size;
  POINT offset;
  D3D11_BOX box;
  BYTE *pixels = NULL;
  const UINT stride = 256 * 4;
  HRESULT hr;

  CHECK(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic));
  CHECK(IWICImagingFactory_CreateStream(wic, &stream));
  CHECK(IWICStream_InitializeFromMemory(stream, (BYTE *)kNoisePng, kNoisePngSize));
  CHECK(IWICImagingFactory_CreateDecoderFromStream(wic, (IStream *)stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder));
  CHECK(IWICBitmapDecoder_GetFrame(decoder, 0, &frame));
  CHECK(IWICImagingFactory_CreateFormatConverter(wic, &converter));
  CHECK(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut));

  pixels = (BYTE *)malloc(stride * 256);
  if (!pixels) return E_OUTOFMEMORY;
  hr = IWICFormatConverter_CopyPixels(converter, NULL, stride, stride * 256, pixels);
  if (FAILED(hr)) { free(pixels); return hr; }

  CHECK(compositor->lpVtbl->QueryInterface(compositor, &kIID_CompositorInterop, (void **)&interop));
  CHECK(interop->lpVtbl->CreateGraphicsDevice(interop, (IUnknown *)d3dDevice, &graphicsDevice));

  size.Width = 256.0f; size.Height = 256.0f;
  CHECK(graphicsDevice->lpVtbl->CreateDrawingSurface(graphicsDevice, size, PIXELFORMAT_B8G8R8A8_UINTNORMALIZED,
                                                     ALPHAMODE_PREMULTIPLIED, &drawingSurface));

  CHECK(drawingSurface->lpVtbl->QueryInterface(drawingSurface, &kIID_CompositionDrawingSurfaceInterop, (void **)&surfaceInterop));
  CHECK(surfaceInterop->lpVtbl->BeginDraw(surfaceInterop, NULL, &IID_IDXGISurface, (void **)&dxgiSurface, &offset));

  hr = IDXGISurface_QueryInterface(dxgiSurface, &IID_ID3D11Texture2D, (void **)&atlasTexture);
  if (SUCCEEDED(hr)) {
    ID3D11Device_GetImmediateContext(d3dDevice, &context);
    box.left = (UINT)offset.x; box.top = (UINT)offset.y; box.front = 0;
    box.right = (UINT)offset.x + 256; box.bottom = (UINT)offset.y + 256; box.back = 1;
    ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)atlasTexture, 0, &box, pixels, stride, 0);
    ID3D11DeviceContext_Release(context);
    ID3D11Texture2D_Release(atlasTexture);
  }
  IDXGISurface_Release(dxgiSurface);
  surfaceInterop->lpVtbl->EndDraw(surfaceInterop);
  free(pixels);
  CHECK(hr);

  CHECK(drawingSurface->lpVtbl->QueryInterface(drawingSurface, &kIID_CompositionSurface, (void **)&surface));
  CHECK(compositor->lpVtbl->CreateSurfaceBrushWithSurface(compositor, surface, &brush));
  CHECK(brush->lpVtbl->put_Stretch(brush, COMPOSITIONSTRETCH_NONE));
  CHECK(brush->lpVtbl->put_HorizontalAlignmentRatio(brush, 0.0f));
  CHECK(brush->lpVtbl->put_VerticalAlignmentRatio(brush, 0.0f));

  *out = brush;
  return S_OK;
}


//========================================================================
// The swapchain, the composition tree, and the GL bridge
//========================================================================

#define WGL_ACCESS_READ_WRITE_NV 0x0001
#define ACRYLIC_GL_FRAMEBUFFER 0x8D40
#define ACRYLIC_GL_COLOR_ATTACHMENT0 0x8CE0
#define ACRYLIC_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define ACRYLIC_GL_TEXTURE_2D 0x0DE1
#define ACRYLIC_GL_READ_FRAMEBUFFER 0x8CA8
#define ACRYLIC_GL_DRAW_FRAMEBUFFER 0x8CA9
#define ACRYLIC_GL_COLOR_BUFFER_BIT 0x00004000
#define ACRYLIC_GL_NEAREST 0x2600

typedef HANDLE(WINAPI *PFN_wglDXOpenDeviceNV)(void *);
typedef BOOL(WINAPI *PFN_wglDXCloseDeviceNV)(HANDLE);
typedef HANDLE(WINAPI *PFN_wglDXRegisterObjectNV)(HANDLE, void *, unsigned int, unsigned int, unsigned int);
typedef BOOL(WINAPI *PFN_wglDXUnregisterObjectNV)(HANDLE, HANDLE);
typedef BOOL(WINAPI *PFN_wglDXLockObjectsNV)(HANDLE, int, HANDLE *);
typedef BOOL(WINAPI *PFN_wglDXUnlockObjectsNV)(HANDLE, int, HANDLE *);
typedef void(APIENTRY *PFN_glGenTextures)(int, unsigned int *);
typedef void(APIENTRY *PFN_glDeleteTextures)(int, const unsigned int *);
typedef void(APIENTRY *PFN_glGenFramebuffers)(int, unsigned int *);
typedef void(APIENTRY *PFN_glBindFramebuffer)(unsigned int, unsigned int);
typedef void(APIENTRY *PFN_glFramebufferTexture2D)(unsigned int, unsigned int, unsigned int, unsigned int, int);
typedef unsigned int(APIENTRY *PFN_glCheckFramebufferStatus)(unsigned int);
typedef void(APIENTRY *PFN_glFlush)(void);
typedef void(APIENTRY *PFN_glGetIntegerv)(unsigned int, int *);
typedef void(APIENTRY *PFN_glBlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);

typedef struct AcrylicGL {
    PFN_wglDXOpenDeviceNV OpenDevice;
    PFN_wglDXCloseDeviceNV CloseDevice;
    PFN_wglDXRegisterObjectNV RegisterObject;
    PFN_wglDXUnregisterObjectNV UnregisterObject;
    PFN_wglDXLockObjectsNV LockObjects;
    PFN_wglDXUnlockObjectsNV UnlockObjects;
    PFN_glGenTextures GenTextures;
    PFN_glDeleteTextures DeleteTextures;
    PFN_glGenFramebuffers GenFramebuffers;
    PFN_glBindFramebuffer BindFramebuffer;
    PFN_glFramebufferTexture2D FramebufferTexture2D;
    PFN_glCheckFramebufferStatus CheckFramebufferStatus;
    PFN_glFlush Flush;
    PFN_glGetIntegerv GetIntegerv;
    PFN_glBlitFramebuffer BlitFramebuffer;
} AcrylicGL;

// One per thread, not one per window, and it lives as long as the thread does.
// Releasing it when a window closes would leave the next window unable to make
// another (CreateDispatcherQueueController answers RPC_E_WRONG_THREAD once the
// thread already has one).
static IUnknown *g_dispatcherController;

typedef struct _GLFWacrylicWin32 {
    ID3D11Device *d3dDevice;
    IDXGISwapChain1 *swapChain;

    Compositor *compositor;
    CompositionTarget *target;
    CompositionEffectBrush *acrylicBrush;
    SpriteVisual *acrylicVisual;
    HSTRING backdropName;
    HSTRING noiseName;
    CompositionBrush *noiseBrushBase;
    CompositionBrush *backdropBrushBase;

    AcrylicGL gl;
    HANDLE interopDevice;
    HANDLE interopTexture;
    unsigned int glTexture;
    unsigned int glFbo;

    int width, height;
    int pendingWidth, pendingHeight;  // 0 when there is nothing to apply
    unsigned int tintRgb;
    float tintOpacity;
    bool locked;      // interop texture currently locked for GL use
    bool bound;       // framebuffer bound and usable
} _GLFWacrylicWin32;

static _GLFWacrylicWin32 *acrylic_of(_GLFWwindow *window) {
    return (_GLFWacrylicWin32 *) window->win32.acrylic;
}

bool _glfwWin32AcrylicActive(_GLFWwindow *window) {
    const _GLFWacrylicWin32 *a = acrylic_of(window);
    return a != NULL && a->bound;
}

// Build the effect graph and hand it the two source brushes. Split out because
// a tint change has to rebuild it: the colours are baked into the Flood nodes.
static HRESULT buildAcrylicBrush(_GLFWacrylicWin32 *a) {
    EffectSourceParameterFactory *parameterFactory = NULL;
    EffectSourceParameter *backdropParameterObject = NULL, *noiseParameterObject = NULL;
    GraphicsEffectSource *backdropParameter = NULL, *noiseParameter = NULL;
    CompositionEffectFactory *effectFactory = NULL;
    CompositionEffectBrush *brush = NULL;
    CompositionBrush *brushBase = NULL;
    HSTRING paramClassId = NULL;
    EffectNode *tintColorEffect, *luminosityColorEffect, *luminosityBlend;
    EffectNode *colorBlend, *noiseBorder, *noiseOpacity, *outerComposite;
    UIColor tintColor, luminosityColor;

    CHECK(MakeString(L"Windows.UI.Composition.CompositionEffectSourceParameter", &paramClassId));
    CHECK(RoGetActivationFactory(paramClassId, &kIID_CompositionEffectSourceParameterFactory, (void **)&parameterFactory));
    WindowsDeleteString(paramClassId);
    CHECK(parameterFactory->lpVtbl->Create(parameterFactory, a->backdropName, &backdropParameterObject));
    CHECK(parameterFactory->lpVtbl->Create(parameterFactory, a->noiseName, &noiseParameterObject));
    CHECK(backdropParameterObject->lpVtbl->QueryInterface(backdropParameterObject, &kIID_GraphicsEffectSource, (void **)&backdropParameter));
    CHECK(noiseParameterObject->lpVtbl->QueryInterface(noiseParameterObject, &kIID_GraphicsEffectSource, (void **)&noiseParameter));

    tintColor = GetEffectiveTintColor(a->tintRgb, a->tintOpacity);
    luminosityColor = GetEffectiveLuminosityColor(a->tintRgb, a->tintOpacity);

    tintColorEffect = MakeColorSource(L"TintColor", tintColor);
    luminosityColorEffect = MakeColorSource(L"LuminosityColor", luminosityColor);

    // WinUI asks for Color (22) where it means luminosity and Luminosity (23)
    // where it means colour, because the two enum names are swapped upstream.
    // Both go straight through to D2D1_BLEND_PROP_MODE, so passing them
    // verbatim is what reproduces Terminal. Correcting them turns the window
    // greyscale.
    luminosityBlend = MakeBlend(22, backdropParameter, Src(luminosityColorEffect));
    colorBlend = MakeBlend(23, Src(luminosityBlend), Src(tintColorEffect));

    noiseBorder = MakeBorder(D2D1_BORDER_EDGE_MODE_WRAP, D2D1_BORDER_EDGE_MODE_WRAP, noiseParameter);
    noiseOpacity = MakeOpacity(L"NoiseOpacity", sc_noiseOpacity, Src(noiseBorder));
    outerComposite = MakeComposite(D2D1_COMPOSITE_MODE_SOURCE_OVER, Src(colorBlend), Src(noiseOpacity));

    CHECK(a->compositor->lpVtbl->CreateEffectFactory(a->compositor, &outerComposite->effect, &effectFactory));
    CHECK(effectFactory->lpVtbl->CreateBrush(effectFactory, &brush));
    CHECK(brush->lpVtbl->SetSourceParameter(brush, a->backdropName, a->backdropBrushBase));
    CHECK(brush->lpVtbl->SetSourceParameter(brush, a->noiseName, a->noiseBrushBase));
    CHECK(brush->lpVtbl->QueryInterface(brush, &kIID_CompositionBrush, (void **)&brushBase));
    CHECK(a->acrylicVisual->lpVtbl->put_Brush(a->acrylicVisual, brushBase));

    if (a->acrylicBrush) a->acrylicBrush->lpVtbl->Release(a->acrylicBrush);
    a->acrylicBrush = brush;
    return S_OK;
}

static bool loadGLEntryPoints(AcrylicGL *gl) {
    gl->OpenDevice = (PFN_wglDXOpenDeviceNV) _glfw.wgl.GetProcAddress("wglDXOpenDeviceNV");
    gl->CloseDevice = (PFN_wglDXCloseDeviceNV) _glfw.wgl.GetProcAddress("wglDXCloseDeviceNV");
    gl->RegisterObject = (PFN_wglDXRegisterObjectNV) _glfw.wgl.GetProcAddress("wglDXRegisterObjectNV");
    gl->UnregisterObject = (PFN_wglDXUnregisterObjectNV) _glfw.wgl.GetProcAddress("wglDXUnregisterObjectNV");
    gl->LockObjects = (PFN_wglDXLockObjectsNV) _glfw.wgl.GetProcAddress("wglDXLockObjectsNV");
    gl->UnlockObjects = (PFN_wglDXUnlockObjectsNV) _glfw.wgl.GetProcAddress("wglDXUnlockObjectsNV");
    gl->GenFramebuffers = (PFN_glGenFramebuffers) _glfw.wgl.GetProcAddress("glGenFramebuffers");
    gl->BindFramebuffer = (PFN_glBindFramebuffer) _glfw.wgl.GetProcAddress("glBindFramebuffer");
    gl->FramebufferTexture2D = (PFN_glFramebufferTexture2D) _glfw.wgl.GetProcAddress("glFramebufferTexture2D");
    gl->CheckFramebufferStatus = (PFN_glCheckFramebufferStatus) _glfw.wgl.GetProcAddress("glCheckFramebufferStatus");
    gl->BlitFramebuffer = (PFN_glBlitFramebuffer) _glfw.wgl.GetProcAddress("glBlitFramebuffer");
    // The GL 1.1 entry points live in opengl32 itself, not in the ICD, so
    // wglGetProcAddress does not return them.
    {
        HMODULE opengl32 = GetModuleHandleW(L"opengl32.dll");
        if (!opengl32) return false;
        gl->GenTextures = (PFN_glGenTextures) GetProcAddress(opengl32, "glGenTextures");
        gl->DeleteTextures = (PFN_glDeleteTextures) GetProcAddress(opengl32, "glDeleteTextures");
        gl->Flush = (PFN_glFlush) GetProcAddress(opengl32, "glFlush");
        gl->GetIntegerv = (PFN_glGetIntegerv) GetProcAddress(opengl32, "glGetIntegerv");
    }
    return gl->OpenDevice && gl->RegisterObject && gl->UnregisterObject && gl->LockObjects &&
           gl->UnlockObjects && gl->GenFramebuffers && gl->BindFramebuffer &&
           gl->FramebufferTexture2D && gl->CheckFramebufferStatus && gl->GenTextures &&
           gl->DeleteTextures && gl->Flush && gl->BlitFramebuffer && gl->GetIntegerv;
}

static void unbindBackBuffer(_GLFWacrylicWin32 *a) {
    if (!a->interopTexture) return;
    if (a->locked) {
        a->gl.UnlockObjects(a->interopDevice, 1, &a->interopTexture);
        a->locked = false;
    }
    a->gl.UnregisterObject(a->interopDevice, a->interopTexture);
    a->interopTexture = NULL;
}

// Register the swapchain back buffer with GL and leave it bound. kitty draws
// to framebuffer 0 and never rebinds, so binding once here is enough.
static bool bindBackBuffer(_GLFWacrylicWin32 *a) {
    ID3D11Texture2D *backBuffer = NULL;

    if (FAILED(IDXGISwapChain1_GetBuffer(a->swapChain, 0, &IID_ID3D11Texture2D, (void **)&backBuffer)))
        return false;

    a->interopTexture = a->gl.RegisterObject(a->interopDevice, backBuffer, a->glTexture,
                                             ACRYLIC_GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV);
    ID3D11Texture2D_Release(backBuffer);
    if (!a->interopTexture) { ADBG("wglDXRegisterObjectNV failed: %lu", GetLastError()); return false; }

    if (!a->gl.LockObjects(a->interopDevice, 1, &a->interopTexture)) {
        a->gl.UnregisterObject(a->interopDevice, a->interopTexture);
        a->interopTexture = NULL;
        return false;
    }
    a->locked = true;

    a->gl.BindFramebuffer(ACRYLIC_GL_FRAMEBUFFER, a->glFbo);
    a->gl.FramebufferTexture2D(ACRYLIC_GL_FRAMEBUFFER, ACRYLIC_GL_COLOR_ATTACHMENT0,
                               ACRYLIC_GL_TEXTURE_2D, a->glTexture, 0);
    const bool complete =
        a->gl.CheckFramebufferStatus(ACRYLIC_GL_FRAMEBUFFER) == ACRYLIC_GL_FRAMEBUFFER_COMPLETE;
    // Leave the default framebuffer bound. kitty renders through
    // bind_framebuffer_for_output(), which rebinds 0 every frame, so this FBO
    // is only ever a blit destination in Present, never the draw target.
    a->gl.BindFramebuffer(ACRYLIC_GL_FRAMEBUFFER, 0);
    if (!complete) {
        unbindBackBuffer(a);
        return false;
    }
    return true;
}


static HRESULT createCompositionTree(_GLFWacrylicWin32 *a, HWND hwnd) {
    HSTRING classId = NULL;
    IInspectable *compositorInspectable = NULL;
    Compositor3 *compositor3 = NULL;
    ICompositorInterop *compositorInterop = NULL;
    ICompositorDesktopInterop *desktopInterop = NULL;
    IUnknown *desktopTarget = NULL;
    CompositionSurfaceBrush *noiseBrush = NULL;
    CompositionBackdropBrush *backdropBrush = NULL;
    CompositionSurface *swapChainSurface = NULL;
    CompositionSurfaceBrush *swapChainBrush = NULL;
    CompositionBrush *swapChainBrushBase = NULL;
    ContainerVisual *root = NULL;
    Visual *rootVisual = NULL, *acrylicVisualBase = NULL, *contentVisualBase = NULL;
    Visual2 *rootVisual2 = NULL, *acrylicVisual2 = NULL, *contentVisual2 = NULL;
    VisualCollection *children = NULL;
    SpriteVisual *contentVisual = NULL;
    Vector2 fill;

    CHECK(MakeString(L"Windows.UI.Composition.Compositor", &classId));
    CHECK(RoActivateInstance(classId, &compositorInspectable));
    WindowsDeleteString(classId);
    CHECK(compositorInspectable->lpVtbl->QueryInterface(compositorInspectable, &kIID_Compositor, (void **)&a->compositor));
    CHECK(a->compositor->lpVtbl->QueryInterface(a->compositor, &kIID_Compositor3, (void **)&compositor3));
    CHECK(a->compositor->lpVtbl->QueryInterface(a->compositor, &kIID_CompositorInterop, (void **)&compositorInterop));
    CHECK(a->compositor->lpVtbl->QueryInterface(a->compositor, &kIID_CompositorDesktopInterop, (void **)&desktopInterop));
    CHECK(desktopInterop->lpVtbl->CreateDesktopWindowTarget(desktopInterop, hwnd, FALSE, &desktopTarget));
    CHECK(desktopTarget->lpVtbl->QueryInterface(desktopTarget, &kIID_CompositionTarget, (void **)&a->target));

    CHECK(GetPropertyValueStatics());
    CHECK(CreateNoiseBrush(a->compositor, a->d3dDevice, &noiseBrush));
    CHECK(noiseBrush->lpVtbl->QueryInterface(noiseBrush, &kIID_CompositionBrush, (void **)&a->noiseBrushBase));

    CHECK(compositor3->lpVtbl->CreateHostBackdropBrush(compositor3, &backdropBrush));
    CHECK(backdropBrush->lpVtbl->QueryInterface(backdropBrush, &kIID_CompositionBrush, (void **)&a->backdropBrushBase));

    CHECK(MakeString(L"Backdrop", &a->backdropName));
    CHECK(MakeString(L"Noise", &a->noiseName));

    CHECK(compositorInterop->lpVtbl->CreateCompositionSurfaceForSwapChain(compositorInterop, (IUnknown *)a->swapChain, &swapChainSurface));
    CHECK(a->compositor->lpVtbl->CreateSurfaceBrushWithSurface(a->compositor, swapChainSurface, &swapChainBrush));
    // Draw the swapchain one to one, top left, never scaled. The swapchain
    // buffer and the visual are on different clocks: a live resize leaves them
    // disagreeing by a frame, and the default Fill stretch turns that into the
    // content pulsing bigger and smaller. With no stretch a stale frame simply
    // does not reach the new edge (the acrylic shows through there for one
    // frame) instead of being scaled.
    CHECK(swapChainBrush->lpVtbl->put_Stretch(swapChainBrush, COMPOSITIONSTRETCH_NONE));
    CHECK(swapChainBrush->lpVtbl->put_HorizontalAlignmentRatio(swapChainBrush, 0.0f));
    CHECK(swapChainBrush->lpVtbl->put_VerticalAlignmentRatio(swapChainBrush, 0.0f));
    CHECK(swapChainBrush->lpVtbl->QueryInterface(swapChainBrush, &kIID_CompositionBrush, (void **)&swapChainBrushBase));

    fill.X = 1.0f; fill.Y = 1.0f;

    CHECK(a->compositor->lpVtbl->CreateContainerVisual(a->compositor, &root));
    CHECK(root->lpVtbl->QueryInterface(root, &kIID_Visual, (void **)&rootVisual));
    CHECK(root->lpVtbl->QueryInterface(root, &kIID_Visual2, (void **)&rootVisual2));
    CHECK(rootVisual2->lpVtbl->put_RelativeSizeAdjustment(rootVisual2, fill));

    CHECK(a->compositor->lpVtbl->CreateSpriteVisual(a->compositor, &a->acrylicVisual));
    CHECK(a->acrylicVisual->lpVtbl->QueryInterface(a->acrylicVisual, &kIID_Visual, (void **)&acrylicVisualBase));
    CHECK(a->acrylicVisual->lpVtbl->QueryInterface(a->acrylicVisual, &kIID_Visual2, (void **)&acrylicVisual2));
    CHECK(acrylicVisual2->lpVtbl->put_RelativeSizeAdjustment(acrylicVisual2, fill));

    // Tracks the window, so it is always exactly the window size with no commit
    // lag of its own. The stretch-none brush above is what keeps the swapchain
    // from being scaled when its buffer briefly lags this size during a resize.
    CHECK(a->compositor->lpVtbl->CreateSpriteVisual(a->compositor, &contentVisual));
    CHECK(contentVisual->lpVtbl->put_Brush(contentVisual, swapChainBrushBase));
    CHECK(contentVisual->lpVtbl->QueryInterface(contentVisual, &kIID_Visual, (void **)&contentVisualBase));
    CHECK(contentVisual->lpVtbl->QueryInterface(contentVisual, &kIID_Visual2, (void **)&contentVisual2));
    CHECK(contentVisual2->lpVtbl->put_RelativeSizeAdjustment(contentVisual2, fill));

    CHECK(buildAcrylicBrush(a));

    CHECK(root->lpVtbl->get_Children(root, &children));
    CHECK(children->lpVtbl->InsertAtBottom(children, acrylicVisualBase));
    CHECK(children->lpVtbl->InsertAtTop(children, contentVisualBase));
    CHECK(a->target->lpVtbl->put_Root(a->target, rootVisual));
    return S_OK;
}

bool _glfwWin32AcrylicCreate(_GLFWwindow *window) {
    _GLFWacrylicWin32 *a;
    IDXGIDevice *dxgiDevice = NULL;
    IDXGIAdapter *dxgiAdapter = NULL;
    IDXGIFactory2 *dxgiFactory = NULL;
    DXGI_SWAP_CHAIN_DESC1 swapDesc;
    RECT client;
    bool ok = false;

    if (window->win32.acrylic) return true;

    a = calloc(1, sizeof(_GLFWacrylicWin32));
    if (!a) return false;
    a->tintRgb = 0x1e1e1e;
    a->tintOpacity = 1.0f;

    GetClientRect(window->win32.handle, &client);
    a->width = client.right - client.left;
    a->height = client.bottom - client.top;
    if (a->width < 1) a->width = 1;
    if (a->height < 1) a->height = 1;

    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) { /* already initialised is fine */ }

    // Windows.UI.Composition refuses to activate on a thread with no
    // DispatcherQueue, and says so as a bare E_ACCESSDENIED from
    // RoActivateInstance. kitty's main thread pumps its own message loop, so
    // the queue only has to exist, not run anything. A failure here is not
    // fatal on its own: the thread may already have a queue we did not make,
    // in which case RoActivateInstance below succeeds anyway.
    if (!g_dispatcherController) {
        DispatcherQueueOptionsC options;
        options.dwSize = sizeof(options);
        options.threadType = 2;     // DQTYPE_THREAD_CURRENT
        options.apartmentType = 0;  // DQTAT_COM_NONE
        HRESULT hr = CreateDispatcherQueueController(options, (void **)&g_dispatcherController);
        if (FAILED(hr)) {
            ADBG("CreateDispatcherQueueController: 0x%08x (continuing)", (unsigned)hr);
            g_dispatcherController = NULL;
        }
    }

    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 NULL, 0, D3D11_SDK_VERSION, &a->d3dDevice, NULL, NULL))) goto fail;
    if (FAILED(ID3D11Device_QueryInterface(a->d3dDevice, &IID_IDXGIDevice, (void **)&dxgiDevice))) goto fail;
    if (FAILED(IDXGIDevice_GetAdapter(dxgiDevice, &dxgiAdapter))) goto fail;
    if (FAILED(IDXGIAdapter_GetParent(dxgiAdapter, &IID_IDXGIFactory2, (void **)&dxgiFactory))) goto fail;

    // Premultiplied alpha is what lets the terminal blend onto the acrylic
    // beneath it rather than replace it.
    ZeroMemory(&swapDesc, sizeof(swapDesc));
    swapDesc.Width = (UINT) a->width;
    swapDesc.Height = (UINT) a->height;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    {
        HRESULT hr = IDXGIFactory2_CreateSwapChainForComposition(dxgiFactory, (IUnknown *)a->d3dDevice,
                                                                 &swapDesc, NULL, &a->swapChain);
        if (FAILED(hr)) { ADBG("CreateSwapChainForComposition failed: 0x%08x", (unsigned)hr); goto fail; }
    }

    if (FAILED(createCompositionTree(a, window->win32.handle))) goto fail;

    window->win32.acrylic = a;
    ok = true;
    ADBG("composition tree created, %dx%d", a->width, a->height);

fail:
    if (dxgiFactory) IDXGIFactory2_Release(dxgiFactory);
    if (dxgiAdapter) IDXGIAdapter_Release(dxgiAdapter);
    if (dxgiDevice) IDXGIDevice_Release(dxgiDevice);
    if (!ok) {
        if (a->swapChain) IDXGISwapChain1_Release(a->swapChain);
        if (a->d3dDevice) ID3D11Device_Release(a->d3dDevice);
        free(a);
    }
    return ok;
}

void _glfwWin32AcrylicDestroy(_GLFWwindow *window) {
    _GLFWacrylicWin32 *a = acrylic_of(window);
    if (!a) return;
    window->win32.acrylic = NULL;

    unbindBackBuffer(a);
    if (a->interopDevice && a->gl.CloseDevice) a->gl.CloseDevice(a->interopDevice);
    if (a->acrylicBrush) a->acrylicBrush->lpVtbl->Release(a->acrylicBrush);
    if (a->acrylicVisual) a->acrylicVisual->lpVtbl->Release(a->acrylicVisual);
    if (a->noiseBrushBase) a->noiseBrushBase->lpVtbl->Release(a->noiseBrushBase);
    if (a->backdropBrushBase) a->backdropBrushBase->lpVtbl->Release(a->backdropBrushBase);
    if (a->target) a->target->lpVtbl->Release(a->target);
    if (a->compositor) a->compositor->lpVtbl->Release(a->compositor);
    if (a->backdropName) WindowsDeleteString(a->backdropName);
    if (a->noiseName) WindowsDeleteString(a->noiseName);
    if (a->swapChain) IDXGISwapChain1_Release(a->swapChain);
    if (a->d3dDevice) ID3D11Device_Release(a->d3dDevice);
    // g_dispatcherController is deliberately not released: it belongs to the
    // thread and the next window needs it.
    free(a);
}

void _glfwWin32AcrylicSetTint(_GLFWwindow *window, unsigned int rgb, float opacity) {
    _GLFWacrylicWin32 *a = acrylic_of(window);
    if (!a) return;
    if (a->tintRgb == rgb && a->tintOpacity == opacity) return;
    a->tintRgb = rgb;
    a->tintOpacity = opacity;
    if (a->acrylicVisual) buildAcrylicBrush(a);
}

// Everything that needs a current GL context lives here, not in Create.
// wglGetProcAddress returns NULL without one, and _glfwRefreshContextAttribs
// leaves no context current after a window is built, so Create runs with none.
bool _glfwWin32AcrylicBindFramebuffer(_GLFWwindow *window) {
    _GLFWacrylicWin32 *a = acrylic_of(window);
    if (!a || a->bound) return a != NULL && a->bound;

    if (!loadGLEntryPoints(&a->gl)) { ADBG("WGL_NV_DX_interop2 or FBO entry points missing"); return false; }

    if (!a->interopDevice) {
        a->interopDevice = a->gl.OpenDevice(a->d3dDevice);
        if (!a->interopDevice) { ADBG("wglDXOpenDeviceNV failed: %lu", GetLastError()); return false; }
    }

    if (!a->glTexture) a->gl.GenTextures(1, &a->glTexture);
    if (!a->glFbo) a->gl.GenFramebuffers(1, &a->glFbo);

    a->bound = bindBackBuffer(a);
    ADBG("bind framebuffer: %s", a->bound ? "ok" : "FAILED");
    return a->bound;
}

// Only records the new size. WM_SIZE arrives with no GL context current, and
// unregistering the interop object needs one -- without it the swapchain keeps
// a live reference to the back buffer and ResizeBuffers fails, which used to
// leave the window with nothing presenting at all. applyPendingResize does the
// real work from Present, where the context is guaranteed current.
void _glfwWin32AcrylicResize(_GLFWwindow *window, int width, int height) {
    _GLFWacrylicWin32 *a = acrylic_of(window);
    if (!a || width < 1 || height < 1) return;
    if (width == a->width && height == a->height) { a->pendingWidth = a->pendingHeight = 0; return; }
    a->pendingWidth = width;
    a->pendingHeight = height;
}

static void applyPendingResize(_GLFWacrylicWin32 *a) {
    const int width = a->pendingWidth, height = a->pendingHeight;
    if (!width || !height) return;
    a->pendingWidth = a->pendingHeight = 0;

    unbindBackBuffer(a);
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(a->swapChain, 2, (UINT)width, (UINT)height,
                                               DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        ADBG("ResizeBuffers %dx%d failed: 0x%08x", width, height, (unsigned)hr);
        a->bound = bindBackBuffer(a);  // keep presenting at the old size
        return;
    }
    a->width = width;
    a->height = height;
    a->bound = bindBackBuffer(a);
    ADBG("resized to %dx%d: %s", width, height, a->bound ? "ok" : "FAILED");
}

void _glfwWin32AcrylicPresent(_GLFWwindow *window) {
    _GLFWacrylicWin32 *a = acrylic_of(window);
    if (!a || !a->bound) return;

    applyPendingResize(a);
    if (!a->bound) return;

    // kitty has just finished drawing into the default framebuffer, which on a
    // window with no redirection surface presents nowhere. Copy it into the
    // swapchain back buffer, which is what the composition tree shows.
    //
    // The destination Y range is inverted on purpose. OpenGL puts the origin at
    // the bottom left, a DXGI texture puts it at the top left, so a
    // like-for-like copy arrives upside down.
    {
        int previous = 0;
        a->gl.GetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &previous);
        a->gl.BindFramebuffer(ACRYLIC_GL_READ_FRAMEBUFFER, 0);
        a->gl.BindFramebuffer(ACRYLIC_GL_DRAW_FRAMEBUFFER, a->glFbo);
        a->gl.BlitFramebuffer(0, 0, a->width, a->height,
                              0, a->height, a->width, 0,
                              ACRYLIC_GL_COLOR_BUFFER_BIT, ACRYLIC_GL_NEAREST);
        a->gl.BindFramebuffer(ACRYLIC_GL_FRAMEBUFFER, (unsigned int) previous);
    }

    a->gl.Flush();
    a->gl.UnlockObjects(a->interopDevice, 1, &a->interopTexture);
    a->locked = false;

    IDXGISwapChain1_Present(a->swapChain, 0, 0);

    a->gl.LockObjects(a->interopDevice, 1, &a->interopTexture);
    a->locked = true;
}