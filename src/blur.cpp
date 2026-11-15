#include "blur.h"
#include "features.h"
#include "log.h"
#include <d3d9.h>
#include <algorithm>
#include <cstdlib>

// Fixed-function separable gaussian: each pass draws the source five times with the linear-filter
// offsets of a 9-tap kernel, weighted through the texture factor and added together. No shaders,
// so it needs nothing beyond plain Direct3D 9 (and works the same under DXVK).
namespace Blur {

namespace {

IDirect3DTexture9* g_texA = nullptr;    // quarter size, ends up holding the result
IDirect3DTexture9* g_texB = nullptr;    // quarter size, ping-pong
IDirect3DSurface9* g_half = nullptr;    // half size, smoother downsample than one big step
IDirect3DSurface9* g_resolve = nullptr; // full size, only when the back buffer is multisampled
UINT g_w = 0, g_h = 0;                  // back buffer size the targets were made for
bool g_requested = false, g_ready = false, g_disabled = false;
bool g_handedOut = false;             // this frame's draw list already points at g_texA
int  g_failures = 0;

struct Vtx { float x, y, z, rhw, u, v; };

template <typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

void ReleaseTargets()
{
    SafeRelease(g_texA); SafeRelease(g_texB); SafeRelease(g_half); SafeRelease(g_resolve);
    g_w = g_h = 0;
    g_ready = false;
}

bool CreateTargets(IDirect3DDevice9* dev, UINT w, UINT h)
{
    ReleaseTargets();
    UINT qw = std::max(1u, w / 4), qh = std::max(1u, h / 4);
    if (FAILED(dev->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_texA, nullptr)) ||
        FAILED(dev->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_texB, nullptr)) ||
        FAILED(dev->CreateRenderTarget(std::max(1u, w / 2), std::max(1u, h / 2), D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_half, nullptr))) {
        ReleaseTargets();
        return false;
    }
    g_w = w; g_h = h;
    return true;
}

void SetBlurStates(IDirect3DDevice9* dev)
{
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

// One direction of the gaussian: src -> dst, (dx, dy) = 1 for the axis to blur along.
bool Pass(IDirect3DDevice9* dev, IDirect3DTexture9* src, IDirect3DTexture9* dst, float dx, float dy, float spread)
{
    IDirect3DSurface9* surf = nullptr;
    if (FAILED(dst->GetSurfaceLevel(0, &surf))) return false;
    HRESULT hr = dev->SetRenderTarget(0, surf);
    surf->Release();
    if (FAILED(hr)) return false;
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetTexture(0, src);

    const float qw = (float)std::max(1u, g_w / 4), qh = (float)std::max(1u, g_h / 4);
    static const float kOffset[3] = { 0.0f, 1.3846154f, 3.2307692f };
    static const int   kWeight[3] = { 57, 81, 18 };       // 0.227, 0.316, 0.070 in 1/255 steps (sum 255)
    for (int i = -2; i <= 2; ++i) {
        int k = abs(i);
        float o = (i < 0 ? -kOffset[k] : kOffset[k]) * spread;
        float du = dx * o / qw, dv = dy * o / qh;
        DWORD wgt = (DWORD)kWeight[k];
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(wgt, wgt, wgt, wgt));
        Vtx q[4] = {
            { -0.5f,      -0.5f,      0, 1, du,        dv },
            { qw - 0.5f,  -0.5f,      0, 1, 1.0f + du, dv },
            { -0.5f,      qh - 0.5f,  0, 1, du,        1.0f + dv },
            { qw - 0.5f,  qh - 0.5f,  0, 1, 1.0f + du, 1.0f + dv },
        };
        if (FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx)))) return false;
    }
    return true;
}

bool Downsample(IDirect3DDevice9* dev, IDirect3DSurface9* bb, const D3DSURFACE_DESC& d)
{
    IDirect3DSurface9* quarter = nullptr;
    if (FAILED(g_texA->GetSurfaceLevel(0, &quarter))) return false;
    IDirect3DSurface9* src = bb;
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        // A multisampled back buffer can only be resolved at its own size, then shrunk.
        if (!g_resolve && FAILED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &g_resolve, nullptr))) {
            quarter->Release();
            return false;
        }
        if (FAILED(dev->StretchRect(bb, nullptr, g_resolve, nullptr, D3DTEXF_NONE))) { quarter->Release(); return false; }
        src = g_resolve;
    }
    bool ok = SUCCEEDED(dev->StretchRect(src, nullptr, g_half, nullptr, D3DTEXF_LINEAR)) &&
              SUCCEEDED(dev->StretchRect(g_half, nullptr, quarter, nullptr, D3DTEXF_LINEAR));
    quarter->Release();
    return ok;
}

} // namespace

void Request() { g_requested = true; }

void* Texture()
{
    if (!g_ready || g_disabled || !g_toggles.blur) return nullptr;
    g_handedOut = true;
    return g_texA;
}

void Release() { ReleaseTargets(); }

void Capture(void* device)
{
    bool wanted = device && g_requested && g_toggles.blur && !g_disabled;
    bool handed = g_handedOut;            // if so, the targets must stay alive until ImGui has drawn
    g_requested = false;
    g_handedOut = false;
    if (!wanted) { g_ready = false; return; }
    auto* dev = (IDirect3DDevice9*)device;

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) { g_ready = false; return; }
    D3DSURFACE_DESC d{};
    bb->GetDesc(&d);
    if ((d.Width != g_w || d.Height != g_h) && handed) { bb->Release(); g_ready = false; return; }   // recreate next frame
    if ((d.Width != g_w || d.Height != g_h) && !CreateTargets(dev, d.Width, d.Height)) {
        bb->Release();
        g_disabled = true;
        Log("Blur: could not create render targets, using a plain tint");
        return;
    }

    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb))) { bb->Release(); g_ready = false; return; }
    IDirect3DSurface9* oldRT = nullptr;
    IDirect3DSurface9* oldDS = nullptr;
    dev->GetRenderTarget(0, &oldRT);
    dev->GetDepthStencilSurface(&oldDS);   // may legitimately be none

    bool ok = Downsample(dev, bb, d);
    if (ok && SUCCEEDED(dev->BeginScene())) {
        SetBlurStates(dev);
        ok = Pass(dev, g_texA, g_texB, 1, 0, 1.0f) && Pass(dev, g_texB, g_texA, 0, 1, 1.0f) &&
             Pass(dev, g_texA, g_texB, 1, 0, 2.0f) && Pass(dev, g_texB, g_texA, 0, 1, 2.0f);
        dev->SetTexture(0, nullptr);
        dev->EndScene();
    } else {
        ok = false;
    }

    dev->SetRenderTarget(0, oldRT);
    dev->SetDepthStencilSurface(oldDS);
    sb->Apply();
    sb->Release();
    if (oldRT) oldRT->Release();
    if (oldDS) oldDS->Release();
    bb->Release();

    g_ready = ok;
    if (ok) { g_failures = 0; return; }
    if (++g_failures >= 5) {
        g_disabled = true;
        if (!handed) ReleaseTargets();
        Log("Blur: failed %d times in a row, using a plain tint", g_failures);
    }
}

}
