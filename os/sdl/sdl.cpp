
// Outlaws.h platform implementation for SDL

#include "StdAfx.h"

#include <locale>
#include <list>

#include "Graphics.h"

#include "sdl_inc.h"
#include "sdl_os.h"

static SDL_Rect     g_savedWindowPos;
static int2         g_windowSize; // size of current window
static float        g_scaling_factor = 1.f;
static SDL_Window*  g_displayWindow  = NULL;
static bool         g_quitting       = false;
static SDL_RWops   *g_logfile        = NULL;
static const char*  g_logpath        = NULL;
static string       g_logdata;
enum LogStates { LOG_INIT, LOG_OPENING, LOG_OPEN, LOG_CLOSED };
static LogStates    g_logstate       = LOG_CLOSED;
static int          g_supportsTearControl = -1;
static int          g_swapInterval = 0;
static bool         g_wantsLogUpload = false;

static DEFINE_CVAR(bool, kOpenGLDebug, IS_DEVEL);
static DEFINE_CVAR(bool, kMaximizeWindow, true);
static DEFINE_CVAR(bool, kTTFDebug, false);
static DEFINE_CVAR(int, kFrameSleepMethod, 1);

#if OL_WINDOWS
#define OL_ENDL "\r\n"
#else
#define OL_ENDL "\n"
#endif

void SetWindowResizable(SDL_Window *win, SDL_bool resizable)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_displayWindow, &info))
        return;

#if OL_WINDOWS
    HWND hwnd = info.info.win.window;
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (resizable)
        style |= WS_THICKFRAME;
    else
        style &= ~WS_THICKFRAME;
    SetWindowLong(hwnd, GWL_STYLE, style);
#endif
}

// don't go through ReportMessagef/ReportMessage!
static void ReportSDL(const char *format, ...)
{
    va_list vl;
    va_start(vl, format);
    const string buf = "\n[SDL] " + str_vformat(format, vl);
    OL_ReportMessage(buf.c_str());
    va_end(vl);
}

static void ReportSDLErr(const char* func)
{
    const string buf = str_format("\n[SDL] %s failed: %s", func, SDL_GetError());
    OL_ReportMessage(buf.c_str());
}

static const string loadFile(SDL_RWops *io, const char* name)
{
    if (!io) {
        ReportSDL("error opening '%s': %s", name, SDL_GetError());
        return "";
    }
    string buf;
    Sint64 size = SDL_RWsize(io);
    buf.resize(size);
    if (SDL_RWread(io, &buf[0], buf.size(), 1) <= 0) {
        ReportSDL("error reading from '%s': %s", name, SDL_GetError());
    }
    if (SDL_RWclose(io) != 0) {
        ReportSDL("error closing file '%s': %s", name, SDL_GetError());
    }

    return buf;
}

static bool closeLogCleanup(const char* reason, bool upload)
{
    os_cleanup();
    
    if (!g_logfile)
        return false;
 
    ReportSDL("Closing log: %s\n", reason);
    g_logstate = LOG_CLOSED;
    SDL_RWwrite(g_logfile, OL_ENDL, strlen(OL_ENDL), 1);
    SDL_RWclose(g_logfile);
    fflush(NULL);
    g_logfile = NULL;

    if (!upload)
        return false;
    
    string data = loadFile(SDL_RWFromFile(g_logpath, "r"), g_logpath);
    if (!data.size())
        return false;
    
    return OLG_UploadLog(data.c_str(), data.size());
}

