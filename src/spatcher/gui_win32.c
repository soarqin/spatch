#define UNICODE
#define _UNICODE

#include "gui_win32.h"

#include "vfs.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDIP_IMPLEMENTATION
#include <nuklear.h>
#include <nuklear_gdip.h>
#include <nuklear_style.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 500

static void (*apply_patch_cb)(void*, const char*) = NULL;
static void *apply_patch_cb_opaque = NULL;

struct icons {
    struct nk_image desktop;
    struct nk_image home;
    struct nk_image computer;
    struct nk_image directory;

    struct nk_image default_file;
    struct nk_image text_file;
    struct nk_image music_file;
    struct nk_image font_file;
    struct nk_image img_file;
    struct nk_image movie_file;
};

enum file_groups {
    FILE_GROUP_DEFAULT,
    FILE_GROUP_TEXT,
    FILE_GROUP_MUSIC,
    FILE_GROUP_FONT,
    FILE_GROUP_IMAGE,
    FILE_GROUP_MOVIE,
    FILE_GROUP_MAX
};

enum file_types {
    FILE_DEFAULT,
    FILE_TEXT,
    FILE_C_SOURCE,
    FILE_CPP_SOURCE,
    FILE_HEADER,
    FILE_CPP_HEADER,
    FILE_MP3,
    FILE_WAV,
    FILE_OGG,
    FILE_TTF,
    FILE_BMP,
    FILE_PNG,
    FILE_JPEG,
    FILE_PCX,
    FILE_TGA,
    FILE_GIF,
    FILE_MAX
};

struct file_group {
    enum file_groups group;
    const char *name;
    struct nk_image *icon;
};

struct file {
    enum file_types type;
    const char *suffix;
    enum file_groups group;
};

struct media {
    int font;
    int icon_sheet;
    struct icons icons;
    struct file_group group[FILE_GROUP_MAX];
    struct file files[FILE_MAX];
};

#define MAX_PATH_LEN 512
struct file_browser {
    /* path */
    char file[MAX_PATH_LEN];
    char home[MAX_PATH_LEN];
    char desktop[MAX_PATH_LEN];
    char directory[MAX_PATH_LEN];

    /* directory content */
    char **files;
    char **directories;
    size_t file_count;
    size_t dir_count;
    struct media *media;
};

#ifndef _WIN32
# include <pwd.h>
#endif

static void
die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(EXIT_FAILURE);
}

static char *
file_load(const char *path, size_t *siz) {
    char *buf;
    FILE *fd = fopen(path, "rb");
    if (!fd) die("Failed to open file: %s\n", path);
    fseek(fd, 0, SEEK_END);
    *siz = (size_t)ftell(fd);
    fseek(fd, 0, SEEK_SET);
    buf = (char *)calloc(*siz, 1);
    fread(buf, *siz, 1, fd);
    fclose(fd);
    return buf;
}

static char *
str_duplicate(const char *src) {
    char *ret;
    size_t len = strlen(src);
    if (!len) return 0;
    ret = (char *)malloc(len + 1);
    if (!ret) return 0;
    memcpy(ret, src, len);
    ret[len] = '\0';
    return ret;
}

static void
dir_free_list(char **list, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i)
        free(list[i]);
    free(list);
}

