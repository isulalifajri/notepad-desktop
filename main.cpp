#include <windows.h>
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#include "sqlite3.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <algorithm>

// ---------------- constants & globals ----------------
const char g_szClassName[] = "notepadApp";
const int ID_SEARCH = 100;
const int ID_BTN_ADD = 101;
const UINT MSG_REFRESH = WM_USER + 1;

HWND hSearchBox = NULL;
HWND hButtonAdd = NULL;
HWND hMainWnd = NULL;

sqlite3* db = nullptr;

// font handles
HFONT hFontBold = NULL;
HFONT hFontNormal = NULL;

// scroll globals
int g_scrollY = 0;
int g_totalHeight = 0;

// structure to track card rectangles and note ids
struct CardInfo {
    RECT rc;
    int noteId;
};
std::vector<CardInfo> g_cards;

// ---------------- SQLite helpers ----------------
bool initDatabase() {
    int rc = sqlite3_open("notes.db", &db);
    if (rc != SQLITE_OK) {
        MessageBoxA(NULL, sqlite3_errmsg(db), "DB Open Error", MB_OK | MB_ICONERROR);
        if (db) sqlite3_close(db);
        db = nullptr;
        return false;
    }
    const char* sql =
        "CREATE TABLE IF NOT EXISTS notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, "
        "content TEXT);";
    char* errmsg = nullptr;
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = "DB Init Error: ";
        msg += errmsg ? errmsg : "";
        MessageBoxA(NULL, msg.c_str(), "DB Error", MB_OK | MB_ICONERROR);
        sqlite3_free(errmsg);
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    return true;
}

bool insertNotePrepared(const std::string& title, const std::string& content) {
    if (!db) return false;
    const char* sql = "INSERT INTO notes (title, content) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool updateNotePrepared(int id, const std::string& title, const std::string& content) {
    if (!db) return false;
    const char* sql = "UPDATE notes SET title = ?, content = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

// fetch notes (id, title, content)
struct Note {
    int id;
    std::string title;
    std::string content;
};
std::vector<Note> fetchNotes(const std::string& q = "") {
    std::vector<Note> out;
    if (!db) return out;
    std::string sql = "SELECT id, title, content FROM notes ";
    if (!q.empty()) {
        sql += "WHERE title LIKE ? OR content LIKE ? ";
    }
    sql += "ORDER BY id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    if (!q.empty()) {
        std::string p = "%" + q + "%";
        sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Note n;
        n.id = sqlite3_column_int(stmt, 0);
        const unsigned char* t = sqlite3_column_text(stmt, 1);
        const unsigned char* c = sqlite3_column_text(stmt, 2);
        n.title = t ? (const char*)t : "";
        n.content = c ? (const char*)c : "";
        out.push_back(n);
    }
    sqlite3_finalize(stmt);
    return out;
}

// ---------------- UI: helper to destroy only card children ----------------
void clearCards(HWND hwndParent) {
    HWND child = GetWindow(hwndParent, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        if (child != hSearchBox && child != hButtonAdd) {
            DestroyWindow(child);
        }
        child = next;
    }
    g_cards.clear();
}