void sdl_os_report_crash(const string &message)
{
    ReportSDL("%s\n", message.c_str());
    fflush(NULL);

    bool success = closeLogCleanup("Crashed", true);

    static string errorm;
    if (success)
    {
        errorm = str_format("%s\n\nAnonymous log uploaded OK.\n\n%s\n", message.c_str(), g_logpath);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reassembly Error", errorm.c_str(), NULL);
        return;
    }
    
    std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
    const std::time_t cstart = std::chrono::system_clock::to_time_t(start);
    char mbstr[100];
    std::strftime(mbstr, sizeof(mbstr), "%Y%m%d_%I.%M.%S.%p", std::localtime(&cstart));
    string dest = OL_PathForFile(str_format("~/Desktop/%s_crash_%s.txt", OLG_GetName(), mbstr).c_str(), "w");
    ReportSDL("Copying log from %s to %s", g_logpath, dest.c_str());

    OL_CopyFile(g_logpath, dest.c_str());

    errorm = str_format("%s\n\nPlease email\n%s\nto arthur@anisopteragames.com\n",
                        message.c_str(), dest.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reassembly Error", errorm.c_str(), NULL);
    ReportSDL("Crash reporting complete\n");
    fflush(NULL);
}

void OL_ScheduleUploadLog(const char* reason)
{
    ReportSDL("Log upload scheduled: %s", reason);
    g_wantsLogUpload = true;
}


void anonymizeUsername(string &str)
{
#if OL_LINUX
    const char *name = "/home";
#else
    const char *name = "Users";
#endif
    for (size_t start = str.find(name);
         start != string::npos && start < str.size();
         start = str.find(name, start))
    {
        start += strlen(name) + 1;
        if (start < str.size() && strchr("/\\", str[start-1]))
        {
            int end=start+1;
            for (; end<str.size() && !strchr("/\\", str[end]); end++);
            str.replace(start, end-start, "<User>");
        }
    }
}

int OL_IsLogOpen(void)
{
    return g_logfile != NULL && g_logstate == LOG_OPEN;
}

void OL_OpenLog(void)
{
    if (g_logstate == LOG_CLOSED)
        g_logstate = LOG_INIT;
}

void OL_ReportMessage(const char *str_)
{
#if OL_WINDOWS
#if IS_DEVEL
    OutputDebugString(s2ws(str_).c_str());
#endif
#endif
    printf("%s", str_);

    if (g_logstate == LOG_CLOSED)
        return;

    string str = str_;
    anonymizeUsername(str);
    
    if (!g_logfile) {
        if (g_logstate == LOG_OPENING) {
            g_logdata += str;
            return;
        }
        g_logstate = LOG_OPENING;
        const char* path = OL_PathForFile(OLG_GetLogFileName(), "w");
        if (!g_logfile) { // may have been opened by OL_PathForFile
            OL_CreateParentDirs(path);
            g_logfile = SDL_RWFromFile(path, "w");
            if (!g_logfile)
                return;
            g_logpath = lstring(path).c_str();
            g_logstate = LOG_OPEN;
            if (g_logdata.size()) {
#if OL_WINDOWS
                g_logdata = str_replace(g_logdata, "\n", OL_ENDL);
#endif
                SDL_RWwrite(g_logfile, g_logdata.c_str(), g_logdata.size(), 1);
                g_logdata.clear();
                g_logdata.shrink_to_fit();
            }
            // call self recursively
            ReportSDL("Log file opened at %s", path);
            const char* latestpath = OL_PathForFile("data/log_latest.txt", "w");
            os_symlink_f(g_logpath, latestpath);
        }
    }
#if OL_WINDOWS
    str = str_replace(str, '\n', OL_ENDL);
#endif
    
    SDL_RWwrite(g_logfile, str.c_str(), str.size(), 1);
}

int OL_GetFullscreen(void)
{
    const int flags = SDL_GetWindowFlags(g_displayWindow);
    if (flags&SDL_WINDOW_BORDERLESS)
        return 1;
    else if (flags&(SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_FULLSCREEN))
        return 2;
    else
        return 0;
}

// 0 is windows, 1 is "fake" fullscreen, 2 is "true" fullscreen
void OL_SetFullscreen(int fullscreen)
{
    const int wasfs = OL_GetFullscreen();
    
#if !OL_WINDOWS
    if (fullscreen)
        fullscreen = 2;
#endif

    if (fullscreen != wasfs)
    {
        g_supportsTearControl = -1; // reset

        // disable / save old state
        if (wasfs == 0)
        {
            ReportSDL("Saving windowed window pos");
            SDL_GetWindowPosition(g_displayWindow, &g_savedWindowPos.x, &g_savedWindowPos.y);
            SDL_GetWindowSize(g_displayWindow, &g_savedWindowPos.w, &g_savedWindowPos.h);
        }
        else if (wasfs == 1)
        {
            ReportSDL("Disabled Fake Fullscreen %d,%d/%dx%d",
                      g_savedWindowPos.x, g_savedWindowPos.y, g_savedWindowPos.w, g_savedWindowPos.h);
            SDL_SetWindowBordered(g_displayWindow, SDL_TRUE);
            SetWindowResizable(g_displayWindow, SDL_TRUE);
        }
        else if (wasfs == 2)
        {
            ReportSDL("Disabled Fullscreen");
            SDL_SetWindowFullscreen(g_displayWindow, 0);
#if OL_LINUX
            SDL_SetWindowGrab(g_displayWindow, SDL_FALSE);
#endif
        }

        // enable new state
        if (fullscreen == 0)
        {
            ReportSDL("Restoring windowed window pos");
            SDL_SetWindowSize(g_displayWindow, g_savedWindowPos.w, g_savedWindowPos.h);
            SDL_SetWindowPosition(g_displayWindow, g_savedWindowPos.x, g_savedWindowPos.y);
        }
        else if (fullscreen == 1)
        {
            int idx = SDL_GetWindowDisplayIndex(g_displayWindow);
            SDL_Rect bounds;
            SDL_GetDisplayBounds(idx, &bounds);
                
            ReportSDL("Enabled Fake Fullscreen %d,%d/%dx%d (from %d,%d/%dx%d)",
                      bounds.x, bounds.y, bounds.w, bounds.h,
                      g_savedWindowPos.x, g_savedWindowPos.y, g_savedWindowPos.w, g_savedWindowPos.h);

            SDL_SetWindowBordered(g_displayWindow, SDL_FALSE);
            SetWindowResizable(g_displayWindow, SDL_FALSE);
            SDL_SetWindowPosition(g_displayWindow, bounds.x, bounds.y);
            SDL_SetWindowSize(g_displayWindow, bounds.w, bounds.h);
        }
        else if (fullscreen == 2)
        {
            ReportSDL("Enabled Fullscreen");
            SDL_SetWindowFullscreen(g_displayWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
#if OL_LINUX
            SDL_SetWindowGrab(g_displayWindow, SDL_TRUE);
#endif
        }
    }
}

double OL_GetCurrentTime()
{
    const Uint64 count = SDL_GetPerformanceCounter();

    static Uint64 start = count;
    static double frequency = 0.0;
    
    if (frequency == 0.0)
        frequency = (double) SDL_GetPerformanceFrequency();

    const Uint64 rel = count - start;
    return (double) rel / frequency;
}

const char* OL_GetPlatformDateInfo(void)
{
    static string str;
    
    str = os_get_platform_info();

    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);

    const int    cpucount = SDL_GetCPUCount();
    const int    rammb    = os_get_system_ram();
    const double ramGb    = rammb / 1024.0;

    str += str_format(" SDL %d.%d.%d, %s with %d cores %.1f GB, ",
                      linked.major, linked.minor, linked.patch,
                      str_cpuid().c_str(), cpucount, ramGb);

    std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
    std::time_t cstart = std::chrono::system_clock::to_time_t(start);
    str += std::ctime(&cstart);
    str = str_strip(str);

    return str.c_str();
}

int OL_GetCpuCount()
{
    return SDL_GetCPUCount();
}

int OL_DoQuit()
{
    int was = g_quitting;
    g_quitting = true;
    return was;
}

int OL_IsQuitting()
{
    return g_quitting;
}

void sdl_set_scaling_factor(float factor)
{
    g_scaling_factor = factor;
}

void OL_GetWindowSize(float *pixelWidth, float *pixelHeight, float *pointWidth, float *pointHeight)
{
    *pixelWidth = g_windowSize.x;
    *pixelHeight = g_windowSize.y;

    *pointWidth = g_windowSize.x / g_scaling_factor;
    *pointHeight = g_windowSize.y / g_scaling_factor;
}

void OL_SetWindowSizePoints(int w, int h)
{
    if (!g_displayWindow)
        return;
    SDL_SetWindowSize(g_displayWindow, w * g_scaling_factor, h * g_scaling_factor);
}

void OL_SetSwapInterval(int interval)
{
    const int error = SDL_GL_SetSwapInterval(interval);
    // ReportSDL("SetSwapInterval(%d) -> %d", interval, error);
    g_swapInterval = interval;
    if (interval < 0) {
        const int supports = error ? 0 : 1;
        if (supports != g_supportsTearControl) {
            ReportSDL("Tear Control %s Supported: %s", supports ? "is" : "is NOT",
                      error ? SDL_GetError() : "OK");
            g_supportsTearControl = supports;
        }
        if (!supports)
            OL_SetSwapInterval(1);
    }
}

int OL_HasTearControl(void)
{
    return g_supportsTearControl;
}

float OL_GetCurrentBackingScaleFactor(void)
{
    return g_scaling_factor;
}

struct OutlawImage OL_LoadImage(const char* fname)
{
    OutlawImage img;
    memset(&img, 0, sizeof(img));
    
    const char *buf = OL_PathForFile(fname, "r");
    ReportSDL("loading [%s]...", buf);

    SDL_Surface *surface = IMG_Load(buf);
 
    if (!surface) {
        ReportSDL("SDL could not load '%s': %s", buf, SDL_GetError());
        return img;
    }

    GLenum texture_format = 0;
    const int nOfColors = surface->format->BytesPerPixel;
    if (nOfColors == 4) {
        texture_format = (surface->format->Rmask == 0x000000ff) ? GL_RGBA : GL_BGRA;
    } else if (nOfColors == 3) {
        texture_format = (surface->format->Rmask == 0x000000ff) ? GL_RGB : GL_BGR;
    } else if (nOfColors == 2) {
        texture_format = GL_LUMINANCE_ALPHA;
    } else {
        texture_format = GL_LUMINANCE;
    }

    ReportSDL("texture has %d colors, %dx%d pixels", nOfColors, surface->w, surface->h);

    img.width = surface->w;
    img.height = surface->h;
    img.type = GL_UNSIGNED_BYTE;
    img.format = texture_format;
    img.data = (char*) surface->pixels;
    img.handle = surface;

    return img;
}

void OL_FreeImage(OutlawImage *img)
{
    SDL_FreeSurface((SDL_Surface*)img->handle);
}

int OL_SaveImage(const OutlawImage *img, const char* fname)
{
    if (!img || !img->data || img->width <= 0|| img->height <= 0)
        return 0;

    int success = false;
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(img->data, img->width, img->height, 32, img->width*4,
                                                 0x000000ff, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (surf)
    {
        const char *path = OL_PathForFile(fname, "w");
        if (OL_CreateParentDirs(path)) {
            success = IMG_SavePNG(surf, path) == 0;
        }
        if (!success) {
            ReportSDL("Failed to write image %dx%d to '%s': %s",
                      img->width, img->height, path, SDL_GetError());
        }
        SDL_FreeSurface(surf);
    }
    else
    {
        ReportSDL("Failed to create surface %%dx%d: %s",
                  img->width, img->height, SDL_GetError());
    }
    
    return success;
}

static lstring                             g_fontFiles[OL_MAX_FONTS];
static std::unordered_map<uint, TTF_Font*> g_fonts;
static std::mutex                          g_fontMutex;

TTF_Font* getFont(int index, float size)
{
    std::lock_guard<std::mutex> l(g_fontMutex);
    if (0 > index || index > OL_MAX_FONTS)
        return NULL;

    const int   isize = round_int(size * g_scaling_factor);
    const uint  key   = (index<<16)|isize;
    TTF_Font*  &font  = g_fonts[key];

    if (!font)
    {
        lstring file = g_fontFiles[index];
        if (!file)
            return NULL;

        font = TTF_OpenFont(file.c_str(), isize);
        if (font) {
            ReportSDL("Loaded font %d '%s' at size %d", index, file.c_str(), isize);
        } else {
            ReportSDL("Failed to load font '%s' at size '%d': %s",
                      file.c_str(), isize, TTF_GetError());
        }
        ASSERT(font);
    }
    return font;
}

void OL_SetFont(int index, const char* file)
{
    const char* fname = OL_PathForFile(file, "r");
    if (fname && OL_FileDirectoryPathExists(fname))
    {
        std::lock_guard<std::mutex> l(g_fontMutex);
        g_fontFiles[index] = lstring(fname);
        for (auto it = g_fonts.begin(); it != g_fonts.end(); )
        {
            if ((it->first >> 16) == index)
            {
                TTF_CloseFont(it->second);
                it = g_fonts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    TTF_Font *font = getFont(index, 12);
    ReportSDL("Found font %d at '%s': %s family:'%s' style:'%s'",
              index, fname, font ? "OK" : "FAILED",
              font ? TTF_FontFaceFamilyName(font) : "",
              font ? TTF_FontFaceStyleName(font) : "");
}

void OL_FontAdvancements(int fontName, float size, struct OLSize* advancements)
{
    TTF_Font* font = getFont(fontName, size);
    if (!font)
        return;
    for (uint i=0; i<128; i++)
    {
        int minx,maxx,miny,maxy,advance;
        if (TTF_GlyphMetrics(font,i,&minx,&maxx,&miny,&maxy,&advance) == 0)
        {
            advancements[i].x = advance / g_scaling_factor;
        }
        else
        {
            ReportSDL("Error getting glyph size for glyph %d/'%c'", i, i);
            advancements[i].x = 0.f;
        }
        advancements[i].y = 0.f;
    }
}

float OL_FontHeight(int fontName, float size)
{
    const TTF_Font* font = getFont(fontName, size);
    return font ? TTF_FontLineSkip(font) / g_scaling_factor : 0.f;
}

static SDL_Color getQuake3Color(int val)
{
    const uint color = OLG_GetQuake3Color(val);
    SDL_Color sc;
    sc.r = (color>>16);
    sc.g = (color>>8)&0xff;
    sc.b = color&0xff;
    sc.a = 0xff;
    return sc;
}

struct Strip {
    TTF_Font    *font = NULL;
    int          pixel_width = 0;
    SDL_Color    color;
    std::string  text;
};

struct StringRenderer {

    const int font_index = 0;
    const float font_size = 0;
    TTF_Font* const orig_font;
    TTF_Font* last_font = NULL;
    int last_font_index = 0;

    vector< Strip > strips;

    int strip_start = 0;
    int line_pixel_width = 0;

    size_t textlen;
    const size_t totallen;
    const char* text_remaining;
    const char * const text;

    SDL_Color color;

    size_t chr_start = 0;
    size_t chr_end = 0;

    StringRenderer(const char* str, float size, int font) :
        font_index(font), font_size(size), orig_font(getFont(font, size)),
        last_font(orig_font),
        textlen(SDL_strlen(str)), totallen(textlen), text_remaining(str), text(str)
    {
        memset(&color, 0xff, sizeof(color));
    }

    int StringImage(OutlawImage *img);

    TTF_Font *findFont(Uint32 chr) const
    {
        if (TTF_GlyphIsProvided(orig_font, chr))
            return orig_font;
        else if (orig_font != last_font && TTF_GlyphIsProvided(last_font, chr))
            return last_font;
        
        for (int j=0; j<arraySize(g_fontFiles); j++)
        {
            if (j == font_index)
                continue;
            TTF_Font *fnt = getFont(j, font_size);
            if (!fnt)
                break;
            if (TTF_GlyphIsProvided(fnt, chr)) {
                return fnt;
            }
        }
        ReportSDL("No font for %#x", chr);
        return orig_font;  // just use boxes, oh well...
    }

    Uint32 getChr()
    {
        chr_start = totallen - textlen;        
        Uint32 chr = textlen ? utf8_getch(&text_remaining, &textlen) : '\0';
        chr_end = totallen - textlen;
        return chr;
    }

    void pushStrip(bool newline)
    {
        if (!newline && chr_start <= strip_start)
            return;
        Strip st;
        st.font = last_font;
        st.color = color;
        st.text = string(&text[strip_start], chr_start - strip_start);

        if (st.text.size()) {
            int pixel_height = 0;
            if (TTF_SizeUTF8(st.font, st.text.c_str(), &st.pixel_width, &pixel_height)) {
                ReportSDLErr("TTF_SizeUTF8");
                st.text = string();
                st.pixel_width = 0;
            }
        }

        line_pixel_width += st.pixel_width;
        if (newline)
            st.pixel_width = -1;
        if (st.pixel_width) {
            if (kTTFDebug) {
                const uint color = (st.color.a<<24)|(st.color.r<<16)|(st.color.g<<8)|(st.color.b);
                ReportSDL("Strip %d '%s' %#x %dpx %#x %s", (int)strips.size(),
                          st.text.c_str(), utf8_getch(st.text),
                          st.pixel_width, color, TTF_FontFaceFamilyName(st.font));
            }
            strips.push_back(std::move(st));
        }
        strip_start = chr_start;
    }

};

static bool ignoreCharacter(Uint32 chr)
{
    // combining characters not rendered correctly: https://en.wikipedia.org/wiki/Combining_character
    return ((0x0300 <= chr && chr < 0x036F) ||
            (0x1AB0 <= chr && chr < 0x1AFF) || 
            (0x1DC0 <= chr && chr < 0x1DFF) || 
            (0x20D0 <= chr && chr < 0x20FF) ||
            (0xFE20 <= chr && chr < 0xFE2F));
}

int StringRenderer::StringImage(OutlawImage *img)
{
    if (!orig_font)
        return 0;

    int color_count = 0;
    int newlines = 0;
    int text_pixel_width = 0;

    // split string up by lines, fallback font
    while (1)
    {
        const Uint32 chr = getChr();

        if (ignoreCharacter(chr))
        {
            pushStrip(false);
            strip_start = chr_end;
        }
        else if (chr == '^' && textlen > 0 && '0' <= text[chr_end] && text[chr_end] <= '9')
        {
            pushStrip(false);
            const Uint32 num = getChr();
            strip_start = chr_end;
            color_count++;
            color = getQuake3Color(num - '0');
        }
        else if (chr == '\n' || chr == '\0' || chr == UNKNOWN_UNICODE)
        {
            pushStrip(true);
            newlines++;
            text_pixel_width = max(text_pixel_width, line_pixel_width);
            line_pixel_width = 0;
            strip_start = chr_end;
            if (chr == '\0' || chr == UNKNOWN_UNICODE)
                break;
        }
        else
        {
            TTF_Font *fnt = findFont(chr);
            if (fnt != last_font)
            {
                pushStrip(false);
                last_font = fnt;
            }
        }
    }

    int line_pixel_height = 0;
    foreach (const Strip &st, strips)
        line_pixel_height = max(line_pixel_height, TTF_FontLineSkip(st.font));
    const int text_pixel_height = newlines * line_pixel_height + 1; // blended text sometimes slightly higher than expected

    SDL_Surface *intermediary = SDL_CreateRGBSurface(0, text_pixel_width, text_pixel_height, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    SDL_Rect dstrect = {};

    int i=0;
    foreach (const Strip &st, strips)
    {
        if (kTTFDebug)
            ReportSDL("Rendering strip %d/%d", i, (int)strips.size());
        if (st.text.size())
        {
            ASSERT(st.font);
            SDL_Surface* initial = TTF_RenderUTF8_Blended(st.font, st.text.c_str(), st.color);
            if (initial)
            {
                ASSERT(initial->w == st.pixel_width || st.pixel_width == -1);
                ASSERT(initial->h <= line_pixel_height + 1);
                if (SDL_SetSurfaceBlendMode(initial, SDL_BLENDMODE_NONE))
                    ReportSDLErr("SDL_SetSurfaceBlendMode");
                if (SDL_BlitSurface(initial, NULL, intermediary, &dstrect))
                    ReportSDLErr("SDL_BlitSurface");
                SDL_FreeSurface(initial);
            }
            else
            {
                ReportSDL("TTF Error: %s", TTF_GetError());
            }
        }

        if (st.pixel_width < 0) {
            dstrect.y += line_pixel_height;
            dstrect.x = 0;
        } else {
            dstrect.x += st.pixel_width;
        }
        i++;
    }

    img->width = text_pixel_width;
    img->height = text_pixel_height;
    img->internal_format = color_count ? GL_RGBA : GL_LUMINANCE_ALPHA;
    img->format = GL_BGRA;
    img->type = GL_UNSIGNED_BYTE;
    img->data = (char*)intermediary->pixels;
    img->handle = intermediary;
    return 1;
}


int OL_StringImage(OutlawImage *img, const char* str, float size, int font, float maxw, float maxh)
{
    StringRenderer ss(str, size, font);
    return ss.StringImage(img);
}

static int keysymToKey(const SDL_Keysym &keysym)
{
    const SDL_Keycode sym = keysym.sym;
    
    if (keysym.mod & KMOD_SHIFT) {
        // a -> A
        if ('a' <= sym && sym <= 'z')
            return sym - 32;
        // 1 -> !
        switch (sym) {
        case SDLK_1: return SDLK_EXCLAIM;
        case SDLK_2: return SDLK_AT;
        case SDLK_3: return SDLK_HASH;
        case SDLK_4: return SDLK_DOLLAR;
        case SDLK_5: return SDLK_PERCENT;
        case SDLK_6: return SDLK_CARET;
        case SDLK_7: return SDLK_AMPERSAND;
        case SDLK_8: return SDLK_ASTERISK;
        case SDLK_9: return SDLK_LEFTPAREN;
        case SDLK_0: return SDLK_RIGHTPAREN;
        case SDLK_SLASH: return SDLK_QUESTION;
        case SDLK_MINUS: return SDLK_UNDERSCORE;
        case SDLK_EQUALS: return SDLK_PLUS;
        case SDLK_SEMICOLON: return SDLK_COLON;
        case SDLK_COMMA: return SDLK_LESS;
        case SDLK_PERIOD: return SDLK_GREATER;
        case SDLK_LEFTBRACKET: return '{';
        case SDLK_RIGHTBRACKET: return '}';
        case SDLK_QUOTE: return '"';
        case SDLK_BACKSLASH: return '|';
        case SDLK_BACKQUOTE: return '~';
        default:
            ;
        }
    }
    // ascii
    if (sym < 127)
        return sym;
    
    switch (sym)
    {
    case SDLK_LEFT:     return NSLeftArrowFunctionKey;
    case SDLK_RIGHT:    return NSRightArrowFunctionKey;
    case SDLK_UP:       return NSUpArrowFunctionKey;
    case SDLK_DOWN:     return NSDownArrowFunctionKey;
    case SDLK_PAGEUP:   return NSPageUpFunctionKey;
    case SDLK_PAGEDOWN: return NSPageDownFunctionKey;
    case SDLK_HOME:     return NSHomeFunctionKey;
    case SDLK_END:      return NSEndFunctionKey;
    case SDLK_PRINTSCREEN: return NSPrintScreenFunctionKey;
    case SDLK_INSERT:   return NSInsertFunctionKey;
    case SDLK_PAUSE:    return NSPauseFunctionKey;
    case SDLK_SCROLLLOCK: return NSScrollLockFunctionKey;
    case SDLK_F1:       return NSF1FunctionKey;
    case SDLK_F2:       return NSF2FunctionKey;
    case SDLK_F3:       return NSF3FunctionKey;
    case SDLK_F4:       return NSF4FunctionKey;
    case SDLK_F5:       return NSF5FunctionKey;
    case SDLK_F6:       return NSF6FunctionKey;
    case SDLK_F7:       return NSF7FunctionKey;
    case SDLK_F8:       return NSF8FunctionKey;
    case SDLK_F9:       return NSF9FunctionKey;
    case SDLK_F10:      return NSF10FunctionKey;
    case SDLK_F11:      return NSF11FunctionKey;
    case SDLK_F12:      return NSF12FunctionKey;
    case SDLK_KP_0:     return Keypad0;
    case SDLK_KP_1:     return Keypad1;
    case SDLK_KP_2:     return Keypad2;
    case SDLK_KP_3:     return Keypad3;
    case SDLK_KP_4:     return Keypad4;
    case SDLK_KP_5:     return Keypad5;
    case SDLK_KP_6:     return Keypad6;
    case SDLK_KP_7:     return Keypad7;
    case SDLK_KP_8:     return Keypad8;
    case SDLK_KP_9:     return Keypad9;
    case SDLK_KP_ENTER: return '\r';
    case SDLK_KP_EQUALS: return '=';
    case SDLK_KP_PLUS:  return '+';
    case SDLK_KP_MINUS: return '-';
    case SDLK_KP_DIVIDE: return '/';
    case SDLK_KP_MULTIPLY: return '*';
    case SDLK_KP_PERIOD: return '.';
    case SDLK_APPLICATION: return NSMenuFunctionKey;
    case SDLK_RSHIFT:   // fallthrough
    case SDLK_LSHIFT:   return OShiftKey;
    case SDLK_CAPSLOCK: // fallthrough
    case SDLK_RCTRL:    // fallthrough
    case SDLK_LCTRL:    return OControlKey;
        //case SDLK_RMETA:    // fallthrough
        //case SDLK_LMETA:    // fallthrough
    case SDLK_RALT:     // fallthrough
    case SDLK_LALT:     return OAltKey;
    case SDLK_LGUI:     //fallthrough (windows / apple key)
    case SDLK_RGUI:     return OControlKey;
    case SDLK_BACKSPACE: return NSBackspaceCharacter;
    case SDLK_DELETE:   return NSDeleteFunctionKey;
    case SDLK_VOLUMEUP: return KeyVolumeUp;
    case SDLK_VOLUMEDOWN: return KeyVolumeDown;
    case SDLK_AUDIONEXT: return KeyAudioNext;
    case SDLK_AUDIOPREV: return KeyAudioPrev;
    case SDLK_AUDIOPLAY: return KeyAudioPlay;
    case SDLK_AUDIOSTOP: return KeyAudioStop;
    case SDLK_AUDIOMUTE: return KeyAudioMute;
        
    default:
        ASSERTF(sym < 0xffff, "%#x", sym);
        return sym;
    }
}


static void HandleEvents()
{
    SDL_Event evt;
    while (SDL_PollEvent(&evt))
    {
        if (Controller_HandleEvent(&evt))
            continue;

        OLEvent e;
        memset(&e, 0, sizeof(e));

        switch (evt.type)
        {
        case SDL_WINDOWEVENT:
        {
            switch (evt.window.event) {
            case SDL_WINDOWEVENT_SHOWN:
                ReportSDL("Window %d shown", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_HIDDEN:
                ReportSDL("Window %d hidden", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_EXPOSED:
                //ReportSDL("Window %d exposed", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_MOVED:
                ReportSDL("Window %d moved to %d,%d",
                        evt.window.windowID, evt.window.data1,
                        evt.window.data2);
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                g_windowSize.x = evt.window.data1;
                g_windowSize.y = evt.window.data2;
                glViewport(0, 0, g_windowSize.x, g_windowSize.y);
                ReportSDL("Window %d size changed to %dx%d", evt.window.windowID, 
                          evt.window.data1, evt.window.data2);
                break;
            case SDL_WINDOWEVENT_RESIZED:
                g_windowSize.x = evt.window.data1;
                g_windowSize.y = evt.window.data2;
                glViewport(0, 0, g_windowSize.x, g_windowSize.y);
                ReportSDL("Window %d resized to %dx%d", evt.window.windowID, 
                          evt.window.data1, evt.window.data2);
                break;
            case SDL_WINDOWEVENT_MINIMIZED:
                ReportSDL("Window %d minimized", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_MAXIMIZED:
                ReportSDL("Window %d maximized", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_RESTORED:
                ReportSDL("Window %d restored", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_ENTER:
                //ReportSDL("Mouse entered window %d", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_LEAVE:
                //ReportSDL("Mouse left window %d", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
               // ReportSDL("Window %d gained keyboard focus", evt.window.windowID);
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST: {
                ReportSDL("Window %d lost keyboard focus", evt.window.windowID);
                e.type = OL_LOST_FOCUS;
                OLG_OnEvent(&e);
                break;
            }
            case SDL_WINDOWEVENT_CLOSE:
                ReportSDL("Window %d closed", evt.window.windowID);
                OLG_DoClose();
                break;
            default:
                ReportSDL("Window %d got unknown event %d",
                        evt.window.windowID, evt.window.event);
                break;
            }
            break;
        }
        case SDL_KEYUP:         // fallthrough
        case SDL_KEYDOWN:
        {
            e.type = (evt.type == SDL_KEYDOWN) ? OL_KEY_DOWN : OL_KEY_UP;
            e.key = keysymToKey(evt.key.keysym);

            //ReportSDL("key %s %d %c\n", (evt.type == SDL_KEYDOWN) ? "down" : "up", evt.key.keysym.sym, e.key);

            if (e.key)
            {
                OLG_OnEvent(&e);
            }

            break;
        }
        case SDL_MOUSEMOTION:
        {
            e.dx = evt.motion.xrel / g_scaling_factor;
            e.dy = evt.motion.yrel / g_scaling_factor;
            e.x = evt.motion.x / g_scaling_factor;
            e.y = (g_windowSize.y - evt.motion.y) / g_scaling_factor;
            const Uint8 state = evt.motion.state;
            const int key = ((state&SDL_BUTTON_LMASK)  ? 0 :
                             (state&SDL_BUTTON_RMASK)  ? 1 :
                             (state&SDL_BUTTON_MMASK)  ? 2 :
                             (state&SDL_BUTTON_X1MASK) ? 3 :
                             (state&SDL_BUTTON_X2MASK) ? 4 : -1);
            if (key == -1) {
                e.type = OL_MOUSE_MOVED;
            } else {
                e.key = key;
                e.type = OL_MOUSE_DRAGGED;
            }
            OLG_OnEvent(&e);
            break;
        }
        case SDL_MOUSEWHEEL:
        {
            e.type = OL_SCROLL_WHEEL;
            e.dy = 5.f * evt.wheel.y;
            e.dx = evt.wheel.x;
            OLG_OnEvent(&e);
            break;
        }
        case SDL_MOUSEBUTTONDOWN: // fallthrorugh
        case SDL_MOUSEBUTTONUP:
        {
            e.x = evt.button.x / g_scaling_factor;
            e.y = (g_windowSize.y - evt.button.y) / g_scaling_factor;
            e.type = evt.type == SDL_MOUSEBUTTONDOWN ? OL_MOUSE_DOWN : OL_MOUSE_UP;
            switch (evt.button.button)
            {
            case SDL_BUTTON_LEFT:   e.key = 0; break;
            case SDL_BUTTON_RIGHT:  e.key = 1; break;
            case SDL_BUTTON_MIDDLE: e.key = 2; break;
            case SDL_BUTTON_X1:     e.key = 3; break;
            case SDL_BUTTON_X2:     e.key = 4; break;
            default:                e.key = 0; break;
            }
            OLG_OnEvent(&e);
            break;
        }
        case SDL_QUIT:
            ReportSDL("SDL_QUIT received");
            OLG_DoClose();
            break;
        }
    }
}

void OL_Present(void)
{
    SDL_GL_SwapWindow(g_displayWindow);
}


void OL_ThreadBeginIteration()
{
}

struct AutoreleasePool {
    
    std::list<std::string> pool;

    static AutoreleasePool &instance() 
    {
        static THREAD_LOCAL AutoreleasePool *p = NULL;
        if (!p)
            p = new AutoreleasePool();
        return *p;
    }

    const char* autorelease(std::string &val)
    {
        pool.push_back(std::move(val));
        return pool.back().c_str();
    }

    void drain()
    {
        pool.clear();
    }
};

const char* sdl_os_autorelease(std::string &val)
{
    if (val.empty())
        return NULL;
    return AutoreleasePool::instance().autorelease(val);
}

void OL_ThreadEndIteration()
{
    AutoreleasePool::instance().drain();
}

void OL_WarpCursorPosition(float x, float y)
{
    SDL_WarpMouseInWindow(g_displayWindow, (int)x * g_scaling_factor, g_windowSize.y - (int) (y * g_scaling_factor));
}

const char* OL_ReadClipboard()
{
    char *ptr = SDL_GetClipboardText();
    if (!ptr)
        return NULL;
    string str = ptr;
#if OL_WINDOWS
    str_replace(ptr, OL_ENDL, "\n");
#endif
    SDL_free(ptr);
    return sdl_os_autorelease(str);
}

void OL_WriteClipboard(const char* txt)
{
#if OL_WINDOWS
    string str = str_replace(txt, "\n", OL_ENDL);
    SDL_SetClipboardText(str.c_str());
#else
    SDL_SetClipboardText(txt);
#endif
}

#define COPY_GL_EXT_IMPL(X) if (!(X) && (X ## EXT)) { ReportSDL("Using " #X "EXT"); (X) = (X ## EXT); } else if (!(X)) { ReportSDL(#X " Not found!"); }
#define ASSERT_EXT_EQL(X) static_assert(X == X ## _EXT, #X "EXT mismatch")

static bool initGlew()
{
    ReportSDL("GLEW Version: %s", glewGetString(GLEW_VERSION));
    
    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        ReportSDL("glewInit() Failed: %s", glewGetErrorString(err));
        // keep going to get as much log as possible
    }
    // GL_EXT_framebuffer_blit
    COPY_GL_EXT_IMPL(glBlitFramebuffer);

    // GL_EXT_framebuffer_object
    ASSERT_EXT_EQL(GL_FRAMEBUFFER);
    ASSERT_EXT_EQL(GL_RENDERBUFFER);
    ASSERT_EXT_EQL(GL_DEPTH_ATTACHMENT);
    ASSERT_EXT_EQL(GL_COLOR_ATTACHMENT0);
    ASSERT_EXT_EQL(GL_FRAMEBUFFER_COMPLETE);
    ASSERT_EXT_EQL(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    COPY_GL_EXT_IMPL(glBindFramebuffer);
    COPY_GL_EXT_IMPL(glBindRenderbuffer);
    COPY_GL_EXT_IMPL(glCheckFramebufferStatus);
    COPY_GL_EXT_IMPL(glDeleteFramebuffers);
    COPY_GL_EXT_IMPL(glDeleteRenderbuffers);
    COPY_GL_EXT_IMPL(glFramebufferRenderbuffer);
    COPY_GL_EXT_IMPL(glFramebufferTexture2D);
    COPY_GL_EXT_IMPL(glGenFramebuffers);
    COPY_GL_EXT_IMPL(glGenRenderbuffers);
    COPY_GL_EXT_IMPL(glGenerateMipmap);
    COPY_GL_EXT_IMPL(glIsFramebuffer);
    COPY_GL_EXT_IMPL(glIsRenderbuffer);
    COPY_GL_EXT_IMPL(glRenderbufferStorage);

    // make sure we print the gl version no matter what!
    const char* error = NULL;
    const int status = OLG_InitGL(&error);
    if (error)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OpenGL Error", error, NULL);
    }
    return status == 1 ? true : false;
}

void OL_Sleep(double sleep)
{
    if (sleep <= 0.0)
        return;
    // 0. sleep_for consistently sleeps slightly too long
    // 1. SDL_Delay has low CPU, is pretty accurate
    // 2. spinning is very accurate but wastes power
    // adaptive vsync / tear control is the best though!
    if (kFrameSleepMethod == 0)
        std::this_thread::sleep_for(std::chrono::microseconds(floor_int(1e6 * sleep)));
    else if (kFrameSleepMethod == 1)
        SDL_Delay(floor_int(sleep * 1000.0));
    else if (kFrameSleepMethod == 2) {
        double start = OL_GetCurrentTime();
        do {
#ifdef YieldProcessor
            YieldProcessor();
#endif
        } while (OL_GetCurrentTime() - start < sleep);
    }
}

static int init_sdl()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER ) < 0)
    {
        ReportSDL("SDL_Init Failed (retrying without gamepad): %s", SDL_GetError());
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            return 1;
        }
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    if (kOpenGLDebug)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    return 0;
}

int sdl_os_main(int argc, const char **argv)
{
    ReportSDL("sdl_os_main()");
    int mode = OLG_Init(argc, argv);

    if (init_sdl() && mode)
    {
        sdl_os_report_crash(str_format("SDL_Init() failed: %s", SDL_GetError()));
        return 1;
    }
    
    if (!os_init())
        return 1;
    
    if (mode == 0)
    {
        SDL_Window *window = SDL_CreateWindow("OpenGL test", -32, -32, 32, 32, SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        SDL_GLContext context = NULL;
        if (window) {
            context = SDL_GL_CreateContext(window);
            if (context && !initGlew())
                return 1;
        }
        
        OLG_Draw();

        if (context)
            SDL_GL_DeleteContext(context);
        if (window)
            SDL_DestroyWindow(window);
        ReportSDL("Goodbye!\n");
        return 0;
    }

    {
        g_windowSize.x = 960;
        g_windowSize.y = 600;

        const int displayCount = SDL_GetNumVideoDisplays();

        for (int i=0; i<displayCount; i++)
        {
            SDL_DisplayMode mode;
            SDL_GetDesktopDisplayMode(i, &mode);
            ReportSDL("Display %d of %d is %dx%d@%dHz: %s", i+1, displayCount, mode.w, mode.h, mode.refresh_rate,
                      SDL_GetDisplayName(i));

            if (i == 0)
                g_windowSize = int2(mode.w, mode.h);
            
            if (mode.w>0 && mode.h>0)
            {
                g_windowSize = min(g_windowSize, int2(0.9f * float2(mode.w, mode.h)));
            }
        }
        g_windowSize = clamp_aspect(max(int2(640, 480), g_windowSize), 1.6, 2.f);
        ReportSDL("Requesting initial window size of %dx%d", g_windowSize.x, g_windowSize.y);
        ReportSDL("Current SDL video driver is '%s'", SDL_GetCurrentVideoDriver());
    }
 
    g_displayWindow = SDL_CreateWindow(OLG_GetName(), 
                                       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                       g_windowSize.x, g_windowSize.y,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_displayWindow) {
        sdl_os_report_crash(str_format("SDL_CreateWindow failed: %s\nIs your desktop set to 32 bit color?", SDL_GetError()).c_str());
    }

    if (kMaximizeWindow)
        SDL_MaximizeWindow(g_displayWindow);

    SDL_GetWindowPosition( g_displayWindow, &g_savedWindowPos.x, &g_savedWindowPos.y );
    SDL_GetWindowSize( g_displayWindow, &g_savedWindowPos.w, &g_savedWindowPos.h );

    ReportSDL("Initial window size+position is %dx%d+%dx%d", g_savedWindowPos.w, g_savedWindowPos.h,
              g_savedWindowPos.x, g_savedWindowPos.y);

#if OL_LINUX
    const char* spath = OL_PathForFile("linux/reassembly_icon.png", "r");
    SDL_Surface *surface = IMG_Load(spath);
    if (surface)
    {
        SDL_SetWindowIcon(g_displayWindow, surface);
        SDL_FreeSurface(surface);
    }
    else
    {
        ReportSDL("Failed to load icon from '%s'", spath);
    }
#endif

    SDL_GLContext glcontext = SDL_GL_CreateContext(g_displayWindow);
    if (!glcontext) {
        ReportSDL("SDL_GL_CreateContext failed: %s", SDL_GetError());
    }
    
    if (!initGlew())
        return 1;
 
    SDL_ShowCursor(0);
    if (TTF_Init() != 0)
    {
        sdl_os_report_crash(str_format("TTF_Init() failed: %s", TTF_GetError()));
        return 1;
    }

    while (!g_quitting)
    {
        const double start = OL_GetCurrentTime();
        HandleEvents();
        OLG_Draw();

        if (g_swapInterval == 0)
        {
            const float targetFPS = OLG_GetTargetFPS();
            if (targetFPS > 0.f)
            {
                const double frameTime = max(0.0, OL_GetCurrentTime() - start);
                const double targetFrameTime = 1.0 / targetFPS;
                const double sleep = targetFrameTime - frameTime;

                OL_Sleep(sleep);
            }
        }
    }

    OLG_OnQuit();

    if (g_wantsLogUpload)
        ReportSDL("Log upload requested");
    closeLogCleanup("Shutdown", g_wantsLogUpload);

    SDL_DestroyWindow(g_displayWindow);

    TTF_Quit();
    SDL_Quit();

    ReportSDL("Good bye!\n");
    return 0;
}