static char **
dir_list(const char *dir, int return_subdirs, size_t *count) {
    size_t n = 0;
    char buffer[MAX_PATH_LEN];
    char **results = NULL;
    const struct vfs_dir_handle *none = NULL;
    size_t capacity = 32;
    size_t size = 0;
    struct vfs_dir_handle *z;

    assert(dir);
    assert(count);

#if defined(_WIN32)
    if (dir[0] == '/' && dir[1] == 0) {
        if (return_subdirs) {DWORD flag = 4;
            DWORD mask = GetLogicalDrives();
            size_t i;
            for (i = 2; i < 26; ++i, flag <<= 1) {
                if (mask & flag) {
                    char *p;
                    if (!size) {
                        results = (char **)calloc(sizeof(char *), capacity);
                    } else if (size >= capacity) {
                        void *old = results;
                        capacity = capacity * 2;
                        results = (char **)realloc(results, capacity * sizeof(char *));
                        assert(results);
                        if (!results) {
                            free(old);
                            size = 0;
                        }
                    }
                    p = malloc(4);
                    p[0] = (char)('A' + i);
                    p[1] = ':';
                    p[2] = 0;
                    results[size++] = p;
                }
            }
            if (count) *count = size;
            return results;
        } else {
            if (count) *count = 0;
            return NULL;
        }
    }
#endif

    strncpy(buffer, dir, MAX_PATH_LEN);
    n = strlen(buffer);

    if (n > 0 && (buffer[n - 1] != '/'))
        buffer[n++] = '/';

    z = vfs.opendir(dir, 0);
    if (z != none) {
        while (vfs.readdir(z)) {
            const char *name = vfs.dirent_get_name(z);
            if (name[0] == '.') { continue; }
            int is_subdir = vfs.dirent_is_dir(z);
            strncpy(buffer + n, name, MAX_PATH_LEN - n);

            if ((return_subdirs && is_subdir) || (!is_subdir && !return_subdirs)) {
                char *p;
                if (!size) {
                    results = (char **)calloc(sizeof(char *), capacity);
                } else if (size >= capacity) {
                    void *old = results;
                    capacity = capacity * 2;
                    results = (char **)realloc(results, capacity * sizeof(char *));
                    assert(results);
                    if (!results) {
                        free(old);
                        size = 0;
                    }
                }
                p = str_duplicate(name);
                results[size++] = p;
            }
        }
    }

    if (z) vfs.closedir(z);
    if (count) *count = size;
    return results;
}

static struct file_group
FILE_GROUP(enum file_groups group, const char *name, struct nk_image *icon) {
    struct file_group fg;
    fg.group = group;
    fg.name = name;
    fg.icon = icon;
    return fg;
}

static struct file
FILE_DEF(enum file_types type, const char *suffix, enum file_groups group) {
    struct file fd;
    fd.type = type;
    fd.suffix = suffix;
    fd.group = group;
    return fd;
}

static struct nk_image *
media_icon_for_file(struct media *media, const char *file) {
    int i = 0;
    const char *s = file;
    char suffix[4];
    int found = 0;
    memset(suffix, 0, sizeof(suffix));

    /* extract suffix .xxx from file */
    while (*s++ != '\0') {
        if (found && i < 3)
            suffix[i++] = *s;

        if (*s == '.') {
            if (found) {
                found = 0;
                break;
            }
            found = 1;
        }
    }

    /* check for all file definition of all groups for fitting suffix*/
    for (i = 0; i < FILE_MAX && found; ++i) {
        struct file *d = &media->files[i];
        {
            const char *f = d->suffix;
            s = suffix;
            while (f && *f && *s && *s == *f) {
                s++;
                f++;
            }

            /* found correct file definition so */
            if (f && *s == '\0' && *f == '\0')
                return media->group[d->group].icon;
        }
    }
    return &media->icons.default_file;
}

static void
media_init(struct media *media) {
    /* file groups */
    struct icons *icons = &media->icons;
    media->group[FILE_GROUP_DEFAULT] = FILE_GROUP(FILE_GROUP_DEFAULT, "default", &icons->default_file);
    media->group[FILE_GROUP_TEXT] = FILE_GROUP(FILE_GROUP_TEXT, "textual", &icons->text_file);
    media->group[FILE_GROUP_MUSIC] = FILE_GROUP(FILE_GROUP_MUSIC, "music", &icons->music_file);
    media->group[FILE_GROUP_FONT] = FILE_GROUP(FILE_GROUP_FONT, "font", &icons->font_file);
    media->group[FILE_GROUP_IMAGE] = FILE_GROUP(FILE_GROUP_IMAGE, "image", &icons->img_file);
    media->group[FILE_GROUP_MOVIE] = FILE_GROUP(FILE_GROUP_MOVIE, "movie", &icons->movie_file);

    /* files */
    media->files[FILE_DEFAULT] = FILE_DEF(FILE_DEFAULT, NULL, FILE_GROUP_DEFAULT);
    media->files[FILE_TEXT] = FILE_DEF(FILE_TEXT, "txt", FILE_GROUP_TEXT);
    media->files[FILE_C_SOURCE] = FILE_DEF(FILE_C_SOURCE, "c", FILE_GROUP_TEXT);
    media->files[FILE_CPP_SOURCE] = FILE_DEF(FILE_CPP_SOURCE, "cpp", FILE_GROUP_TEXT);
    media->files[FILE_HEADER] = FILE_DEF(FILE_HEADER, "h", FILE_GROUP_TEXT);
    media->files[FILE_CPP_HEADER] = FILE_DEF(FILE_HEADER, "hpp", FILE_GROUP_TEXT);
    media->files[FILE_MP3] = FILE_DEF(FILE_MP3, "mp3", FILE_GROUP_MUSIC);
    media->files[FILE_WAV] = FILE_DEF(FILE_WAV, "wav", FILE_GROUP_MUSIC);
    media->files[FILE_OGG] = FILE_DEF(FILE_OGG, "ogg", FILE_GROUP_MUSIC);
    media->files[FILE_TTF] = FILE_DEF(FILE_TTF, "ttf", FILE_GROUP_FONT);
    media->files[FILE_BMP] = FILE_DEF(FILE_BMP, "bmp", FILE_GROUP_IMAGE);
    media->files[FILE_PNG] = FILE_DEF(FILE_PNG, "png", FILE_GROUP_IMAGE);
    media->files[FILE_JPEG] = FILE_DEF(FILE_JPEG, "jpg", FILE_GROUP_IMAGE);
    media->files[FILE_PCX] = FILE_DEF(FILE_PCX, "pcx", FILE_GROUP_IMAGE);
    media->files[FILE_TGA] = FILE_DEF(FILE_TGA, "tga", FILE_GROUP_IMAGE);
    media->files[FILE_GIF] = FILE_DEF(FILE_GIF, "gif", FILE_GROUP_IMAGE);
}

