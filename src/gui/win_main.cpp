// PressPNG GUI - native Win32 interface with a single resizable image preview.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "presspng/compressor.h"
#include "presspng/codec_factory.h"
#include "presspng/format_bytes.h"
#include "presspng/image_loader.h"
#include "presspng/preview.h"
#include "presspng/target_size_solver.h"
#include "registry_settings_store.h"

#pragma comment(lib, "uxtheme.lib")


namespace {

namespace fs = std::filesystem;

// Control IDs
constexpr int kIdPathEdit = 1001;
constexpr int kIdBrowse = 1002;
constexpr int kIdSizeEdit = 1003;
constexpr int kIdFormatCombo = 1004;
constexpr int kIdCompress = 1005;
constexpr int kIdCancel = 1006;
constexpr int kIdStatus = 1007;
constexpr int kIdProgress = 1008;
constexpr int kIdOpenFolder = 1010;
constexpr int kIdCopyPath = 1011;
constexpr int kIdZoom = 1012;
constexpr int kIdOutputPath = 1013;
constexpr int kIdSaveAs = 1014;
constexpr int kIdPreviewMode = 1015;

// Custom messages
constexpr UINT kMsgProgress = WM_APP + 1;
constexpr UINT kMsgDone = WM_APP + 2;
constexpr UINT kMsgPreview = WM_APP + 3;

// Preview view
struct PreviewView {
    bool fit = true;      // fit to pane vs 100%
    int panX = 0, panY = 0;
    bool dragging = false;
    POINT lastMouse{};
};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (needed <= 0) return std::wstring(text.begin(), text.end());
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), &out[0], needed);
    return out;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), &out[0], needed,
                        nullptr, nullptr);
    return out;
}

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, &text[0], length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void setText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

// Create a 32bpp DIB from RGBA pixels (BGRA for GDI).
HBITMAP makeDIB(const presspng::Image& image) {
    if (image.empty()) return nullptr;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image.width();
    bmi.bmiHeader.biHeight = -image.height(); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!bmp || !bits) return nullptr;

    auto* dst = static_cast<std::uint32_t*>(bits);
    const std::uint8_t* src = image.pixels();
    const int total = image.width() * image.height();
    for (int i = 0; i < total; ++i) {
        const std::uint8_t r = src[i * 4 + 0];
        const std::uint8_t g = src[i * 4 + 1];
        const std::uint8_t b = src[i * 4 + 2];
        const std::uint8_t a = src[i * 4 + 3];
        // BGRA with simple alpha over white (checkerboard added in paint)
        const std::uint8_t br = static_cast<std::uint8_t>((r * a + 255 * (255 - a)) / 255);
        const std::uint8_t bg = static_cast<std::uint8_t>((g * a + 255 * (255 - a)) / 255);
        const std::uint8_t bb = static_cast<std::uint8_t>((b * a + 255 * (255 - a)) / 255);
        dst[i] = (0xFFu << 24) | (static_cast<std::uint32_t>(br) << 16) |
                 (static_cast<std::uint32_t>(bg) << 8) |
                 static_cast<std::uint32_t>(bb);
    }
    return bmp;
}

struct PreviewState {
    HBITMAP bitmap = nullptr;
    int srcW = 0, srcH = 0;
    std::string message;
    PreviewView view;
};

// Messages for preview results
struct PreviewMsg {
    bool ok = false;
    presspng::Image thumbnail;
    std::string message;
    bool estimate = false;
    bool targetMet = false;
    std::uint64_t outputBytes = 0;
    unsigned long long generation = 0;
};

struct AppState {
    HWND window = nullptr;
    HWND pathEdit = nullptr, browseBtn = nullptr, sizeEdit = nullptr;
    HWND formatCombo = nullptr, compressBtn = nullptr, cancelBtn = nullptr;
    HWND status = nullptr, progress = nullptr;
    HWND zoomBtn = nullptr;
    HWND openFolder = nullptr, copyPath = nullptr;
    HWND paneOrig = nullptr, paneComp = nullptr;
    HWND outputEdit = nullptr, previewMode = nullptr, saveAsBtn = nullptr;

    bool busy = false;
    bool closeWhenDone = false;
    std::vector<fs::path> batchFiles;
    size_t batchIndex = 0;
    presspng::CompressionRequest request;
    fs::path previewPath;
    std::shared_ptr<presspng::CancelToken> estimateCancel;
    presspng::CompressionResult lastResult;
    presspng::CancelToken cancelToken;

    PreviewState orig, comp;
    std::atomic<unsigned long long> previewGen{0};
    unsigned long long origGen = 0, compGen = 0;
    std::unique_ptr<presspng::ISettingsStore> settings;
    presspng::AppSettings appSettings;
};

HWND makeControl(HWND parent, DWORD exStyle, const wchar_t* cls, const wchar_t* text,
                 DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
    return c;
}