// ---------------- UI: show notes in grid 2-cols ----------------
void showNotes(HWND hwndParent, const std::string& search = "") {
    char buf[512] = {0};
    if (hSearchBox) GetWindowTextA(hSearchBox, buf, (int)sizeof(buf));
    clearCards(hwndParent);

    std::vector<Note> notes = fetchNotes(search.empty() ? std::string(buf) : search);

    int margin = 10;
    int cardW = 180;
    int cardH = 110;
    int x = margin;
    int y = 50 - g_scrollY; // ADDED: scroll offset
    int col = 0;

    HINSTANCE hInst = GetModuleHandle(NULL);

    for (auto& n : notes) {
        HWND hCard = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "",
            WS_CHILD | WS_VISIBLE, x, y, cardW, cardH, hwndParent, NULL, hInst, NULL);

        HWND hTitle = CreateWindowA("STATIC", n.title.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 8, 8, cardW - 16, 22, hCard, NULL, hInst, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        std::string contentPreview = n.content;
        if (contentPreview.size() > 300) contentPreview = contentPreview.substr(0, 300) + "...";
        HWND hContent = CreateWindowA("STATIC", contentPreview.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 8, 34, cardW - 16, cardH - 42, hCard, NULL, hInst, NULL);
        SendMessage(hContent, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        RECT rc;
        SetRect(&rc, x, y, x + cardW, y + cardH);
        g_cards.push_back({ rc, n.id });

        col++;
        if (col == 2) {
            col = 0;
            x = margin;
            y += cardH + margin;
        }
        else {
            x += cardW + margin;
        }
    }

    // ADDED: scroll range
    g_totalHeight = y + cardH + margin;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = g_totalHeight;
    si.nPage = 500;
    SetScrollInfo(hwndParent, SB_VERT, &si, TRUE);
}

// ---------------- Note editor window ----------------
LRESULT CALLBACK NoteWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hTitleEdit = NULL;
    static HWND hContentEdit = NULL;
    static intptr_t noteId = 0;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        noteId = (intptr_t)cs->lpCreateParams;

        CreateWindowA("STATIC", "Judul:", WS_CHILD | WS_VISIBLE, 10, 10, 50, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
        hTitleEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            70, 10, 320, 22, hwnd, NULL, GetModuleHandle(NULL), NULL);

        CreateWindowA("STATIC", "Isi Catatan:", WS_CHILD | WS_VISIBLE, 10, 40, 80, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
        hContentEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            10, 65, 380, 260, hwnd, NULL, GetModuleHandle(NULL), NULL);

        if (noteId > 0 && db) {
            const char* sql = "SELECT title, content FROM notes WHERE id = ? LIMIT 1;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, (int)noteId);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const unsigned char* t = sqlite3_column_text(stmt, 0);
                    const unsigned char* c = sqlite3_column_text(stmt, 1);
                    if (t) SetWindowTextA(hTitleEdit, (const char*)t);
                    if (c) SetWindowTextA(hContentEdit, (const char*)c);
                }
            }
            sqlite3_finalize(stmt);
        }
        break;
    }

    case WM_CLOSE: {
        int lenTitle = GetWindowTextLengthA(hTitleEdit);
        int lenContent = GetWindowTextLengthA(hContentEdit);
        std::string title; title.resize(lenTitle);
        std::string content; content.resize(lenContent);
        GetWindowTextA(hTitleEdit, &title[0], lenTitle + 1);
        GetWindowTextA(hContentEdit, &content[0], lenContent + 1);

        bool hasContent = !content.empty();
        if (hasContent) {
            if (noteId > 0) {
                updateNotePrepared((int)noteId, title, content);
            }
            else {
                insertNotePrepared(title, content);
            }
        }
        if (hMainWnd) PostMessage(hMainWnd, MSG_REFRESH, 0, 0);
        DestroyWindow(hwnd);
        break;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ---------------- main window proc ----------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hMainWnd = hwnd;

        hFontBold = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        hFontNormal = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        if (!initDatabase()) {
            PostQuitMessage(1);
            break;
        }

        // search box + placeholder
        hSearchBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 10, 360, 24,
            hwnd, (HMENU)ID_SEARCH, GetModuleHandle(NULL), NULL);
        SendMessageW(hSearchBox, EM_SETCUEBANNER, FALSE, (LPARAM)L"Cari catatan...");
        UpdateWindow(hSearchBox);


        // tombol tambah (posisi diatur di WM_SIZE)
        hButtonAdd = CreateWindowA("BUTTON", "+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 60, 60, hwnd, (HMENU)ID_BTN_ADD, GetModuleHandle(NULL), NULL);

        showNotes(hwnd, "");
        break;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int btnW = 60, btnH = 60;
        int margin = 10;
        int btnX = rc.right - btnW - margin;
        int btnY = rc.bottom - btnH - margin;
        SetWindowPos(hButtonAdd, HWND_TOP, btnX, btnY, btnW, btnH, SWP_NOZORDER);
        break;
    }

    case WM_VSCROLL: {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int yPos = si.nPos;

        switch (LOWORD(wParam)) {
        case SB_LINEUP: si.nPos -= 20; break;
        case SB_LINEDOWN: si.nPos += 20; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si);

        if (si.nPos != yPos) {
            ScrollWindow(hwnd, 0, yPos - si.nPos, NULL, NULL);
            g_scrollY = si.nPos;
            UpdateWindow(hwnd);
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == ID_BTN_ADD) {
            WNDCLASSEX wcNote{};
            wcNote.cbSize = sizeof(wcNote);
            wcNote.lpfnWndProc = NoteWndProc;
            wcNote.hInstance = GetModuleHandle(NULL);
            wcNote.lpszClassName = "NoteWindowClass";
            RegisterClassExA(&wcNote);

            HWND note = CreateWindowExA(0, "NoteWindowClass", "Catatan Baru",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 420, 380,
                hwnd, NULL, GetModuleHandle(NULL), (LPVOID)0);
            ShowWindow(note, SW_SHOW);
        }
        else if (id == ID_SEARCH && code == EN_CHANGE) {
            char q[256] = { 0 };
            GetWindowTextA(hSearchBox, q, sizeof(q));
            showNotes(hwnd, std::string(q));
        }
        break;
    }

    case MSG_REFRESH: {
        char q[256] = { 0 };
        if (hSearchBox) GetWindowTextA(hSearchBox, q, sizeof(q));
        showNotes(hwnd, std::string(q));
        break;
    }

    case WM_DESTROY:
        if (db) sqlite3_close(db);
        if (hFontBold) DeleteObject(hFontBold);
        if (hFontNormal) DeleteObject(hFontNormal);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ---------------- winmain ----------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = g_szClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Register class failed", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // mengatur tampilan window
    HWND hwnd = CreateWindowExA(
        0,                  // Extended style
        g_szClassName,      // Class name
        "Notepad SQLite - C++", // Window title
        // WS_OVERLAPPEDWINDOW, -> kalau mau bisa resize,, terus dibawah nya 1 baris ini dihapus
        WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU, // Window style, nggak bisa di maxsimaze
        CW_USEDEFAULT,      // x posisi (default)
        CW_USEDEFAULT,      // y posisi (default)
        450,                // **lebar window**
        620,                // **tinggi window**
        NULL,               // Parent window
        NULL,               // Menu
        hInstance,          // Instance handle
        NULL                // Parameter tambahan
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