static void
file_browser_reload_directory_content(struct file_browser *browser, const char *path) {
    strncpy(browser->directory, path, MAX_PATH_LEN);
    dir_free_list(browser->files, browser->file_count);
    dir_free_list(browser->directories, browser->dir_count);
    browser->files = dir_list(path, 0, &browser->file_count);
    browser->directories = dir_list(path, 1, &browser->dir_count);
}

static void
file_browser_init(struct file_browser *browser, struct media *media) {
    memset(browser, 0, sizeof(*browser));
    browser->media = media;
    {
        /* load files and sub-directory list */
        char *home = getenv("HOME");
#ifdef _WIN32
        int i;
        if (!home) home = getenv("USERPROFILE");
        for (i = 0; home[i]; ++i) {
            if (home[i] == '\\') home[i] = '/';
        }
#else
        if (!home) home = getpwuid(getuid())->pw_dir;
#endif
        {
            size_t l;
            strncpy(browser->home, home, MAX_PATH_LEN);
            l = strlen(browser->home);
            strcpy(browser->home + l, "/");
        }
        {
            size_t l;
            strcpy(browser->desktop, browser->home);
            l = strlen(browser->desktop);
#ifdef _WIN32
            strcpy(browser->desktop + l, "Desktop/");
#else
            strcpy(browser->desktop + l, "desktop/");
#endif
        }
    }
}

static void
file_browser_free(struct file_browser *browser) {
    if (browser->files)
        dir_free_list(browser->files, browser->file_count);
    if (browser->directories)
        dir_free_list(browser->directories, browser->dir_count);
    browser->files = NULL;
    browser->directories = NULL;
    memset(browser, 0, sizeof(*browser));
}