void postProgress(HWND window, const std::string& text) {
    auto* payload = new std::wstring(utf8ToWide(text));
    if (!PostMessageW(window, kMsgProgress, 0, reinterpret_cast<LPARAM>(payload)))
        delete payload;
}

// Worker contexts
struct CompContext {
    HWND window;
    presspng::Compressor compressor;
    presspng::CompressionRequest request;
    presspng::CancelToken* cancel = nullptr;
};

DWORD WINAPI compressWorker(LPVOID param) {
    auto* ctx = static_cast<CompContext*>(param);
    const auto result = ctx->compressor.compress(
        ctx->request, [w = ctx->window](const std::string& s) { postProgress(w, s); },
        ctx->cancel);
    auto* payload = new presspng::CompressionResult(result);
    if (!PostMessageW(ctx->window, kMsgDone, 0, reinterpret_cast<LPARAM>(payload)))
        delete payload;
    delete ctx;
    return 0;
}

struct PrevContext {
    HWND window;
    HWND targetPane;
    std::unique_ptr<presspng::IPreviewSource> source;
    unsigned long long gen = 0;
    int maxW = 256, maxH = 192;
    bool estimate = false;
    std::uint64_t targetBytes = 0;
    presspng::UserFormat format = presspng::UserFormat::Keep;
    std::shared_ptr<presspng::CancelToken> cancel;
};

DWORD WINAPI previewWorker(LPVOID param) {
    auto* ctx = static_cast<PrevContext*>(param);
    auto* msg = new PreviewMsg();
    msg->generation = ctx->gen;
    try {
        if (ctx->estimate) {
            const auto& file = static_cast<const presspng::FilePreviewSource&>(*ctx->source).path();
            const auto raw = presspng::ImageLoader::readFile(file);
            const auto inputFormat = presspng::ImageLoader::sniff(raw);
            auto selected = presspng::CodecFactory::create(ctx->format, inputFormat);
            std::vector<std::uint8_t> encoded;
            if (ctx->format == presspng::UserFormat::Keep && raw.size() <= ctx->targetBytes &&
                !selected.fellBack) {
                encoded = raw; // match the compressor's unchanged-copy fast path
                msg->targetMet = true;
            } else {
                presspng::Image source = presspng::ImageLoader::decode(raw);
                presspng::TargetSizeSolver solver;
                const auto solved = solver.solve(source, *selected.codec, ctx->targetBytes, {}, ctx->cancel.get());
                encoded = solved.bytes;
                msg->targetMet = solved.targetMet;
            }
            msg->outputBytes = encoded.size();
            auto decoded = presspng::ImageLoader::decode(encoded);
            const double scale = std::min({1.0, static_cast<double>(ctx->maxW) / decoded.width(),
                                           static_cast<double>(ctx->maxH) / decoded.height()});
            const int w = std::max(1, static_cast<int>(decoded.width() * scale));
            const int h = std::max(1, static_cast<int>(decoded.height() * scale));
            msg->thumbnail = w == decoded.width() && h == decoded.height()
                                 ? std::move(decoded) : decoded.resized(w, h);
            msg->ok = true;
            msg->estimate = true;
            msg->message = std::string(msg->targetMet ? "Estimated output: " : "Smallest output: ") +
                           presspng::formatBytes(msg->outputBytes);
        } else {
            presspng::PreviewService svc;
            auto frame = svc.make(*ctx->source, {ctx->maxW, ctx->maxH});
            msg->ok = frame.ok;
            msg->thumbnail = std::move(frame.thumbnail);
            msg->message = frame.message;
        }
    } catch (const std::exception& e) {
        msg->ok = false;
        msg->message = e.what();
    }
    if (!PostMessageW(ctx->window, kMsgPreview,
                      static_cast<WPARAM>(reinterpret_cast<INT_PTR>(ctx->targetPane)),
                      reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
    delete ctx;
    return 0;
}

void clearPreview(PreviewState& ps) {
    if (ps.bitmap) { DeleteObject(ps.bitmap); ps.bitmap = nullptr; }
    ps.srcW = ps.srcH = 0;
    ps.view.fit = true;
    ps.view.panX = ps.view.panY = 0;
    ps.view.dragging = false;
}

void paintPreviewPane(HWND pane, PreviewState& ps, const wchar_t* caption) {
    PAINTSTRUCT psPaint;
    HDC hdc = BeginPaint(pane, &psPaint);
    RECT rc; GetClientRect(pane, &rc);
    // checkerboard background
    HBRUSH bg1 = CreateSolidBrush(RGB(200, 200, 200));
    HBRUSH bg2 = CreateSolidBrush(RGB(160, 160, 160));
    int cs = 8;
    for (int y = rc.top; y < rc.bottom; y += cs) {
        for (int x = rc.left; x < rc.right; x += cs) {
            RECT r1{x,y,(x+cs < rc.right?x+cs:rc.right),(y+cs < rc.bottom?y+cs:rc.bottom)};
            if (((x / cs) + (y / cs)) % 2 == 0)
                FillRect(hdc, &r1, bg1);
            else
                FillRect(hdc, &r1, bg2);
        }
    }
    DeleteObject(bg1); DeleteObject(bg2);

    if (ps.bitmap && ps.srcW > 0 && ps.srcH > 0) {
        int pw = rc.right - rc.left;
        int ph = rc.bottom - rc.top - 18;
        double scale = 1.0;
        if (ps.view.fit) {
            scale = std::min(static_cast<double>(pw) / ps.srcW,
                              static_cast<double>(ph) / ps.srcH);
        } else {
            scale = 1.0;
        }
        int dw = static_cast<int>(ps.srcW * scale);
        int dh = static_cast<int>(ps.srcH * scale);
        int dx = (pw - dw) / 2 + ps.view.panX;
        int dy = 4 + (ph - dh) / 2 + ps.view.panY;

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, ps.bitmap));
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, ps.srcW, ps.srcH, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);

    } else if (!ps.message.empty()) {
        RECT tr = rc; tr.top += 40;
        DrawTextW(hdc, utf8ToWide(ps.message).c_str(), -1, &tr,
                  DT_CENTER | DT_WORDBREAK);
    }

    // caption
    RECT cr = rc; cr.bottom = 16; cr.left += 4;
    std::wstring cap = std::wstring(caption);
    if (ps.srcW > 0) {
        wchar_t tmp[64]; swprintf(tmp, 64, L" (%dx%d)", ps.srcW, ps.srcH);
        cap += tmp;
    }
    DrawTextW(hdc, cap.c_str(), -1, &cr, DT_LEFT | DT_SINGLELINE);

    // border
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
    HGDIOBJ oldp = SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(hdc, oldp);
    DeleteObject(pen);

    EndPaint(pane, &psPaint);
}

