#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "canvas.h"
#include "common.h"
#include "icons.h"

// An app from the Start menu's full list (desktop and Store apps alike).
struct InstalledApp {
    std::wstring name;
    std::wstring target;  // shell:AppsFolder\<id>: opens it the way the Start menu does
};

// Every app in Start, sorted by name, without uninstallers, help files and web links. Needs COM.
std::vector<InstalledApp> ListInstalledApps();

// Windows' own pickers. Each returns false if cancelled. `target` is a file system path where there is one, else
// the shell's name for the item (so libraries and This PC work too). Need COM.
bool PickFolder(HWND owner, std::wstring& name, std::wstring& target);
bool PickProgram(HWND owner, std::wstring& name, std::wstring& target);

// The app picker as layout and painting only, so it can be drawn and hit-tested without a window.
class AppPickerView {
public:
    enum class Part { None, Close, Title, Search, Row, Browse };

    void Build(float scale);
    void SetApps(const std::vector<InstalledApp>* apps);
    void SetFilter(const std::wstring& filter);  // keeps names containing it; selects the first
    const std::wstring& Filter() const { return filter_; }
    int Matches() const { return int(matches_.size()); }
    const InstalledApp* Match(int match) const;
    int VisibleRows() const;
    int FirstRow() const { return scroll_; }
    void Scroll(int rows);
    void MoveSelection(int delta);  // and keeps it in view
    int Selected() const { return selected_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    Part HitTest(POINT point, int* match) const;
    void Paint(Canvas& canvas, Part hover, int hoverMatch,
               const std::function<const IconImage*(const InstalledApp&)>& icon) const;

private:
    RECT RowRect(int visibleIndex) const;

    float scale_ = 1.0f;
    int width_ = 0;
    int height_ = 0;
    RECT close_ = {};
    RECT search_ = {};
    RECT list_ = {};
    RECT browse_ = {};
    int rowHeight_ = 0;
    const std::vector<InstalledApp>* apps_ = nullptr;
    std::wstring filter_;
    std::vector<int> matches_;  // indices into *apps_
    int scroll_ = 0;
    int selected_ = -1;  // index into matches_
};

// The "Add an app" window: type to search, arrow keys and Enter, or click. Closes when you click elsewhere.
class AppPicker {
public:
    using Chosen = std::function<void(const InstalledApp&)>;
    bool Init(HINSTANCE instance, Chosen chosen);
    void Show(HMONITOR monitor);
    void Close();
    bool IsOpen() const { return window_ != nullptr; }

private:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Repaint();
    void Choose(int match);
    void Browse();
    const IconImage* IconFor(const InstalledApp& app);

    HINSTANCE instance_ = nullptr;
    Chosen chosen_;
    HWND window_ = nullptr;
    AppPickerView view_;
    Canvas canvas_;
    std::vector<InstalledApp> apps_;
    std::map<std::wstring, IconImage> icons_;
    float scale_ = 1.0f;
    AppPickerView::Part hover_ = AppPickerView::Part::None;
    int hoverMatch_ = -1;
    bool tracking_ = false;
    bool browsing_ = false;  // Windows' file picker is open over this one
};