static int
file_browser_run(struct file_browser *browser, struct nk_context *ctx) {
    int ret = 0;
    struct media *media = browser->media;
    struct nk_rect total_space;
    float rows_ratio[] = {NK_UNDEFINED, NK_UNDEFINED, NK_UNDEFINED, NK_UNDEFINED, NK_UNDEFINED, NK_UNDEFINED, NK_UNDEFINED};
    const char *comboitems[] = {"Home", "Desktop", "Computer"};
    const struct nk_image comboimages[] = {
        media->icons.home,
        media->icons.desktop,
        media->icons.computer,
        {0}
    };
    if (nk_begin(ctx, "File Browser", nk_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
                 NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_CLOSABLE)) {
        float spacing_x = ctx->style.window.spacing.x;
        int combo_count = sizeof(comboitems) / sizeof(const char *);
        nk_flags old_flags;

        total_space = nk_window_get_content_region(ctx);
        rows_ratio[0] = 160.f / total_space.w;

        nk_menubar_begin(ctx);
        nk_layout_row(ctx, NK_DYNAMIC, 25.f, 7, rows_ratio);
        old_flags = ctx->style.combo.button.text_alignment;
        ctx->style.contextual_button.text_alignment = NK_TEXT_LEFT;
        if (nk_combo_begin_image_label(ctx, "==NAV==", comboimages[combo_count], nk_vec2(200.f, 400.f))) {
            int i;
            nk_layout_row_dynamic(ctx, 25.f, 1);
            for (i = 0; i < combo_count; ++i) {
                if (nk_combo_item_image_label(ctx, comboimages[i], comboitems[i], 0)) {
                    switch (i) {
                    case 0:
                        file_browser_reload_directory_content(browser, browser->home);
                        break;
                    case 1:
                        file_browser_reload_directory_content(browser, browser->desktop);
                        break;
                    case 2:
                        file_browser_reload_directory_content(browser, "/");
                        break;
                    default:
                        break;
                    }
                }
            }
            nk_combo_end(ctx);
        }
        ctx->style.contextual_button.text_alignment = old_flags;

        /* output path directory selector in the menubar */
        ctx->style.window.spacing.x = 1;
        {
            char *d = browser->directory;
            char *begin = d;
            if (*begin == '/') ++begin;
            while (*d++) {
                if (*d == '/') {
                    *d = '\0';
                    if (nk_button_label(ctx, begin)) {
                        *d++ = '/';
                        *d = '\0';
                        file_browser_reload_directory_content(browser, browser->directory);
                        break;
                    }
                    *d = '/';
                    begin = d + 1;
                }
            }
        }
        nk_menubar_end(ctx);
        ctx->style.window.spacing.x = spacing_x;

        /* window layout */
        total_space = nk_window_get_content_region(ctx);
        nk_layout_row_dynamic(ctx, total_space.h - 40, 1);

        /* output directory content window */
        nk_group_begin(ctx, "Content", 0);
        {
            int index = -1;
            size_t i = 0;
            size_t count = browser->dir_count + browser->file_count;

            old_flags = ctx->style.button.text_alignment;
            ctx->style.button.text_alignment = NK_TEXT_LEFT;
            nk_layout_row_dynamic(ctx, 25.f, 3);
            for (i = 0; i < count; ++i) {
                if (i < browser->dir_count) {
                    /* draw and execute directory buttons */
                    if (nk_button_image_label(ctx, media->icons.directory, browser->directories[i], 0))
                        index = (int)i;
                } else {
                    /* draw and execute files buttons */
                    struct nk_image *icon;
                    size_t fileIndex = ((size_t)i - browser->dir_count);
                    icon = media_icon_for_file(media, browser->files[fileIndex]);
                    if (nk_button_image_label(ctx, *icon, browser->files[fileIndex], 0)) {
                        size_t n;
                        strncpy(browser->file, browser->directory, MAX_PATH_LEN);
                        n = strlen(browser->file);
                        strncpy(browser->file + n, browser->files[fileIndex], MAX_PATH_LEN - n);
                        ret = 1;
                    }
                }
            }

            if (index != -1) {
                size_t n = strlen(browser->directory);
#if defined(_WIN32)
                if (n == 1 && browser->directory[0] == '/') {
                    n = 0;
                }
#endif
                strncpy(browser->directory + n, browser->directories[index], MAX_PATH_LEN - n);
                n = strlen(browser->directory);
                if (n < MAX_PATH_LEN - 1) {
                    browser->directory[n] = '/';
                    browser->directory[n + 1] = '\0';
                }
                file_browser_reload_directory_content(browser, browser->directory);
            }
            ctx->style.button.text_alignment = old_flags;
            nk_group_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 0, 1);
        if (nk_button_label(ctx, "Use this folder")) {
            ret = 1;
        }
    } else {
        ret = -1;
    }
    nk_end(ctx);
    return ret;
}

static int gui_main_run(struct nk_context *ctx, const char *curr_dir) {
    int ret = 0;
    if (nk_begin(ctx, "spatch", nk_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
                 NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_CLOSABLE)) {
        struct nk_rect total_space;
        int len = strlen(curr_dir);
        total_space = nk_window_get_content_region(ctx);
        nk_layout_space_begin(ctx, NK_DYNAMIC, total_space.h, 3);
        nk_layout_space_push(ctx, nk_rect(0.2, 0.35, 0.52, 0.09));
        nk_edit_string(ctx, NK_EDIT_READ_ONLY | NK_EDIT_SELECTABLE, (char*)curr_dir, &len, 4096, NULL);
        nk_layout_space_push(ctx, nk_rect(0.73, 0.35, 0.07, 0.09));
        if (nk_button_label(ctx, "...")) {
            ret = 1;
        }
        nk_layout_space_push(ctx, nk_rect(0.2, 0.45, 0.6, 0.09));
        if (nk_button_label(ctx, "Apply Patch")) {
            ret = 2;
        }
        nk_layout_space_end(ctx);
    } else {
        ret = -1;
    }
    nk_end(ctx);
    return ret;
}