void requestPreview(AppState* state, HWND targetPane, const fs::path& path,
                    unsigned long long gen, int maxW, int maxH,
                    bool estimate = false, std::uint64_t targetBytes = 0,
                    presspng::UserFormat format = presspng::UserFormat::Keep) {
    if (estimate) {
        if (state->estimateCancel) state->estimateCancel->cancel();
        state->estimateCancel = std::make_shared<presspng::CancelToken>();
    }
    auto* ctx = new PrevContext{state->window, targetPane,
                                std::make_unique<presspng::FilePreviewSource>(path),
                                gen, maxW, maxH};
    ctx->estimate = estimate;
    ctx->targetBytes = targetBytes;
    ctx->format = format;
    ctx->cancel = estimate ? state->estimateCancel : nullptr;
    HANDLE t = CreateThread(nullptr, 0, previewWorker, ctx, 0, nullptr);
    if (t) CloseHandle(t);
    else delete ctx;
}

void startCompression(AppState* state) {
    if (state->busy) return;
    const std::wstring path = windowText(state->pathEdit);
    const std::wstring sizeText = windowText(state->sizeEdit);
    if (path.empty()) { SetWindowTextW(state->status, L"Choose an image file first."); return; }
    if (sizeText.empty() || std::wcstoull(sizeText.c_str(), nullptr, 10) == 0) {
        SetWindowTextW(state->status, L"Enter a target size in KB (e.g. 500)."); return;
    }
    int fi = static_cast<int>(SendMessageW(state->formatCombo, CB_GETCURSEL, 0, 0));
    presspng::UserFormat fmt = presspng::UserFormat::Keep;
    if (fi == 1) fmt = presspng::UserFormat::Jpeg;
    else if (fi == 2) fmt = presspng::UserFormat::Png;
    else if (fi == 3) fmt = presspng::UserFormat::Qoi;

    std::wstring destinationText = windowText(state->outputEdit);
    if (!destinationText.empty()) {
        fs::path destination(destinationText);
        if (fmt == presspng::UserFormat::Jpeg) destination.replace_extension(L".jpg");
        else if (fmt == presspng::UserFormat::Png) destination.replace_extension(L".png");
        else if (fmt == presspng::UserFormat::Qoi) destination.replace_extension(L".qoi");
        destinationText = destination.wstring();
        setText(state->outputEdit, destinationText);
    }
    if (!destinationText.empty() && fs::exists(fs::path(destinationText)) &&
        MessageBoxW(state->window, L"This output file already exists. Replace it?",
                    L"Confirm overwrite", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    state->batchFiles.clear();
    // if multiple files were dropped, compress all; else single
    // batch set by WM_DROPFILES handling? keep simple: use pathEdit as single or we can extend
    state->batchFiles.push_back(fs::path(path));
    state->batchIndex = 0;
    state->cancelToken.reset();

    // save settings
    state->appSettings.lastDirectory = fs::path(path).parent_path().u8string();
    state->appSettings.targetSizeText = wideToUtf8(sizeText);
    state->appSettings.format = fmt;
    if (state->settings) state->settings->save(state->appSettings);

    state->busy = true;
    if (state->estimateCancel) state->estimateCancel->cancel();
    state->compGen = ++state->previewGen; // discard any estimate still running
    EnableWindow(state->compressBtn, FALSE);
    EnableWindow(state->cancelBtn, TRUE);
    SendMessageW(state->progress, PBM_SETPOS, 0, 0);
    SendMessageW(state->progress, PBM_SETMARQUEE, TRUE, 30);
    SetWindowTextW(state->status, L"Starting...");
    EnableWindow(state->openFolder, FALSE);
    EnableWindow(state->copyPath, FALSE);

    auto* ctx = new CompContext{state->window, presspng::Compressor{}, {}, &state->cancelToken};
    ctx->request.input = state->batchFiles[0];
    ctx->request.outputDirectory.clear();
    ctx->request.outputPath = destinationText.empty() ? fs::path{} : fs::path(destinationText);
    ctx->request.targetBytes = std::wcstoull(sizeText.c_str(), nullptr, 10) * 1000ULL;
    ctx->request.format = fmt;
    state->request = ctx->request;

    HANDLE t = CreateThread(nullptr, 0, compressWorker, ctx, 0, nullptr);
    if (!t) { delete ctx; state->busy = false; EnableWindow(state->compressBtn, TRUE); EnableWindow(state->cancelBtn, FALSE); }
    else CloseHandle(t);
}

void showResult(AppState* state, const presspng::CompressionResult& result) {
    state->lastResult = result;
    if (!result.ok) {
        if (result.cancelled) SetWindowTextW(state->status, L"Cancelled.");
        else SetWindowTextW(state->status, utf8ToWide("Error: " + result.message).c_str());
        return;
    }
    const wchar_t* verdict = result.targetMet ? L"Target met" : L"Target not met; saved the smallest result";
    setText(state->outputEdit, result.outputPath.wstring());

    wchar_t line[512];
    swprintf(line, 512, L"%.1f KB -> %.1f KB (%.1f%% smaller)",
             result.inputBytes / 1000.0, result.outputBytes / 1000.0, result.reductionPercent);
    std::wstring summary = verdict;
    summary += L"  |  ";
    summary += line;
    SetWindowTextW(state->status, summary.c_str());
    // show in status area is fine; also enable actions
    EnableWindow(state->openFolder, TRUE);
    EnableWindow(state->copyPath, TRUE);

    // load compressed preview
    const auto g = ++state->previewGen;
    state->compGen = g;
    clearPreview(state->comp);
    {
        requestPreview(state, state->paneComp, result.outputPath, g, 16384, 16384);
    }
}

LRESULT CALLBACK PaneProc(HWND pane, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(GetParent(pane), GWLP_USERDATA));
    PreviewState* ps = nullptr;
    bool showingResult = false;
    if (pane == state->paneOrig) {
        showingResult = SendMessageW(state->previewMode, CB_GETCURSEL, 0, 0) == 1;
        ps = showingResult ? &state->comp : &state->orig;
    } else ps = &state->comp;

    switch (msg) {
        case WM_PAINT:
            if (ps) paintPreviewPane(pane, *ps,
                showingResult ? L"Compressed result" : L"Source image");
            return 0;
        case WM_LBUTTONDOWN:
            if (ps) { ps->view.dragging = true; ps->view.lastMouse = {LOWORD(lParam), HIWORD(lParam)}; SetCapture(pane); }
            return 0;
        case WM_MOUSEMOVE:
            if (ps && ps->view.dragging && !ps->view.fit) {
                POINT pt{LOWORD(lParam), HIWORD(lParam)};
                ps->view.panX += pt.x - ps->view.lastMouse.x;
                ps->view.panY += pt.y - ps->view.lastMouse.y;
                ps->view.lastMouse = pt;
                InvalidateRect(pane, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (ps) { ps->view.dragging = false; ReleaseCapture(); }
            return 0;
        case WM_MOUSEWHEEL:
            if (ps && !ps->view.fit) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                ps->view.panY += delta / 20;
                InvalidateRect(pane, nullptr, FALSE);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(pane, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<AppState*>(cs->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->window = window;
            state->settings = std::make_unique<presspng::RegistrySettingsStore>();
            state->appSettings = state->settings->load();

            INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
            InitCommonControlsEx(&icc);

            // register pane class
            WNDCLASSW pc{}; pc.lpfnWndProc = PaneProc; pc.hInstance = cs->hInstance;
            pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); pc.lpszClassName = L"PressPNGPanes";
            RegisterClassW(&pc);

            int y = 10;
            makeControl(window, 0, L"STATIC", L"Image file:", SS_LEFT, 10, y+4, 70, 16, 0);
            state->pathEdit = makeControl(window, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP|ES_AUTOHSCROLL, 84, y, 460, 24, kIdPathEdit);
            state->browseBtn = makeControl(window, 0, L"BUTTON", L"Choose image…", WS_TABSTOP|BS_PUSHBUTTON, 552, y, 110, 24, kIdBrowse);
            y += 30;

            makeControl(window, 0, L"STATIC", L"Save to:", SS_LEFT, 10, y+5, 70, 18, 0);
            state->outputEdit = makeControl(window, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP|ES_AUTOHSCROLL, 84, y, 460, 24, kIdOutputPath);
            state->saveAsBtn = makeControl(window, 0, L"BUTTON", L"Save as…", WS_TABSTOP|BS_PUSHBUTTON, 552, y, 110, 24, kIdSaveAs);
            y += 34;

            makeControl(window, 0, L"STATIC", L"Target:", SS_LEFT, 10, y+4, 52, 16, 0);
            state->sizeEdit = makeControl(window, WS_EX_CLIENTEDGE, L"EDIT", utf8ToWide(state->appSettings.targetSizeText).c_str(), WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL, 66, y, 56, 22, kIdSizeEdit);
            makeControl(window, 0, L"STATIC", L"KB", SS_LEFT, 128, y+4, 18, 16, 0);
            makeControl(window, 0, L"STATIC", L"Format:", SS_LEFT, 154, y+4, 48, 16, 0);
            state->formatCombo = makeControl(window, 0, L"COMBOBOX", L"", WS_TABSTOP|CBS_DROPDOWNLIST, 206, y, 110, 140, kIdFormatCombo);
            SendMessageW(state->formatCombo, CB_ADDSTRING, 0, (LPARAM)L"Keep original");
            SendMessageW(state->formatCombo, CB_ADDSTRING, 0, (LPARAM)L"JPEG");
            SendMessageW(state->formatCombo, CB_ADDSTRING, 0, (LPARAM)L"PNG");
            SendMessageW(state->formatCombo, CB_ADDSTRING, 0, (LPARAM)L"QOI");
            int fsel = 0; if(state->appSettings.format==presspng::UserFormat::Jpeg)fsel=1; else if(state->appSettings.format==presspng::UserFormat::Png)fsel=2; else if(state->appSettings.format==presspng::UserFormat::Qoi)fsel=3; SendMessageW(state->formatCombo, CB_SETCURSEL, fsel, 0);
            makeControl(window, 0, L"STATIC", L"Preview:", SS_LEFT, 324, y+4, 56, 16, 0);
            state->previewMode = makeControl(window, 0, L"COMBOBOX", L"", WS_TABSTOP|CBS_DROPDOWNLIST, 384, y, 140, 140, kIdPreviewMode);
            SendMessageW(state->previewMode, CB_ADDSTRING, 0, (LPARAM)L"Source image");
            SendMessageW(state->previewMode, CB_ADDSTRING, 0, (LPARAM)L"Target preview / result");
            SendMessageW(state->previewMode, CB_SETCURSEL, 0, 0);

            y += 30;
            state->compressBtn = makeControl(window, 0, L"BUTTON", L"Compress", WS_TABSTOP|BS_DEFPUSHBUTTON, 10, y, 78, 24, kIdCompress);
            state->cancelBtn = makeControl(window, 0, L"BUTTON", L"Cancel", WS_TABSTOP|BS_PUSHBUTTON, 94, y, 68, 24, kIdCancel);
            EnableWindow(state->cancelBtn, FALSE);
            state->zoomBtn = makeControl(window, 0, L"BUTTON", L"100%", WS_TABSTOP|BS_PUSHBUTTON, 170, y, 60, 24, kIdZoom);
            state->openFolder = makeControl(window, 0, L"BUTTON", L"Open output folder", WS_TABSTOP|BS_PUSHBUTTON, 490, y, 110, 24, kIdOpenFolder);
            state->copyPath = makeControl(window, 0, L"BUTTON", L"Copy path", WS_TABSTOP|BS_PUSHBUTTON, 606, y, 76, 24, kIdCopyPath);
            EnableWindow(state->openFolder, FALSE);
            EnableWindow(state->copyPath, FALSE);
            y += 32;

            state->progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD|WS_VISIBLE|PBS_MARQUEE, 10, y, 550, 10, window, (HMENU)kIdProgress, cs->hInstance, nullptr);
            SendMessageW(state->progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            y += 16;

            state->status = makeControl(window, 0, L"STATIC", L"Ready.", SS_LEFT|SS_PATHELLIPSIS, 10, y, 550, 16, kIdStatus);
            y += 22;

            state->paneOrig = CreateWindowExW(WS_EX_CLIENTEDGE, L"PressPNGPanes", nullptr, WS_CHILD|WS_VISIBLE, 10, y, 680, 430, window, nullptr, cs->hInstance, nullptr);
            state->paneComp = CreateWindowExW(0, L"PressPNGPanes", nullptr, WS_CHILD, 0, 0, 1, 1, window, nullptr, cs->hInstance, nullptr);

            DragAcceptFiles(window, TRUE);
            if (!state->appSettings.lastDirectory.empty()) {
                // keep last dir in mind via settings; user can browse
            }
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            int paneY = 174;
            int paneH = (std::max)(80, h - paneY - 12);
            int paneW = (std::max)(200, w - 24);
            SetWindowPos(state->paneOrig, nullptr, 10, paneY, paneW, paneH, SWP_NOZORDER);
            SetWindowPos(state->progress, nullptr, 10, 136, (std::max)(200, w - 24), 10, SWP_NOZORDER);
            SetWindowPos(state->status, nullptr, 10, 151, (std::max)(200, w - 24), 20, SWP_NOZORDER);
            SetWindowPos(state->pathEdit, nullptr, 84, 10, (std::max)(100, w - 370), 24, SWP_NOZORDER);
            SetWindowPos(state->browseBtn, nullptr, w - 276, 10, 110, 24, SWP_NOZORDER);
            SetWindowPos(state->outputEdit, nullptr, 84, 40, (std::max)(100, w - 370), 24, SWP_NOZORDER);
            SetWindowPos(state->saveAsBtn, nullptr, w - 276, 40, 110, 24, SWP_NOZORDER);
            SetWindowPos(state->openFolder, nullptr, w - 264, 102, 110, 24, SWP_NOZORDER);
            SetWindowPos(state->copyPath, nullptr, w - 148, 102, 76, 24, SWP_NOZORDER);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 720;
            info->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 1) {
                int id = LOWORD(wParam);
                if (id == kIdBrowse) {
                    wchar_t file[MAX_PATH] = L"";
                    OPENFILENAMEW dlg{}; dlg.lStructSize = sizeof(dlg); dlg.hwndOwner = window;
                    dlg.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.qoi)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.qoi\0All files (*.*)\0*.*\0";
                    dlg.lpstrFile = file; dlg.nMaxFile = MAX_PATH; dlg.lpstrTitle = L"Choose an image"; dlg.Flags = OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
                    const std::wstring initialDir = utf8ToWide(state->appSettings.lastDirectory);
                    if (!initialDir.empty()) dlg.lpstrInitialDir = initialDir.c_str();
                    if (GetOpenFileNameW(&dlg)) {
                        state->appSettings.lastDirectory = fs::path(file).parent_path().u8string();
                        state->previewPath = fs::path(file);
                        setText(state->outputEdit, L"");
                        state->lastResult.ok = false;
                        EnableWindow(state->openFolder, FALSE);
                        EnableWindow(state->copyPath, FALSE);
                        SendMessageW(state->previewMode, CB_SETCURSEL, 0, 0);
                        setText(state->pathEdit, file);
                        SetWindowTextW(state->status, L"Ready.");
                        const auto g = ++state->previewGen; state->origGen = g;
                        requestPreview(state, state->paneOrig, fs::path(file), g, 16384, 16384);
                        clearPreview(state->orig);
                        InvalidateRect(state->paneOrig, nullptr, TRUE);
                        clearPreview(state->comp); InvalidateRect(state->paneComp, nullptr, TRUE);
                        state->compGen = ++state->previewGen;
                        SetTimer(window, 1, 250, nullptr);
                    }
                    return 0;
                }
                if (id == kIdCompress) { startCompression(state); return 0; }
                if (id == kIdCancel) { if (state->busy) { state->cancelToken.cancel(); SetWindowTextW(state->status, L"Cancelling..."); } return 0; }
                if (id == kIdSaveAs) {
                    wchar_t file[MAX_PATH] = L"";
                    const std::wstring input = windowText(state->pathEdit);
                    const fs::path inputPath(input);
                    std::wstring suggested = inputPath.stem().wstring() + L"_compressed";
                    if (suggested.size() < MAX_PATH) {
                        std::copy(suggested.begin(), suggested.end(), file);
                        file[suggested.size()] = L'\0';
                    }
                    OPENFILENAMEW dlg{}; dlg.lStructSize = sizeof(dlg); dlg.hwndOwner = window;
                    dlg.lpstrFilter = L"Images (*.jpg;*.png;*.qoi;*.bmp;*.tga)\0*.jpg;*.png;*.qoi;*.bmp;*.tga\0All files (*.*)\0*.*\0";
                    dlg.lpstrFile = file; dlg.nMaxFile = MAX_PATH; dlg.lpstrTitle = L"Choose output file";
                    dlg.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER;
                    const std::wstring initialDir = inputPath.parent_path().wstring();
                    if (!initialDir.empty()) dlg.lpstrInitialDir = initialDir.c_str();
                    std::wstring defaultExtension = inputPath.extension().wstring();
                    const int formatIndex = static_cast<int>(SendMessageW(state->formatCombo, CB_GETCURSEL, 0, 0));
                    if (formatIndex == 1) defaultExtension = L"jpg";
                    else if (formatIndex == 2) defaultExtension = L"png";
                    else if (formatIndex == 3) defaultExtension = L"qoi";
                    if (!defaultExtension.empty() && defaultExtension.front() == L'.') defaultExtension.erase(0, 1);
                    if (defaultExtension.empty()) defaultExtension = L"png";
                    if (defaultExtension != L"jpg" && defaultExtension != L"jpeg" &&
                        defaultExtension != L"png" && defaultExtension != L"qoi" &&
                        defaultExtension != L"bmp" && defaultExtension != L"tga") defaultExtension = L"png";
                    dlg.lpstrDefExt = defaultExtension.c_str();
                    if (GetSaveFileNameW(&dlg)) {
                        setText(state->outputEdit, file);
                        state->request.outputPath = fs::path(file);
                    }
                    return 0;
                }
                if (id == kIdZoom) {
                    bool f = !state->orig.view.fit;
                    state->orig.view.fit = state->comp.view.fit = f;
                    state->orig.view.panX = state->orig.view.panY = state->comp.view.panX = state->comp.view.panY = 0;
                    SetWindowTextW(state->zoomBtn, f ? L"100%" : L"Fit");
                    InvalidateRect(state->paneOrig, nullptr, TRUE); InvalidateRect(state->paneComp, nullptr, TRUE);
                    return 0;
                }
                if (id == kIdOpenFolder && state->lastResult.ok) {
                    std::wstring cmd = L"/select,\"" + state->lastResult.outputPath.wstring() + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                if (id == kIdCopyPath && state->lastResult.ok) {
                    std::wstring w = state->lastResult.outputPath.wstring();
                    if (OpenClipboard(window)) {
                        EmptyClipboard();
                        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size()+1)*sizeof(wchar_t));
                        if (h) { memcpy(GlobalLock(h), w.c_str(), (w.size()+1)*sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
                        CloseClipboard();
                    }
                    SetWindowTextW(state->status, L"Path copied to clipboard.");
                    return 0;
                }
            }
            if (HIWORD(wParam) == EN_CHANGE &&
                (LOWORD(wParam) == kIdSizeEdit || LOWORD(wParam) == kIdPathEdit)) {
                SetTimer(window, 1, 300, nullptr);
                return 0;
            }
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kIdPreviewMode) {
                InvalidateRect(state->paneOrig, nullptr, TRUE);
                return 0;
            }
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kIdFormatCombo) {
                SetTimer(window, 1, 150, nullptr);
                return 0;
            }
            return 0;
        }
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wParam);
            wchar_t file[MAX_PATH] = L"";
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            if (n >= 1) {
                DragQueryFileW(drop, 0, file, MAX_PATH);
                setText(state->outputEdit, L"");
                state->lastResult.ok = false;
                EnableWindow(state->openFolder, FALSE);
                EnableWindow(state->copyPath, FALSE);
                SendMessageW(state->previewMode, CB_SETCURSEL, 0, 0);
                setText(state->pathEdit, file);
                state->previewPath = fs::path(file);
                SetWindowTextW(state->status, L"Ready.");
                const auto g = ++state->previewGen; state->origGen = g;
                requestPreview(state, state->paneOrig, fs::path(file), g, 16384, 16384);
                SetTimer(window, 1, 300, nullptr);
                clearPreview(state->comp); InvalidateRect(state->paneComp, nullptr, TRUE);
                state->compGen = ++state->previewGen;
                if (n > 1) SetWindowTextW(state->status, L"Using the first dropped image.");
            }
            DragFinish(drop);
            return 0;
        }
        case kMsgProgress: {
            auto* t = reinterpret_cast<std::wstring*>(lParam);
            if (t) { SetWindowTextW(state->status, t->c_str()); delete t; }
            return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) {
                KillTimer(window, 1);
                const auto path = windowText(state->pathEdit);
                const auto size = windowText(state->sizeEdit);
                if (!state->busy) {
                    if (path.empty()) {
                        state->previewPath.clear();
                        state->origGen = ++state->previewGen;
                        state->compGen = ++state->previewGen;
                        clearPreview(state->orig);
                        clearPreview(state->comp);
                        InvalidateRect(state->paneOrig, nullptr, TRUE);
                    } else {
                        const fs::path selectedPath(path);
                        if (selectedPath != state->previewPath) {
                            state->previewPath = selectedPath;
                            state->compGen = ++state->previewGen;
                            clearPreview(state->orig);
                            clearPreview(state->comp);
                            const auto originalGeneration = ++state->previewGen;
                            state->origGen = originalGeneration;
                            requestPreview(state, state->paneOrig, selectedPath, originalGeneration, 16384, 16384);
                            InvalidateRect(state->paneOrig, nullptr, TRUE);
                            InvalidateRect(state->paneComp, nullptr, TRUE);
                        }
                    }
                }
                if (!path.empty() && !size.empty() && std::wcstoull(size.c_str(), nullptr, 10) > 0 && !state->busy) {
                    int fi = static_cast<int>(SendMessageW(state->formatCombo, CB_GETCURSEL, 0, 0));
                    auto fmt = fi == 1 ? presspng::UserFormat::Jpeg : fi == 2 ? presspng::UserFormat::Png : fi == 3 ? presspng::UserFormat::Qoi : presspng::UserFormat::Keep;
                    const auto gen = ++state->previewGen;
                    state->compGen = gen;
                    requestPreview(state, state->paneComp, fs::path(path), gen, 16384, 16384, true,
                                   std::wcstoull(size.c_str(), nullptr, 10) * 1000ULL, fmt);
                    SetWindowTextW(state->status, L"Estimating target preview…");
                }
            }
            return 0;
        }
        case kMsgPreview: {
            auto* pane = reinterpret_cast<HWND>(wParam);
            auto* pm = reinterpret_cast<PreviewMsg*>(lParam);
            if (pm && state) {
                PreviewState* ps = (pane == state->paneOrig) ? &state->orig : &state->comp;
                const auto expected = pane == state->paneOrig ? state->origGen : state->compGen;
                if (pm->generation != expected) { delete pm; return 0; }
                clearPreview(*ps);
                if (pm->ok && !pm->thumbnail.empty()) {
                    ps->bitmap = makeDIB(pm->thumbnail);
                    ps->srcW = pm->thumbnail.width();
                    ps->srcH = pm->thumbnail.height();
                    ps->message.clear();
                    if (pm->estimate) {
                        std::wstring estimate = utf8ToWide(pm->message);
                        estimate += pm->targetMet ? L" — target met" : L" — target may be unreachable";
                        SetWindowTextW(state->status, estimate.c_str());
                        SendMessageW(state->previewMode, CB_SETCURSEL, 1, 0);
                    }
                } else {
                    ps->message = pm->message;
                }
                InvalidateRect(pane, nullptr, TRUE);
                if (pane == state->paneComp) InvalidateRect(state->paneOrig, nullptr, TRUE);
            }
            if (pm) delete pm;
            return 0;
        }
        case kMsgDone: {
            auto* result = reinterpret_cast<presspng::CompressionResult*>(lParam);
            if (result && state) {
                showResult(state, *result);
                if (result->ok) {
                    SendMessageW(state->progress, PBM_SETMARQUEE, FALSE, 0);
                    SendMessageW(state->progress, PBM_SETPOS, 100, 0);
                    SendMessageW(state->previewMode, CB_SETCURSEL, 1, 0);
                    InvalidateRect(state->paneOrig, nullptr, TRUE);
                }
                delete result;
            }
            if (state) {
                SendMessageW(state->progress, PBM_SETMARQUEE, FALSE, 0);
                state->busy = false;
                EnableWindow(state->compressBtn, TRUE);
                EnableWindow(state->cancelBtn, FALSE);
                if (state->closeWhenDone) SendMessageW(window, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        case WM_CLOSE:
            if (state && state->busy) { state->closeWhenDone = true; SetWindowTextW(state->status, L"Cancelling before close…"); state->cancelToken.cancel(); return 0; }
            if (state && state->settings) { const auto imagePath = fs::path(windowText(state->pathEdit)); if (!imagePath.empty()) state->appSettings.lastDirectory = imagePath.parent_path().u8string(); state->appSettings.targetSizeText = wideToUtf8(windowText(state->sizeEdit)); int fsel=static_cast<int>(SendMessageW(state->formatCombo,CB_GETCURSEL,0,0)); if(fsel==1)state->appSettings.format=presspng::UserFormat::Jpeg; else if(fsel==2)state->appSettings.format=presspng::UserFormat::Png; else if(fsel==3)state->appSettings.format=presspng::UserFormat::Qoi; else state->appSettings.format=presspng::UserFormat::Keep; state->settings->save(state->appSettings); }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t* kClass = L"PressPNGMainWindow";
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    AppState state;
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"PressPNG - image compressor",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  900, 740, nullptr, nullptr, instance, &state);
    if (!window) return 1;
    ACCEL shortcuts[] = {{FVIRTKEY | FCONTROL, 'O', kIdBrowse},
                         {FVIRTKEY | FCONTROL, VK_RETURN, kIdCompress}};
    HACCEL accelerator = CreateAcceleratorTableW(shortcuts, 2);
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accelerator, &msg) &&
            !IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (accelerator) DestroyAcceleratorTable(accelerator);
    clearPreview(state.orig); clearPreview(state.comp);
    return static_cast<int>(msg.wParam);
}
