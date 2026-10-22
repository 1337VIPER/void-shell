#pragma once

#include <hstring.h>
#include <objectarray.h>
#include <shobjidl_core.h>

#include <string>
#include <vector>

#include "common.h"

namespace vdcom {

// Undocumented shell interfaces for Windows 11 24H2 and later (build 26100+, including 25H2's 26200).
// Microsoft changes these IIDs whenever the layout changes, so on another build QueryService fails cleanly
// instead of calling the wrong method. Layout checked against MScholtes/VirtualDesktop (VirtualDesktop11-24H2.cs)
// and Ciantic/VirtualDesktopAccessor (src/interfaces.rs).
MIDL_INTERFACE("3F07F4BE-B107-441A-AF0F-39D82529072C")
IVirtualDesktop : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown* view, BOOL* visible) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING* name) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING* path) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsRemote(BOOL* remote) = 0;
};

MIDL_INTERFACE("53F5CA0B-158F-4124-900C-057158060B27")
IVirtualDesktopManagerInternal : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown* view, IVirtualDesktop* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown* view, BOOL* canMove) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IVirtualDesktop** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray** desktops) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IVirtualDesktop* from, UINT direction,
                                                         IVirtualDesktop** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IVirtualDesktop* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(IVirtualDesktop* desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(IVirtualDesktop** desktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(IVirtualDesktop* desktop, UINT index) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(IVirtualDesktop* desktop, IVirtualDesktop* fallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID* id, IVirtualDesktop** desktop) = 0;
    // The interface continues, but nothing after FindDesktop is used, so it isn't declared.
};

class DECLSPEC_UUID("C2F03A33-21F5-47FA-B4BB-156362A2F239") ImmersiveShell;
class DECLSPEC_UUID("C5E0CDCA-7B6E-41B2-9FC4-D93975CC467B") VirtualDesktopManagerInternalService;
// The documented CLSID_VirtualDesktopManager, for the public IVirtualDesktopManager.
class DECLSPEC_UUID("AA509086-5CA9-4C25-8F95-589D3C07B48A") PublicVirtualDesktopManager;

}  // namespace vdcom

struct DesktopInfo {
    GUID id = {};
    std::wstring name;       // empty until the user renames the desktop
    std::wstring wallpaper;  // empty means the global wallpaper
};

std::wstring GuidToString(const GUID& id);

// Windows' virtual desktops. Lists, creates and switches them through the shell's internal COM interface. If that
// interface doesn't match this Windows build, falls back to reading the registry and pressing the built-in
// shortcuts (Win+Ctrl+D, Win+Ctrl+Left/Right).
class VirtualDesktops {
public:
    void Init();
    const char* Mode() const { return manager_ ? "shell COM API" : "registry + shortcuts"; }
    bool List(std::vector<DesktopInfo>& desktops, GUID& current);
    bool Current(GUID& current);
    // The COM API creates the desktop in the background; the shortcut fallback also switches to it.
    bool Create(GUID& created);
    bool CreateAlsoSwitches() const { return !manager_; }
    bool Switch(const GUID& id);
    // Moves a desktop to a position in the order. Needs the COM API.
    bool Move(const GUID& id, int index);
    // Removes a desktop; Windows moves its windows to `fallback`. Needs the COM API.
    bool Remove(const GUID& id, const GUID& fallback);
    bool CanRearrange() const { return manager_ != nullptr; }
    // Documented API: puts one of this process's windows on a desktop.
    bool MoveWindowTo(HWND window, const GUID& id);
    // Documented API: which desktop a window is on.
    bool WindowDesktop(HWND window, GUID& id);

    static bool ListFromRegistry(std::vector<DesktopInfo>& desktops, GUID& current);

private:
    bool ListWithCom(std::vector<DesktopInfo>& desktops, GUID& current);

    ComPtr<vdcom::IVirtualDesktopManagerInternal> manager_;
    ComPtr<IVirtualDesktopManager> publicManager_;
};