static int64_t patch_progress = 0, patch_total = 0;
static char patch_message[1024] = {0};

static int patch_progress_run(struct nk_context *ctx) {
    int ret = 0;
    if (nk_begin(ctx, "spatch", nk_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
                 NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_rect total_space;
        char progtext[64];
        total_space = nk_window_get_content_region(ctx);
        nk_layout_space_begin(ctx, NK_DYNAMIC, total_space.h, patch_progress < 0 ? 4 : 3);
        nk_layout_space_push(ctx, nk_rect(0.1, 0.35, 0.8, 0.09));
        nk_label(ctx, patch_message, NK_TEXT_CENTERED);
        nk_layout_space_push(ctx, nk_rect(0.2, 0.45, 0.6, 0.09));
        nk_prog(ctx, patch_progress, patch_total, nk_false);
        snprintf(progtext, 64, "%d%%", patch_progress < 0 ? 100 : patch_total ? (int)(patch_progress * 100 / patch_total) : 0);
        nk_label(ctx, progtext, NK_TEXT_CENTERED);
        if (patch_progress < 0) {
            nk_layout_space_push(ctx, nk_rect(0.3, 0.55, 0.4, 0.09));
            if (nk_button_label(ctx, "Finish")) {
                ret = -1;
            }
        }
        nk_layout_space_end(ctx);
    }
    nk_end(ctx);
    return ret;
}

static int header_height = 0;
static int header_caporgx = 0, header_caporgy = 0, header_capx = 0, header_capy = 0, header_cap = 0;

static void update_window_header_height(struct nk_context *ctx) {
    header_height = (int)(ctx->style.font->height + 2.0f * ctx->style.window.header.padding.y
        + 2.0f * ctx->style.window.header.label_padding.y + .5f);
}

static LRESULT CALLBACK
WindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        if ((lparam >> 16) < header_height && (lparam & 0xFFFF) < WINDOW_WIDTH - header_height) {
            RECT rc;
            POINT pt;
            SetCapture(wnd);
            header_cap = 1;
            GetCursorPos(&pt);
            header_capx = pt.x;
            header_capy = pt.y;
            GetWindowRect(wnd, &rc);
            header_caporgx = rc.left;
            header_caporgy = rc.top;
            fflush(stdout);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
        if (header_cap) {
            header_cap = 0;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (!header_cap) {
            break;
        }
        {
            POINT pt;
            RECT rc;
            GetCursorPos(&pt);
            GetWindowRect(wnd, &rc);
            MoveWindow(wnd, header_caporgx + pt.x - header_capx, header_caporgy + pt.y - header_capy, rc.right - rc.left, rc.bottom - rc.top, FALSE);
            return 0;
        }
    case WM_CREATE: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessage(wnd, WM_SETICON, ICON_SMALL2, (LPARAM)icon);
        DestroyIcon(icon);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    if (nk_gdip_handle_event(wnd, msg, wparam, lparam))
        return 0;
    return DefWindowProcW(wnd, msg, wparam, lparam);
}

DWORD WINAPI patchThread(LPVOID lpThreadParameter) {
    if (apply_patch_cb) {
        apply_patch_cb(apply_patch_cb_opaque, (const char*)lpThreadParameter);
    }
    return 0;
}

static HWND main_wnd;

int run_gui(const char *init_path) {
    GdipFont *font[2];
    struct nk_context *ctx;

    WNDCLASSW wc;
    DWORD style = 0;
    DWORD exstyle = 0;
    int running = 1;
    int needs_refresh = 1;

    struct file_browser browser;
    struct media media;
    int scene = 0;
    char curr_path[512];
    HANDLE thread_hdl = INVALID_HANDLE_VALUE;

    snprintf(curr_path, 512, "%s", init_path);

    /* Win32 */
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(0);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"NuklearWindowClass";
    RegisterClassW(&wc);

    main_wnd = CreateWindowExW(exstyle, wc.lpszClassName, L"Nuklear Demo", style,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, wc.hInstance, NULL);
    SetWindowLong(main_wnd, GWL_STYLE, 0);
    {
        HMONITOR monitor = MonitorFromWindow(main_wnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(monitor, &mi);
        MoveWindow(main_wnd,
                   (mi.rcWork.left + mi.rcWork.right - WINDOW_WIDTH) / 2,
                   (mi.rcWork.top + mi.rcWork.bottom - WINDOW_HEIGHT) / 2,
                   WINDOW_WIDTH, WINDOW_HEIGHT,
                   FALSE);
    }
    ShowWindow(main_wnd, SW_SHOW);

    /* GUI */
    ctx = nk_gdip_init(main_wnd, WINDOW_WIDTH, WINDOW_HEIGHT);
    font[0] = nk_gdipfont_create("Arial", 16, FontStyleBold);
    font[1] = nk_gdipfont_create("Arial", 14, FontStyleRegular);
    nk_gdip_set_font(font[0]);
    update_window_header_height(ctx);

    /*set_style(ctx, THEME_WHITE);*/
    /*set_style(ctx, THEME_RED);*/
    /*set_style(ctx, THEME_BLUE);*/
    /*set_style(ctx, THEME_DARK);*/

    media.icons.home = nk_gdip_load_image_from_rcdata(1006);
    media.icons.directory = nk_gdip_load_image_from_rcdata(1004);
    media.icons.computer = nk_gdip_load_image_from_rcdata(1001);
    media.icons.desktop = nk_gdip_load_image_from_rcdata(1003);
    media.icons.default_file = nk_gdip_load_image_from_rcdata(1002);
    media.icons.text_file = nk_gdip_load_image_from_rcdata(1010);
    media.icons.music_file = nk_gdip_load_image_from_rcdata(1009);
    media.icons.font_file = nk_gdip_load_image_from_rcdata(1005);
    media.icons.img_file = nk_gdip_load_image_from_rcdata(1007);
    media.icons.movie_file = nk_gdip_load_image_from_rcdata(1008);
    media_init(&media);

    file_browser_init(&browser, &media);

    while (running) {
        MSG msg;
        nk_input_begin(ctx);
        if (needs_refresh == 0) {
            if (GetMessageW(&msg, NULL, 0, 0) <= 0)
                running = 0;
            else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            needs_refresh = 1;
        } else needs_refresh = 0;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        nk_input_end(ctx);
        switch (scene) {
        case 0:
            switch (gui_main_run(ctx, curr_path)) {
            case -1:
                PostQuitMessage(0);
                break;
            case 1:
                scene = 1;
                nk_gdip_set_font(font[1]);
                update_window_header_height(ctx);
                strcat(curr_path, "/");
                file_browser_reload_directory_content(&browser, curr_path);
                break;
            case 2:
                scene = 2;
                nk_gdip_set_font(font[0]);
                thread_hdl = CreateThread(NULL, 0, &patchThread, curr_path, 0, NULL);
                break;
            default:
                break;
            }
            break;
        case 1: {
            int len;
            int res = file_browser_run(&browser, ctx);
            if (res != 0) {
                scene = 0;
                nk_gdip_set_font(font[0]);
                update_window_header_height(ctx);
                if (res > 0) {
                    snprintf(curr_path, 512, "%s", browser.directory);
                }
            }
            len = strlen(curr_path);
            if (len && curr_path[len - 1] == '/') curr_path[len - 1] = 0;
            break;
        }
        default:
            if (patch_progress_run(ctx) < 0) {
                PostQuitMessage(0);
            }
            break;
        }
        nk_gdip_render(NK_ANTI_ALIASING_ON, nk_rgb(30, 30, 30));
    }

    if (thread_hdl != INVALID_HANDLE_VALUE && thread_hdl != NULL) {
        TerminateThread(thread_hdl, 0);
        CloseHandle(thread_hdl);
    }
    nk_gdipfont_del(font[0]);
    nk_gdipfont_del(font[1]);
    nk_gdip_shutdown();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

void set_apply_patch_callback(void (*cb)(void*, const char*), void *opaque) {
    apply_patch_cb = cb;
    apply_patch_cb_opaque = opaque;
}

void set_apply_patch_progress(int64_t prog, int64_t total) {
    patch_progress = prog;
    patch_total = total;
    PostMessage(main_wnd, WM_USER + 1, 0, 0);
}

void set_apply_patch_message(const char *message) {
    snprintf(patch_message, 1024, "%s", message);
    PostMessage(main_wnd, WM_USER + 1, 0, 0);
}

void set_apply_patching_file(const char *filename) {
    snprintf(patch_message, 1024, "Patching %s...", filename);
    PostMessage(main_wnd, WM_USER + 1, 0, 0);
}
