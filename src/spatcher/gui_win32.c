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
            strcpy(browser->directory, browser->home);
        }
        {
            size_t l;
            strcpy(browser->desktop, browser->home);
            l = strlen(browser->desktop);
            strcpy(browser->desktop + l, "desktop/");
        }
        browser->files = dir_list(browser->directory, 0, &browser->file_count);
        browser->directories = dir_list(browser->directory, 1, &browser->dir_count);
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
    static int fav_sel = -1;
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

    if (nk_begin(ctx, "File Browser", nk_rect(50, 50, WINDOW_WIDTH - 100, WINDOW_HEIGHT - 100),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        float spacing_x = ctx->style.window.spacing.x;
        int combo_count = sizeof(comboitems) / sizeof(const char *);
        nk_flags old_flags;

        total_space = nk_window_get_content_region(ctx);
        rows_ratio[0] = 200.f / total_space.w;

        nk_menubar_begin(ctx);
        nk_layout_row(ctx, NK_DYNAMIC, 25.f, 7, rows_ratio);
        old_flags = ctx->style.combo.button.text_alignment;
        ctx->style.contextual_button.text_alignment = NK_TEXT_LEFT;
        if (nk_combo_begin_image_label(ctx, fav_sel < 0 ? "" : comboitems[fav_sel], comboimages[fav_sel < 0 ? combo_count : fav_sel], nk_vec2(200.f, 400.f))) {
            int i;
            nk_layout_row_dynamic(ctx, 25.f, 1);
            for (i = 0; i < combo_count; ++i) {
                if (nk_combo_item_image_label(ctx, comboimages[i], comboitems[i], 0)) {
                    fav_sel = i;
                    switch (i) {
                    case 0:
                        file_browser_reload_directory_content(browser, browser->home);
                        break;
                    case 1:
                        file_browser_reload_directory_content(browser, browser->desktop);
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
        nk_layout_row_dynamic(ctx, total_space.h, 1);

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
    }
    nk_end(ctx);
    return ret;
}

static LRESULT CALLBACK
WindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:break;
    }
    if (nk_gdip_handle_event(wnd, msg, wparam, lparam))
        return 0;
    return DefWindowProcW(wnd, msg, wparam, lparam);
}

int run_gui() {
    GdipFont *font;
    struct nk_context *ctx;

    WNDCLASSW wc;
    RECT rect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exstyle = WS_EX_APPWINDOW;
    HWND wnd;
    int running = 1;
    int needs_refresh = 1;

    struct file_browser browser;
    struct media media;

    /* Win32 */
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(0);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"NuklearWindowClass";
    RegisterClassW(&wc);

    AdjustWindowRectEx(&rect, style, FALSE, exstyle);

    wnd = CreateWindowExW(exstyle, wc.lpszClassName, L"Nuklear Demo",
                          style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                          rect.right - rect.left, rect.bottom - rect.top,
                          NULL, NULL, wc.hInstance, NULL);

    /* GUI */
    ctx = nk_gdip_init(wnd, WINDOW_WIDTH, WINDOW_HEIGHT);
    font = nk_gdipfont_create("Arial", 12);
    nk_gdip_set_font(font);

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
            needs_refresh = 1;
        }
        nk_input_end(ctx);
        file_browser_run(&browser, ctx);
        nk_gdip_render(NK_ANTI_ALIASING_ON, nk_rgb(30, 30, 30));
    }

    nk_gdipfont_del(font);
    nk_gdip_shutdown();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
