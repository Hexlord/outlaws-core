//
// GUI.cpp - widget library
//

// Copyright (c) 2013-2016 Arthur Danskin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "StdAfx.h"
#include "ZipFile.h"

#define DEF_COLOR(X, Y) DEFINE_CVAR(uint, X, Y)
#include "GUI.h"
#undef DEF_COLOR

#include "Shaders.h"
#if HAS_SOUND
#include "Sound.h"
#define PLAY_BUTTON_HOVER() globals.sound->OnButtonHover()
#define PLAY_BUTTON_PRESS() globals.sound->OnButtonPress()
#else
#define PLAY_BUTTON_HOVER()
#define PLAY_BUTTON_PRESS()
#endif

#define HAS_KEYS 1
#if HAS_KEYS
#include "Keys.h"
#define GET_MENU_TRANSLATION(E) KeyBindings::instance().getMenuTranslation(E)
#else
struct KeyBinding;
#define GET_MENU_TRANSLATION(E) int2()
#endif

static DEFINE_CVAR(float, kScrollbarWidth, 25.f);
DEFINE_CVAR(float2, kButtonPad, float2(4.f));

bool ButtonBase::HandleEvent(const Event* event, bool* isActivate, bool* isPress)
{
    const float2 sz = 0.5f*size;

    bool handled = false;
    if (event->type == Event::KEY_DOWN || event->type == Event::KEY_UP)
    {
        if (active && event->key && (event->key == keys[0] ||
                                     event->key == keys[1] ||
                                     event->key == keys[2] ||
                                     event->key == keys[3]))
        {
            bool activate = !pressed && (event->type == Event::KEY_DOWN);
            handled       = true;
            pressed       = activate;
            if (isActivate)
                *isActivate = activate;
        }
    }
    else
    {
        if (event->isMouse())
            hovered = intersectPointRectangle(event->pos, position, sz);

        handled = visible && hovered && ((event->type == Event::MOUSE_DOWN) ||
                                         (event->type == Event::MOUSE_UP));
        // && (event->key == 0);

        const bool wasPressed = pressed;
        if (active && handled)
        {
            if (wasPressed && (event->type == Event::MOUSE_UP)) {
                if (isActivate)
                    *isActivate = true;
                pressed = false;
            } else if (!wasPressed && event->type == Event::MOUSE_DOWN) {
                if (isPress)
                    *isPress = true;
                pressed = true;
            }
        }
        else if (event->type == Event::MOUSE_MOVED || event->type == Event::LOST_FOCUS)
        {
            pressed = false;
        }
    }
    return handled;
}

bool ButtonBase::renderTooltip(const ShaderState &ss, const View& view, uint color, bool force) const
{
    if (tooltip.empty() || !visible || (!force && !hovered) || alpha < epsilon)
        return false;

    TextBox dat;
    dat.tSize = 11.f;
    dat.alpha = alpha;
    dat.fgColor = color;
    dat.bgColor = kGUIToolBg;
    dat.font = kMonoFont;
    dat.view = &view;
    dat.rad = size / 2.f;
    dat.Draw(ss, position, tooltip);
    return true;
}

void ButtonBase::renderSelected(const ShaderState &ss, uint bgcolor, uint linecolor, float alpha) const
{
    ShaderState s = ss;
    const float2 sz = 0.5f * size;
    const float2 p = position + float2(-sz.x - sz.y, 0);
    s.color32(bgcolor, alpha);
    ShaderUColor::instance().DrawTri(s, p + float2(0, sz.y), p + float2(sz.y / 2, 0), p + float2(0, -sz.y));
    s.translateZ(0.1f);
    s.color32(linecolor, alpha);
    ShaderUColor::instance().DrawLineTri(s, p + float2(0, sz.y), p + float2(sz.y / 2, 0), p + float2(0, -sz.y));
}

void ButtonBase::render(const ShaderState &ss, bool selected)
{
    if (!visible)
        return;

    {
        DMesh::Handle h(theDMesh());
        renderButton(h.mp, selected);
        h.Draw(ss);
    }

    renderContents(ss);
}

float2 Button::getTextSize() const
{
    const GLText* tx = GLText::get(textFont, textSize, text);
    float2 sz = tx->getSize();

    if (subtext.size())
    {
        const GLText* stx = GLText::get(textFont, subtextSize, text);
        sz.x = max(sz.x, stx->getSize().x);
        sz.y += stx->getSize().y;
    }
    return sz + padding;
}

void Button::renderButton(DMesh& mesh, bool selected)
{
    if (!visible)
        return;

    if (text.size() && !(style&S_FIXED))
    {
        const float2 sz = getTextSize();
        size.y = sz.y;
        size.x = max(max(size.x, sz.x), size.y * (float)kGoldenRatio);
    }

    mesh.translateZ(0.1f);

    position = floor(position) + f2(0.5f);
    size = 2.f * round(size * 0.5f);

    const uint bg = getBGColor();
    const uint fg = getFGColor(selected);

    if (style&S_BOX) {
        PushRect(&mesh.tri, &mesh.line, position, 0.5f * size, bg, fg, alpha);
    } else if (style&S_HEX) {
        PushHex(&mesh.tri, &mesh.line, position, 0.5f * size, bg, fg, alpha);
    } else if (style&S_CORNERS) {
        PushButton(&mesh.tri, &mesh.line, position, 0.5f * size, bg, fg, alpha);
    }

    mesh.translateZ(-0.1f);
}

void Button::renderContents(const ShaderState &ss)
{
    if (!visible)
        return;
    const uint tcolor = MultAlphaAXXX((!active) ? inactiveTextColor : textColor, alpha);
    const uint stc = MultAlphaAXXX(subtextColor, alpha);
    const float2 pos = position + justY(subtext.size() ? size.y * (0.5f - (textSize / (subtextSize + textSize))) : 0.f);
    const GLText::Align align = subtext.size() ? GLText::CENTERED : GLText::MID_CENTERED;

    if (style&S_FIXED)
    {
        const float w = size.x - 2.f * padding.x;
        renderButtonText(ss, pos, w, align, textFont, tcolor, &dynamicTextSize, 6.f, textSize, text);
        renderButtonText(ss, pos, w, GLText::DOWN_CENTERED, textFont, stc, &dynamicSubtextSize, 6.f, subtextSize, subtext);
    }
    else
    {
        GLText::Put(ss, pos, align, textFont, tcolor, textSize, text);
        if (subtext.size())
            GLText::Put(ss, pos, GLText::DOWN_CENTERED, textFont, stc, subtextSize, subtext);
    }
}

bool URLButton::HandleEvent(const Event* event, bool *isActivate, bool *isPress)
{
    bool activate = false;
    if (!Button::HandleEvent(event, &activate))
        return false;
    if (activate) {
        if (str_startswith(url, "http"))
            OL_OpenWebBrowser(url.c_str());
        else
            OL_OpenFolder(url.c_str());
        if (isActivate)
            *isActivate = activate;
    }
    return true;
}

void TextInputBase::setText(const char* text)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    lines.clear();
    if (text)
        pushText(text);
    if (lines.empty())
        lines.push_back(string());
    cursor = int2(lines.back().size(), lines.size()-1);
}

void TextInputBase::setLines(const vector<string> &lns)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    lines.clear();
    vec_extend(lines, lns);

    cursor = int2(lines[lines.size()-1].size(), lines.size()-1);
}

static void cursor_move_utf8(const string& line, int2& cursor, int adjust)
{
    cursor.x += adjust;
    while (0 <= cursor.x && cursor.x < line.size() && utf8_iscont(line[cursor.x]))
        cursor.x += adjust;
}

static bool forward_char(int2& cursor, deque<string> &lines, int offset)
{
    if (offset == -1)
    {
        if (cursor.x == 0) {
            if (cursor.y <= 0)
                return false;

            cursor.y--;
            cursor.x = (int)lines[cursor.y].size();
        } else {
            cursor_move_utf8(lines[cursor.y], cursor, -1);
        }
    }
    else if (offset == 1)
    {
        if (cursor.x == lines[cursor.y].size()) {
            if (cursor.y >= lines.size()-1)
                return false;
            cursor.y++;
            cursor.x = 0;
        } else
            cursor_move_utf8(lines[cursor.y], cursor, +1);
    }
    return true;
}

static void forward_when(int2& cursor, deque<string> &lines, int offset, int (*pred)(int))
{
    forward_char(cursor, lines, offset);
    while (cursor.y < lines.size() &&
           (cursor.x >= lines[cursor.y].size() ||
            pred(lines[cursor.y][cursor.x])) &&
           forward_char(cursor, lines, offset));
}

static bool delete_char(int2& cursor, deque<string> &lines)
{
    if (0 > cursor.y || cursor.y >= lines.size()) {
        return '\0';
    } else if (cursor.x > 0) {
        string &line = lines[cursor.y];
        cursor_move_utf8(line, cursor, -1);
        line = utf8_erase(line, cursor.x, 1);
        return true;
    } else if (cursor.y > 0) {
        int nx = lines[cursor.y-1].size();
        lines[cursor.y - 1].append(lines[cursor.y]);
        lines.erase(lines.begin() + cursor.y);
        cursor.y--;
        cursor.x = nx;
        return true;
    } else {
        return false;
    }
}

static void delete_region(int2& cursor, deque<string> &lines, int2 mark)
{
    if (mark.y < cursor.y || (mark.y == cursor.y && mark.x < cursor.x))
        swap(cursor, mark);

    while (mark != cursor && delete_char(mark, lines));
}

bool TextInputBase::HandleEvent(const Event* event, bool *textChanged)
{
    hovered = intersectPointRectangle(KeyState::instance().cursorPosScreen, position, 0.5f * size);
    active = forceActive || hovered;

    if (active && event->type == Event::SCROLL_WHEEL)
    {
        startChars.y += ceil_int(-event->vel.y);
        startChars.y = clamp(startChars.y, 0, max(0, (int)lines.size() - sizeChars.y));
        return true;
    }
    else if (active && event->type == Event::KEY_DOWN)
    {
        if (event->key == NSPageUpFunctionKey)
        {
            startChars.y -= sizeChars.y;
            startChars.y = clamp(startChars.y, 0, max(0, (int)lines.size() - sizeChars.y));
            return true;
        }
        else if (event->key == NSPageDownFunctionKey)
        {
            startChars.y += sizeChars.y;
            startChars.y = clamp(startChars.y, 0, max(0, (int)lines.size() - sizeChars.y));
            return true;
        }
    }

    if (locked)
        return false;

    if (textChanged)
        *textChanged = false;

    if (active && event->type == Event::KEY_DOWN)
    {
        std::lock_guard<std::recursive_mutex> l(mutex);

        cursor.y = clamp(cursor.y, 0, lines.size()-1);
        cursor.x = clamp(cursor.x, 0, lines[cursor.y].size());

        switch (KeyState::instance().keyMods() | event->key)
        {
        case MOD_CTRL|'b': forward_char(cursor, lines, -1); break;
        case MOD_CTRL|'f': forward_char(cursor, lines, +1); break;
        case MOD_CTRL|'p': cursor.y--; break;
        case MOD_CTRL|'n': cursor.y++; break;
        case MOD_CTRL|'a': cursor.x = 0; break;
        case MOD_CTRL|'e': cursor.x = lines[cursor.y].size(); break;
        case MOD_CTRL|'k': {
            string &line = lines[cursor.y];
            OL_WriteClipboard(line.substr(cursor.x).c_str());
            if (cursor.x == line.size() && cursor.y+1 < lines.size()) {
                line.append(lines[cursor.y+1]);
                lines.erase(lines.begin() + cursor.y + 1);
            } else
                line.erase(cursor.x);
            break;
        }
        case MOD_CTRL|'v':
        case MOD_CTRL|'y':
            insertText(OL_ReadClipboard());
            if (textChanged)
                *textChanged = true;
            break;
        case MOD_ALT|NSRightArrowFunctionKey:
        case MOD_ALT|'f': forward_when(cursor, lines, +1, isalnum); break;
        case MOD_ALT|NSLeftArrowFunctionKey:
        case MOD_ALT|'b': forward_when(cursor, lines, -1, isalnum); break;

        case MOD_ALT|NSBackspaceCharacter: {
            const int2 scursor = cursor;
            forward_when(cursor, lines, -1, isalnum);
            delete_region(cursor, lines, scursor);
            if (textChanged)
                *textChanged = true;
            break;
        }
        case MOD_ALT|'d': {
            const int2 scursor = cursor;
            forward_when(cursor, lines, 1, isalnum);
            delete_region(cursor, lines, scursor);
            if (textChanged)
                *textChanged = true;
            break;
        }
        case MOD_ALT|'m':
            cursor.x = 0;
            forward_when(cursor, lines, +1, isspace);
            break;
        case NSLeftArrowFunctionKey:  forward_char(cursor, lines, -1); break;
        case NSRightArrowFunctionKey: forward_char(cursor, lines, +1); break;
        case NSUpArrowFunctionKey:    cursor.y = max(0, cursor.y-1); break;
        case NSDownArrowFunctionKey:  cursor.y = min((int)lines.size()-1, cursor.y+1); break;
        case NSHomeFunctionKey:       cursor.x = 0; break;
        case NSEndFunctionKey:        cursor.x = lines[cursor.y].size(); break;
        case MOD_CTRL|'d':
        case NSDeleteFunctionKey:
            if (!forward_char(cursor, lines, +1))
                break;
            // fallthrough!
        case NSBackspaceCharacter:
            delete_char(cursor, lines);
            if (textChanged)
                *textChanged = true;
            break;
        case '\r':
        {
            if (fixedHeight && lines.size() >= sizeChars.y)
                return false;
            string s = utf8_substr(lines[cursor.y], cursor.x);
            lines[cursor.y] = utf8_erase(lines[cursor.y], cursor.x);
            lines.insert(lines.begin() + cursor.y + 1, s);
            if (textChanged)
                *textChanged = true;
            cursor.y++;
            cursor.x = 0;
            break;
        }
        default:
        {
            string str = event->toUTF8();
            if (str.empty())
                return false;
            ASSERT(cursor.x == utf8_advance(lines[cursor.y], cursor.x));
            lines[cursor.y].insert(cursor.x, str);
            cursor.x += str.size();
            if (textChanged)
                *textChanged = true;
            break;
        }
        }

        if (lines.size() == 0)
            lines.push_back("");

        if (cursor.y >= lines.size()) {
            cursor.x = lines.back().size();
            cursor.y = lines.size()-1;
        }
        cursor.y = clamp(cursor.y, 0, (int)lines.size()-1);
        cursor.x = clamp(cursor.x, 0, lines[cursor.y].size());
        scrollForInput();

        return true;
    }

    return false;
}

void TextInputBase::popText(int chars)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    while (chars && lines.size())
    {
        string &str = lines.back();
        int remove = min(chars, (int) str.size());
        str.resize(str.size() - remove);
        chars -= remove;
        if (str.size() == 0)
            lines.pop_back();
    }
}

void TextInputBase::pushText(string txt, int linesback)
{
    if (wrapText)
        txt = str_word_wrap(txt, sizeChars.x);
    vector<string> nlines = str_split('\n', txt);

    std::lock_guard<std::recursive_mutex> l(mutex);

    const int pt = lines.size() - linesback;
    lines.insert(lines.end() - min(linesback, (int)lines.size()), nlines.begin(), nlines.end());

    if (cursor.y >= pt)
        cursor.y += nlines.size();
    // cursor.x = lines[cursor.y].size();
    scrollForInput();
}

void TextInputBase::insertText(const char *txt)
{
    if (!txt)
        return;
    vector<string> nlines = str_split('\n', txt);
    if (nlines.size() == 0)
        return;

    std::lock_guard<std::recursive_mutex> l(mutex);
    lines[cursor.y].insert(cursor.x, nlines[0]);
    cursor.x += nlines[0].size();

    lines.insert(lines.begin() + cursor.y + 1, nlines.begin() + 1, nlines.end());
    cursor.y += nlines.size()-1;

    scrollForInput();
}

float2 TextInputBase::getCharSize() const
{
    float2 size = FontStats::get(kMonoFont, textSize).charMaxSize;
    return size;
}


void TextInputBase::render(const ShaderState &s_)
{
    std::lock_guard<std::recursive_mutex> l(mutex);

    ShaderState s = s_;

    startChars.x = 0;

    const int2 start     = startChars;
    const int  drawLines = min((int)lines.size() - start.y, sizeChars.y);

    if (!fixedWidth)
    {
        float longestPointWidth = 0.f;
        int   longestChars      = 0;
        for (int i=start.y; i<(start.y + drawLines); i++)
        {
            const GLText* tx = GLText::get(kMonoFont, textSize, lines[i]);
            if (tx->getSize().x > longestPointWidth) {
                longestPointWidth = tx->getSize().x;
                longestChars     = lines[i].size();
            }
        }

        sizeChars.x = max(sizeChars.x, (int)longestChars + 1);
        if (longestChars) {
            const float mwidth = max(longestPointWidth, sizeChars.x * (longestPointWidth / longestChars));
            size.x = max(size.x, kButtonPad.x + mwidth);
        }
    }
    const float charHeight = getCharSize().y;
    size.y = charHeight * sizeChars.y + kPadDist;

    const float2 sz = 0.5f * size;

    {
        DMesh::Handle h(theDMesh());
        h.mp.tri.color32(active ? activeBGColor : defaultBGColor, alpha);
        if (h.mp.tri.cur().color&ALPHA_OPAQUE)
            h.mp.tri.PushRect(position, sz);

        h.mp.line.color32(active ? activeLineColor : defaultLineColor, alpha);
        if (h.mp.line.cur().color&ALPHA_OPAQUE)
            h.mp.line.PushRect(position, sz);

        if (lines.size() > sizeChars.y && defaultLineColor)
        {
            scrollbar.size = float2(kScrollbarWidth, size.y - kButtonPad.y);
            scrollbar.position = position + justX(size/2.f) - justX(scrollbar.size.x/2.f + kButtonPad.x);
            scrollbar.alpha = alpha;
            scrollbar.first = startChars.y;
            scrollbar.sfirst = scrollbar.first;
            scrollbar.visible = sizeChars.y;
            scrollbar.total = lines.size();
            scrollbar.defaultBGColor = defaultBGColor;
            scrollbar.hoveredFGColor = activeLineColor;
            scrollbar.defaultFGColor = defaultLineColor;
            scrollbar.render(h.mp);
        }

        h.Draw(s);
    }

    s.translate(position);

    s.translate(float2(-sz.x + kPadDist, sz.y - charHeight - kPadDist));
    s.color32(textColor, alpha);
    const int2 curs = cursor;

    for (int i=start.y; i<(start.y + drawLines); i++)
    {
//            const string txt = lines[i].substr(start,
//                                               min(lines[i].size(), start.x + sizeChars.x));
        const GLText* tx = GLText::get(kMonoFont, textSize, lines[i]);

        if (lines[i].size())
        {
            tx->render(&s);
        }

        // draw cursor
        if (active && !locked && curs.y == i) {
            ShaderState  s1    = s;
            const float  spos = tx->getCharStart(curs.x);
            const float2 size  = tx->getCharSize(curs.x);
            s1.translate(float2(spos, 0));
            s1.color(textColor, alpha);
            s1.translateZ(1.f);
            ShaderUColor::instance().DrawRectCorners(s1, float2(0), float2(size.x, charHeight));
            if (curs.x < lines[i].size()) {
                GLText::Put(s1, float2(0.f), GLText::LEFT, kMonoFont,
                            ALPHA_OPAQUE|GetContrastWhiteBlack(textColor),
                            textSize, utf8_substr(lines[i], curs.x, 1));
            }
        }

        s.translate(float2(0.f, -charHeight));
    }
}

TextInputCommandLine::TextInputCommandLine()
{
    // 120 on mac, 100 on windows??
    sizeChars = int2(100, 10);
    fixedHeight = false;
    fixedWidth = true;
    wrapText = true;

    registerCommand(cmd_help, comp_help, this, "help", "[command]: list help for specified command, or all commands if unspecified");
    registerCommand(cmd_find, comp_help, this, "find", "[string]: list commands matching search");
    setLineText("");
}

void TextInputCommandLine::saveHistory(const char *fname)
{
    string str = str_join("\n", commandHistory);
    int status = ZF_SaveFile(fname, str.c_str(), str.size());
    Reportf("Wrote %d lines of console history to '%s': %s",
            (int) commandHistory.size(), fname, status ? "OK" : "FAILED");
}

void TextInputCommandLine::loadHistory(const char *fname)
{
    string data = ZF_LoadFile(fname);
    if (data.size()) {
#if WIN32
        data = str_replace(data, "\r", "");
#endif
        data = str_chomp(data);
        commandHistory = str_split('\n', data);
    }
    historyIndex = commandHistory.size();
}

vector<string> TextInputCommandLine::comp_help(void* data, const char* name, const char* args)
{
    vector<string> options;
    TextInputCommandLine *self = (TextInputCommandLine*) data;
    for (std::map<string, Command>::iterator it=self->commands.begin(), end=self->commands.end(); it != end; ++it)
    {
        options.push_back(it->first);
    }
    return options;
}

string TextInputCommandLine::cmd_help(void* data, const char* name, const char* args)
{
    TextInputCommandLine* self = (TextInputCommandLine*) data;

    const string arg = str_strip(args);
    vector<string> helps = self->completeCommand(arg);
    if (helps.size() == 0)
    {
        for (std::map<string, Command>::iterator it=self->commands.begin(), end=self->commands.end(); it != end; ++it)
            helps.push_back(it->first);
    }
    string ss;
    foreach (const string &cmd, helps)
        ss += "^2" + cmd + "^7 " + self->commands[cmd].description + "\n";
    ss.pop_back();
    return ss;
}

string TextInputCommandLine::cmd_find(void* data, const char* name, const char* args)
{
    TextInputCommandLine* self = (TextInputCommandLine*) data;

    const string arg = str_strip(args);
    string ss;
    int count = 0;
    foreach (const auto& x, self->commands) {
        if (str_contains(x.first, arg) || str_contains(x.second.description, arg))
        {
            ss += "^2" + x.first + "^7 " + x.second.description + "\n";
            count++;
        }
    }
    if (count) {
        ss.pop_back();
    } else {
        ss = str_format("No commands matching '%s'", arg.c_str());
    }
    return ss;
}

void TextInputCommandLine::pushPrompt()
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    lines.push_back(prompt);

    cursor.y = (int)lines.size() - 1;
    cursor.x = lines[cursor.y].size();
    scrollForInput();
}

vector<string> TextInputCommandLine::completeCommand(string cmd) const
{
    vector<string> possible;
    foreach (const auto& x, commands) {
        if (x.first.size() >= cmd.size() && x.first.substr(0, cmd.size()) == cmd)
            possible.push_back(x.first);
    }
    return possible;
}

bool TextInputCommandLine::doCommand(const string& line)
{
    pushHistory(line);
    const vector<string> expressions = str_split_quoted(';', line);
    foreach (const string &expr, expressions)
    {
        vector<string> args = str_split_quoted(' ', str_strip(expr));
        if (!args.size())
            return false;
        string cmd = str_tolower(args[0]);

        Command *c = NULL;
        if (map_contains(commands, cmd)) {
            c = &commands[cmd];
        } else {
            const vector<string> possible = completeCommand(cmd);
            if (possible.size() == 1) {
                c = &commands[possible[0]];
            } else {
                pushText(str_format("No such command '%s'%s", cmd.c_str(),
                                    possible.size() ? str_format(", did you mean %s?",
                                                                 str_join(", ", possible).c_str()).c_str() : ""), 0);
                pushPrompt();
                return false;
            }
        }

        DPRINT(CONSOLE, ("'%s'", expr.c_str()));

        const string argstr = str_join(" ", args.begin() + 1, args.end());
        const string ot = c->func(c->data, cmd.c_str(), argstr.c_str());

        DPRINT(CONSOLE, ("-> '%s'", ot.c_str()));

        const vector<string> nlines = str_split('\n', str_word_wrap(ot, sizeChars.x));
        std::lock_guard<std::recursive_mutex> l(mutex);
        lines.insert(lines.end(), nlines.begin(), nlines.end());
    }
    pushPrompt();
    return true;
}

bool TextInputCommandLine::pushCommand(const string& line)
{
    setLineText(line);
    return doCommand(line);
}

const TextInputCommandLine::Command *TextInputCommandLine::getCommand(const string &abbrev) const
{
    const string cmd = str_tolower(abbrev);
    if (map_contains(commands, cmd)) {
        return map_addr(commands, cmd);
    } else {
        vector<string> possible;
        foreach (const auto& x, commands) {
            if (x.first.size() > cmd.size() && x.first.substr(0, cmd.size()) == cmd)
                possible.push_back(x.first);
        }
        if (possible.size() == 1) {
            return map_addr(commands, possible[0]);
        }
        return NULL;
    }
}

bool TextInputCommandLine::HandleEvent(const Event* event, bool *textChanged)
{
    if (textChanged)
        *textChanged = false;

    if (active && event->type == Event::KEY_DOWN)
    {
        std::unique_lock<std::recursive_mutex> l(mutex);
        switch (KeyState::instance().keyMods() | event->key)
        {
        case MOD_CTRL|'l': {
            lines.erase(lines.begin(), lines.end()-1);
            scrollForInput();
            break;
        }
        case NSUpArrowFunctionKey:
        case NSDownArrowFunctionKey:
        case MOD_CTRL|'p':
        case MOD_CTRL|'n':
            if (commandHistory.size())
            {
                lastSearch = "";
                const int delta = (event->key == NSUpArrowFunctionKey || event->key == 'p')  ? -1 : 1;
                historyIndex = modulo((historyIndex + delta), (commandHistory.size() + 1));
                setLineText(historyIndex >= commandHistory.size() ? currentCommand : commandHistory[historyIndex]);
            }
            return true;
        case '\r':
        {
            lastSearch = "";
            string cmd = getLineText();
            l.unlock();
            doCommand(cmd);
            return true;
        }
        case MOD_CTRL|'r':
        case MOD_CTRL|'s':
        case MOD_ALT|'p':
        case MOD_ALT|'n':
        {
            int end = 0;
            int delta = 0;
            if (event->key == 'p' || event->key == 'r') {
                end = 0;
                delta = -1;
            } else if (event->key == 'n' ||event->key == 's') {
                end = commandHistory.size();
                delta = 1;
            }

            if (lastSearch.empty())
                lastSearch = getLineText();
            if (lastSearch.empty())
                return true;

            for (int i=historyIndex; i != end; i += delta)
            {
                if (i != historyIndex &&
                    commandHistory[i].size() >= lastSearch.size() &&
                    commandHistory[i].substr(0, lastSearch.size()) == lastSearch)
                {
                    historyIndex = i;
                    setLineText(commandHistory[i]);
                    break;
                }
            }
            return true;
        }
        case '\t':
        {
            lastSearch = "";
            string line = getLineText();
            string suffix;
            const int curs = cursor.x - prompt.size();
            if (curs < line.size()) {
                suffix = line.substr(curs);
                line = line.substr(0, curs);
            }
            string prefix;
            int cmdStart = line.rfind(';');
            if (cmdStart > 0) {
                while (cmdStart < line.size() && str_contains(" ;", line[cmdStart]))
                    cmdStart++;
                prefix = line.substr(0, cmdStart);
                line = line.substr(cmdStart);
            }
            int start = line.rfind(' ');
            
            vector<string> options;
            if (start > 0)
            {
                //  complete arguments
                vector<string> args = str_split(' ', str_strip(line));
                if (args.size() >= 1)
                {
                    const Command *cmd = getCommand(args[0]);
                    if (cmd && cmd->comp)
                    {
                        const string argstr = str_join(" ", args.begin() + 1, args.end());
                        const string largs = str_tolower(argstr);
                        options = cmd->comp(cmd->data, cmd->name.c_str(), argstr.c_str());
                        for (int i=0; i<options.size(); ) {
                            vec_pop_increment(
                                options, i, str_tolower(options[i].substr(0, largs.size())) != largs);
                        }
                        for_ (op, options) {
                            if (op.find(' ') != string::npos)
                                op = '"' + op + '"';
                        }
                        line = cmd->name + " " + argstr;
                        start = argstr.size();
                    }
                }
            }
            else
            {
                // complete commands
                const string lline = str_tolower(line);
                for (std::map<string, Command>::iterator it=commands.begin(), end=commands.end(); it != end; ++it)
                {
                    if (str_tolower(it->first.substr(0, lline.size())) == lline)
                        options.push_back(it->second.name);
                }
                start = line.size();
            }

            if (options.empty()) {
                pushText("No completions");
                return true;
            }
            
            bool done = false;
            const string oline = line;
            line.resize(line.size() - start); // update case
            line += options[0].substr(0, start);
            for (int i=start; ; i++) {
                const char c = options[0][i];
                for (uint j=0; j<options.size(); j++) {
                    if (i == options[j].size() || options[j][i] != c) {
                        done = true;
                        break;
                    }
                }
                if (done)
                    break;
                else
                    line += c;
            }

            if (options.size() > 1 && oline.size() == line.size())
            {
                pushText(prompt + oline);
                pushText(str_join(" ", options));
            }
            setLineText(prefix + line + suffix, prefix.size() + line.size());
            if (textChanged)
                *textChanged = true;
            return true;
        }
        default:
            break;
        }
    }

    bool changed = false;
    if (!TextInputBase::HandleEvent(event, &changed))
        return false;

    std::lock_guard<std::recursive_mutex> l(mutex);
    if (changed)
    {
        currentCommand = getLineText();
        historyIndex = commandHistory.size();

        if (textChanged)
            *textChanged = true;
    }

    if (cursor.y >= lines.size()) {
        cursor.x = lines.back().size();
    } else {
        cursor.x = clamp(cursor.x, prompt.size(), lines.back().size());
    }

    cursor.y = (int)lines.size()-1;

    string& line = lines[cursor.y];
    if (line.size() < prompt.size()) {
        setLineText("");
    } else if (!str_startswith(line, prompt)) {
        for (int i=0; i<prompt.size(); i++) {
            if (line[i] != prompt[i])
                line.insert(i, 1, prompt[i]);
        }
    }

    lastSearch = "";
    return true;
}

void ContextMenu::setLine(int line, const string &txt)
{
    if (line >= lines.size()) {
        lines.resize(line + 1);
        enabled.resize(line + 1);
    }
    lines[line] = txt;
    enabled[line] = str_len(txt) != 0;
}

int ContextMenu::getHoverSelection(float2 p) const
{
    if (lines.empty() || !intersectPointRectangle(p, getCenterPos(), size/2.f))
        return -1;

    const int    vis_lines  = min((int)lines.size(), scrollbar.visible);
    const float2 relp       = p - position;
    const float  lineHeight = size.y / vis_lines;
    const int    sel        = (int) floor(-relp.y / lineHeight);

    if (sel >= lines.size() || !enabled[sel])
        return -1;
    return sel + scrollbar.first;
}

void ContextMenu::openMenu(float2 pos)
{
    pos.y = max(pos.y, size.y + kButtonPad.y);
    if (pos.x + size.x + kButtonPad.x > globals.windowSizePoints.x)
        pos.x -= size.x;
    position = pos;
    openTime = globals.updateStartTime;
    active = true;
    scrollbar.parent = this;
}

bool ContextMenu::HandleEvent(const Event* event, int* select)
{
    if (lines.size() > scrollbar.visible && scrollbar.HandleEvent(event))
        return true;
    
    if (!event->isMouse())
        return false;

    if (!active && event->type == Event::MOUSE_DOWN && event->key == 1)
    {
        openMenu(event->pos);
        return true;
    }

    hovered = active ? getHoverSelection(event->pos) : -1;

    if (!active)
        return false;

    if (hovered == -1)
    {
        if (event->type == Event::MOUSE_DOWN)
        {
            active = false;
            return true;
        }
        return false;
    }

    if (event->type == Event::MOUSE_DOWN)
    {
        return true;
    }
    else if (event->type == Event::MOUSE_UP && (globals.updateStartTime - openTime) > 0.25)
    {
        if (select)
            *select = hovered;
        active = (hovered == -1);   // disable on successful use
        return hovered >= 0;
    }

    return false;
}

void ContextMenu::render(const ShaderState &ss)
{
    if (lines.empty() || !active || alpha < epsilon)
        return;

    scrollbar.setup(this, i2(1, 15), lines.size());
    scrollbar.position = (position + flipY(size/2.f)) + f2x(size/2.f - scrollbar.size.x/2.f);
    const int first = scrollbar.first;
    const int last = scrollbar.last();
    const int vis_lines = last - first;
    
    // always calculate size
    float2 sz;
    for (int i=first; i<last; i++)
    {
        const GLText* tx = GLText::get(kDefaultFont, textSize, lines[i]);
        sz.x = max(sz.x, tx->getSize().x);
        sz.y = vis_lines * tx->getSize().y;
    }
    size = sz + 2.f * kButtonPad + f2x(scrollbar.size.x);

    DMesh::Handle h(theDMesh());
    h.mp.translateZ(2.f);
    PushRect(&h.mp.tri, &h.mp.line, getCenterPos(), size/2.f, defaultBGColor, defaultLineColor, alpha);
    h.mp.translateZ(0.1f);

    float2 pos = position + flipY(kButtonPad);
    const float textHeight = sz.y / vis_lines;
    if (hovered >= 0)
    {
        const float2 hpos = position - f2y((hovered - first) * textHeight + 2.f * kButtonPad);
        h.mp.tri.color32(hoveredBGColor, alpha);
        h.mp.tri.PushRectCorners(hpos, hpos + float2(size.x - kButtonPad.x, -textHeight));
    }

    h.Draw(ss);
    h.clear();
    h.mp.line.translateZ(2.1f);

    for (int i=first; i<last; i++)
    {
        pos.y -= textHeight;
        if (lines[i].empty()) {
            h.mp.line.color32(inactiveTextColor, alpha);
            h.mp.line.PushLine(pos + justY(textHeight/2.f), pos + float2(sz.x, textHeight/2.f));
        } else {
            const uint color = MultAlphaAXXX(enabled[i] ? textColor : inactiveTextColor, alpha);
            GLText::Put(ss, pos, GLText::LEFT, color, textSize, lines[i]);
        }
    }

    if (lines.size() > scrollbar.visible)
        scrollbar.render(h.mp);
    h.Draw(ss);
}


bool OptionButtons::HandleEvent(const Event* event, int* butActivate, int* butPress)
{
    Event ev = *event;
    ev.pos -= position;

    bool handled = false;

    for (int i=0; i<buttons.size(); i++)
    {
        bool isActivate = false;
        bool isPress    = false;
        if (buttons[i].HandleEvent(&ev, &isActivate, &isPress))
        {
            if (isActivate && butActivate)
                *butActivate = isActivate;
            if (isPress && butPress)
                *butPress = isPress;
            if (isPress)
                selected = i;
            handled = true;
            break;
        }
    }
    for (int j=0; j<buttons.size(); j++)
        buttons[j].pressed = (selected == j);
    return handled;
}

bool BContextBase::HandleEventMenu(const Event* event, bool* changed)
{
    bool isPress = false;
    if (menu.active)
    {
        int selected = -1;
        if (menu.HandleEvent(event, &selected))
        {
            if (selected >= 0)
            {
                const int last = selection;
                setSelection(selected);
                if (changed && last != selection)
                    *changed = true;
            }
            return true;
        }
    }
    else if (ButtonHandleEvent(*this, event, NULL, &isPress))
    {
        if (isPress)
        {
            menu.openMenu(position);
        }
        return true;
    }
    return false;
}


void BContextBase::setSelection(int index)
{
    selection = clamp(index, 0, menu.lines.size()-1);
    if (showSelection) {
        // FIXME bug, text is read in render thread!
        if (title.empty())
            text = menu.lines[selection];
        else
            text = lang_colon(title, menu.lines[selection]);
    }
}


void BContextBase::renderContents1(const ShaderState &ss)
{
    menu.alpha = alpha;
    menu.render(ss);
}

void OptionButtons::render(ShaderState *s_, const View& view)
{
    ShaderState ss = *s_;
    ss.translate(position);

    size = float2(0.f);
    float2 maxsize;
    foreach (Button& but, buttons) {
        maxsize = max(maxsize, but.size);
        size    = max(size, abs(but.position) + 0.5f * but.size);
    }

    foreach (Button& but, buttons) {
        but.size = maxsize; // buttons are all the same size
        but.render(ss);
    }

    foreach (Button& but, buttons) {
        but.renderTooltip(ss, view, but.textColor);
    }

#if 0
    // for debugging - draw the bounding rectangle
    if (buttons.size()) {
        ss.color32(buttons[0].defaultLineColor);
        ShaderUColor::instance().DrawLineRect(ss, size);
    }
#endif
}

bool OptionSlider::HandleEvent(const Event* event, bool *valueChanged)
{
    if (!event->isMouse() || !active)
        return false;
    const float2 sz = 0.5f*size;

    hovered = pressed || intersectPointRectangle(event->pos, position, sz);
    pressed = (hovered && event->type == Event::MOUSE_DOWN) ||
              (!isDiscrete() && pressed && event->type == Event::MOUSE_DRAGGED);

    const bool handled = pressed || (hovered && event->isMouse());

    const int lasthval = hoveredValue;
    hoveredValue = hovered ? (isBinary() ? !value : floatToValue(((event->pos.x - position.x) / size.x) + 0.5f)) : -1;
    if (isDiscrete() && lasthval != hoveredValue)
        PLAY_BUTTON_HOVER();

    if (pressed) {
        if (valueChanged && (value != hoveredValue)) {
            if (isDiscrete())
                PLAY_BUTTON_PRESS();
            *valueChanged = true;
        }
        value = hoveredValue;
    }

    return handled;
}

void OptionSlider::render(const ShaderState &s_)
{
    const float2 sz = 0.5f * size;
    const float  w  = max(sz.x / values, 5.f);
    const uint   fg = getFGColor();
    const uint   bg = getBGColor();

    if (isDiscrete())
    {
        DMesh::Handle h(theDMesh());
        if (isBinary())
        {
            const uint bgc = bg;
            const uint fgc = hovered ? hoveredLineColor : defaultLineColor;
            PushButton(&h.mp.tri, &h.mp.line, position, sz, bgc, fgc, alpha);
            if (value) {
                h.mp.tri.translateZ(0.1f);
                PushButton(&h.mp.tri, &h.mp.line, position, max(float2(2.f), sz - kButtonPad), fgc, fgc, alpha);
                h.mp.tri.translateZ(-0.1f);
            }
        }
        else
        {
            float2 pos = position - justX(sz.x - w);
            const float2 bs = float2(w, sz.y) - kButtonPad;
            for (int i=0; i<values; i++)
            {
                const uint bgc = (i == value) ? bg : 0x0;
                const uint fgc = (i == hoveredValue) ? hoveredLineColor : defaultLineColor;
                PushButton(&h.mp.tri, &h.mp.line, pos, bs, bgc, fgc, alpha);
                if (i == value)
                {
                    h.mp.tri.translateZ(0.1f);
                    PushButton(&h.mp.tri, &h.mp.line, pos, max(float2(2.f), bs - kButtonPad), fgc, fgc, alpha);
                    h.mp.tri.translateZ(-0.1f);
                }
                pos.x += 2.f * w;
            }
        }
        h.Draw(s_);
    }
    else
    {
        ShaderState ss = s_;
        ss.color32(fg, alpha);
        ss.translateZ(0.1f);
        ShaderUColor::instance().DrawLine(ss, position - justX(sz), position + justX(sz));
        const float of = (size.x - 2.f * w) * (getValueFloat() - 0.5f);
        ss.translateZ(1.f);
        DrawButton(&ss, position + float2(of, 0.f), float2(w, sz.y), bg, fg, alpha);
    }
}

OptionEditor::OptionEditor(float *f, const char* lbl, float mn, float mx, const StringVec tt)
{
    init(FLOAT, (void*) f, lbl, tt, mn, mx-mn, 200.f);
}

OptionEditor::OptionEditor(float *f, const char* lbl, float mn, float mx, float inc, const StringVec tt)
{
    init(FLOAT, (void*) f, lbl, tt, mn, mx-mn, floor_int((mx - mn) / inc) + 1);
}

OptionEditor::OptionEditor(int *u, const char* lbl, int states, const StringVec tt)
{
    init(INT, (void*) u, lbl, tt, 0.f, (float) states-1, states);
}

OptionEditor::OptionEditor(int *u, const char* lbl, int low, int high, int increment, const StringVec tt)
{
    init(INT, (void*) u, lbl, tt, low, high - low, (high - low + increment-1) / increment + 1);
}

float OptionEditor::getValueFloat() const
{
    return ((type == FLOAT) ? *(float*) value : ((float) *(int*) value));
}

void OptionEditor::setValueFloat(float v)
{
    if (type == FLOAT)
        *(float*) value = v;
    else
        *(int*) value = round_int(v);
    txt = lang_colon(label, getTxt());
}

void OptionEditor::updateSlider()
{
    slider.setValueFloat((getValueFloat() - start) / mult);
    txt = lang_colon(label, getTxt());
}


void OptionEditor::init(Type t, void *v, const char* lbl, const StringVec &tt, float st, float mu, int states)
{
    type = t;
    value = v;
    start = st;
    mult = mu;
    slider.values = states;
    label = lbl;
    tooltip = tt;
    updateSlider();
}

string OptionEditor::getTxt() const
{
    if (format == SECONDS) {
        const float v = getValueFloat();
        return (v <= 0.f) ? _("Off") : str_time_format_long(v);
    } else if (format == COUNT) {
        return (type == INT) ? str_format("%d", getValueInt()) : str_format("%.f", getValueFloat());
    } else if (format == PERCENT) {
        const int val = floor_int(100.f * getValueFloat());
        return (val < 1.f) ? _("Off") : str_format("%d%%", val);
    } else if (slider.values == tooltip.size()) {
        return tooltip[clamp(getValueInt(), 0, tooltip.size()-1)];
    } else if (slider.values <= 4) {
        const int val = getValueInt();
        if (val == 0) {
            return _("Off");
        } else if (slider.values == 3) {
            return val == 1 ? _("Low") :  _("Full");
        } else if (slider.values == 4) {
            return (val == 1 ? _("Low") :
                    val == 2 ? _("Medium") : _("Full"));
        } else {
            return _("On");
        }
    } else if (start != 0.f && (type == INT)) {
        return str_format("%d", getValueInt());
    } else {
        const int val = floor_int(100.f * getValueFloat());
        return (val < 1.f) ? _("Off") : str_format("%d%%", val);
    }
}

float2 OptionEditor::render(const ShaderState &ss, float alpha)
{
    slider.alpha = alpha;
    slider.render(ss);
    float dir = right ? -1.f : 1.f;
    return GLText::Put(ss, slider.position + justX(dir * (0.5f * slider.size.x + 2.f * kButtonPad.x)),
                       right ? GLText::MID_RIGHT : GLText::MID_LEFT,
                       SetAlphaAXXX(slider.active ? kGUIText : kGUIInactive, alpha), 14, txt);
}

bool OptionEditor::HandleEvent(const Event* event, bool* valueChanged)
{
    bool changed = false;
    bool handled = slider.HandleEvent(event, &changed);
    if (handled && changed) {
        setValueFloat(slider.getValueFloat() * mult + start);
    }
    if (valueChanged)
        *valueChanged = changed;
    return handled;
}


void TabWindow::TabButton::renderButton(DMesh &mesh, bool)
{
    static const float o = 0.05f;
    const float2 r = size / 2.f;
    //   1      2
    // 0        3
    const float2 v[] = {
        position + float2(-r),
        position + float2(lerp(-r.x, r.x, o), r.y),
        position + float2(r),
        position + float2(r.x, -r.y),
    };
    mesh.tri.PushPoly(v, arraySize(v));
    mesh.line.PushStrip(v, arraySize(v));
}

float TabWindow::getTabHeight() const
{
    return kButtonPad.y + 1.5f * GLText::getScaledSize(textSize);
}

void TabWindow::render(const ShaderState &ss, const View &view)
{
    alpha = view.alpha;
    if (alpha > epsilon)
    {
        DMesh::Handle h(theDMesh());

        const float2 opos = position - float2(0.f, 0.5f * getTabHeight());
        const float2 osz = 0.5f * (size - float2(0.f, getTabHeight()));
        h.mp.translateZ(-1.5f);
        h.mp.tri.color32(defaultBGColor, alpha);
        h.mp.tri.PushRect(opos, osz);
        h.mp.line.translateZ(0.1f);
        h.mp.line.color32(defaultLineColor, alpha);

        const float2 tsize = float2(size.x / buttons.size(), getTabHeight());
        float2 pos = opos + flipX(osz);
        int i=0;
        foreach (TabButton& but, buttons)
        {
            but.size = tsize;
            but.position = pos + 0.5f * tsize;
            pos.x += tsize.x;

            h.mp.line.color32(!but.active ? inactiveLineColor :
                              but.hovered ? hoveredLineColor : defaultLineColor, alpha);
            h.mp.tri.color32((selected == i) ? defaultBGColor : inactiveBGColor, alpha);
            but.renderButton(h.mp);

            i++;
        }

        // 4 5 0 1
        // 3     2
        const float2 vl[] = {
            buttons[selected].position + flipY(buttons[selected].size / 2.f),
            opos + osz,
            opos + flipY(osz),
            opos - osz,
            opos + flipX(osz),
            buttons[selected].position - buttons[selected].size / 2.f
        };
        h.mp.line.color32(defaultLineColor, alpha);
        h.mp.line.PushStrip(vl, arraySize(vl));

        h.Draw(ss);

        foreach (const TabButton& but, buttons)
        {
            GLText::Put(ss, but.position, GLText::MID_CENTERED, MultAlphaAXXX(textColor, alpha),
                        textSize, but.text);
        }
    }

    View tview = view;
    tview.center = getContentsCenter();
    tview.size = getContentsSize();
    buttons[selected].interface->renderTab(tview);
}

int TabWindow::addTab(string txt, int ident, ITabInterface *inf, const KeyBinding *key)
{
    const int idx = (int)buttons.size();
    buttons.push_back(TabButton());
    TabButton &bu = buttons.back();
    bu.interface = inf;
    bu.text = txt;
    bu.ident = ident;
    bu.key = key;
    return idx;
}

bool TabWindow::swapToTab(int next)
{
    if (next == selected)
        return false;
    buttons[selected].interface->onSwapOut();
    buttons[next].interface->onSwapIn();
    selected = next;            // set selected AFTER swapping out
    PLAY_BUTTON_HOVER();
    return true;
}

bool TabWindow::HandleEvent(const Event* event, bool *istoggle)
{
    if (buttons[selected].interface->HandleEvent(event))
        return true;

    bool handled = false;
    int i=0;
    foreach (TabButton& but, buttons)
    {
        bool isActivate = false;
        if (but.HandleEvent(event, &isActivate)) {
            if (isActivate)
                swapToTab(i);
            handled = true;
        }
#if HAS_KEYS
        if ((istoggle || i != selected) && but.key && but.key->isDownEvent(event))
        {
            if (!swapToTab(i) && istoggle)
                *istoggle = true;
            return true;
        }
#endif
        i++;
    }

    if (handled)
        return true;

    const int dkey = KeyState::instance().getDownKey(event);
    const bool isLeft = dkey == (MOD_SHFT | '\t') || dkey == GamepadLeftShoulder;
    const bool isRight = dkey == '\t' || dkey == GamepadRightShoulder;

    if (isLeft || isRight)
    {
        swapToTab(modulo(selected + (isLeft ? -1 : 1), buttons.size()));
        return true;
    }

    return false;
}

MessageBoxWidget::MessageBoxWidget()
{
    title = _("Message");
    okbutton.setReturnKeys();
}

MessageBoxBase::MessageBoxBase()
{
    okbutton.setText(_("OK"));
}

void MessageBoxBase::updateFade()
{
    static const float kMessageBoxFadeTime = 0.15f;
    static const float kMessageBoxTextFadeTime = 0.25f;

    alpha = lerp(alpha, active ? 1.f : 0.f, globals.frameTime / kMessageBoxFadeTime);
    alpha2 = active ? lerp(alpha2, 1.f, globals.frameTime / kMessageBoxTextFadeTime) : alpha;
}

static const float2 kBoxPad = 3.f * kButtonPad;

void MessageBoxBase::render(const ShaderState &s1, const View& view)
{
    const GLText *msg = GLText::get(messageFont, textSize, message);
        
    size = max(0.25f * view.sizePoints,
               msg->getSize() + 6.f * kBoxPad +
               justY(GLText::getScaledSize(titleSize) + okbutton.size.y));

    position = 0.5f * view.sizePoints;

    if (alpha < epsilon || !active)
        return;

    ShaderState ss = s1;
    ss.translateZ(2.1f);

    {
        View fview = view;
        fview.alpha = 0.5f * alpha;
        fadeFullScreen(fview, COLOR_BLACK);
    }


    const float2 boxRad = size / 2.f;

    DrawFilledRect(ss, position, boxRad, kGUIBg, kGUIFg, alpha);

    float2 pos = position + justY(boxRad) - justY(kBoxPad);

    pos.y -= GLText::Put(ss, pos, GLText::DOWN_CENTERED, MultAlphaAXXX(kGUIText, alpha),
                         titleSize, title).y;

    pos.x = position.x - boxRad.x + kBoxPad.x;

    DrawFilledRect(ss, position, msg->getSize() / 2.f + kBoxPad, kGUIBg, kGUIFg, alpha2);

    {
        ShaderState s2 = ss;
        s2.color(kGUIText, alpha);
        msg->render(&s2, position - msg->getSize() / 2.f);
    }
}

static void renderOneButton(const ShaderState &ss, Button &bu)
{
    {
        DMesh::Handle h(theDMesh());
        h.mp.translateZ(1.f);
        bu.renderButton(h.mp, false);
        h.Draw(ss);
    }
    bu.renderContents(ss);
}

void MessageBoxWidget::render(const ShaderState &ss, const View& view)
{
    MessageBoxBase::render(ss, view);

    okbutton.position = position - justY(size / 2.f) + justY(kBoxPad + 0.5f * okbutton.size.y);
    okbutton.alpha = alpha2;
    if (active)
        renderOneButton(ss, okbutton);
}

bool MessageBoxWidget::HandleEvent(const Event* event)
{
    if (!active)
        return false;
    bool isActivate = false;
    if (okbutton.HandleEvent(event, &isActivate) && isActivate)
    {
        active = false;
    }
    // always handle when active
    return true;
}

ConfirmWidget::ConfirmWidget()
{
    title = _("Confirm");
    active = false;
    okbutton.setYesKeys();
    cancelbutton.text = _("Cancel");
    cancelbutton.setNoKeys();
}

void ConfirmWidget::render(const ShaderState &ss, const View& view)
{
    MessageBoxBase::render(ss, view);

    const float2 bcr = position + justY(kBoxPad + 0.5f * okbutton.size - size / 2.f );
    okbutton.position = bcr + justX(okbutton.size + kBoxPad);
    okbutton.alpha = alpha2;

    cancelbutton.position = bcr - justX(cancelbutton.size + kBoxPad);
    cancelbutton.alpha = alpha2;

    if (!active)
        return;
 
    DMesh::Handle h(theDMesh());
    h.mp.translateZ(1.f);

    okbutton.renderButton(h.mp, false);
    cancelbutton.renderButton(h.mp, false);

    h.Draw(ss);

    okbutton.renderContents(ss);
    cancelbutton.renderContents(ss);

    ShaderState s1 = ss;
    s1.translateZ(1.f);
    okbutton.renderTooltip(s1, view, okbutton.textColor);
    cancelbutton.renderTooltip(s1, view, cancelbutton.textColor);
}

bool ConfirmWidget::HandleEvent(const Event* event, bool *selection)
{
    if (!active)
        return false;
    bool isActivate = false;
    if (okbutton.HandleEvent(event, &isActivate) && isActivate)
    {
        if (selection)
            *selection = true;
        active = false;
    }
    if ((cancelbutton.HandleEvent(event, &isActivate) && isActivate) ||
        (allow_dismiss && event->type == Event::MOUSE_UP && !intersectPointRectangle(event->pos, position, size/2.f))
        )
    {
        if (selection)
            *selection = false;
        active = false;
    }

    // always handle when active
    return true;
}

ScrollMessageBox::ScrollMessageBox()
{
    okbutton.setText(_("OK"));
    title = _("Message");
    okbutton.setReturnKeys();
    message.sizeChars = int2(80, 30);
    message.locked = true;
    message.textSize = 13.f;
    message.wrapText = true;
    active = false;
}

void ScrollMessageBox::render(const ShaderState &s1, const View& view)
{
    const float titleSize = 36;
    message.size.x = 0.8f * view.sizePoints.x;
    size = max(0.9f * view.sizePoints,
               message.size + 6.f * kBoxPad +
               justY(GLText::getScaledSize(titleSize) + okbutton.size.y));

    position = 0.5f * view.sizePoints;

    okbutton.position = position - justY(size / 2.f) + justY(kBoxPad + 0.5f * okbutton.size.y);
    okbutton.alpha = alpha;

    if (alpha < epsilon || !active)
        return;

    ShaderState ss = s1;
    ss.translateZ(2.1f);

    {
        View fview = view;
        fview.alpha = 0.5f * alpha;
        fadeFullScreen(fview, COLOR_BLACK);
    }

    const float2 boxRad = size / 2.f;

    DrawFilledRect(ss, position, boxRad, kGUIBg, kGUIFg, alpha);

    float2 pos = position + justY(boxRad) - justY(kBoxPad);

    pos.y -= GLText::Put(ss, pos, GLText::DOWN_CENTERED, MultAlphaAXXX(kGUIText, alpha),
                         titleSize, title).y;

    message.position = pos - justY(message.size/2.f + kButtonPad.y);
    message.render(ss);

    renderOneButton(ss, okbutton);
}

bool ScrollMessageBox::HandleEvent(const Event* event)
{
    if (!active)
        return false;
    if (message.HandleEvent(event))
        return true;

    bool isActivate = false;
    if (okbutton.HandleEvent(event, &isActivate) && isActivate)
    {
        active = false;
    }

    // always handle when active
    return true;
}

void ScrollMessageBox::activateSetText(const char* txt)
{
    message.sizeChars.x = floor_int(0.8f * globals.windowSizePoints.x / message.getCharSize().x);
    message.setText(txt);
    message.cursor = int2(0, 0);
    message.startChars.y = 0;
    active = true;
}

static void setupHsvRect(VertexPosColor* verts, float2 pos, float2 rad, float alpha,
                         const std::initializer_list<float3> c)
{
    // 0 1
    // 2 3
    verts[0].pos = float3(pos + flipX(rad), 0.f);
    verts[1].pos = float3(pos + rad, 0.f);
    verts[2].pos = float3(pos - rad, 0.f);
    verts[3].pos = float3(pos + flipY(rad), 0.f);

    for (int i=0; i<4; i++)
        verts[i].color = ALPHAF(alpha)|rgb2bgr(rgbf2rgb(c.begin()[i]));
}

void ColorPicker::render(const ShaderState &ss)
{
    DrawFilledRect(ss, position, size/2.f, kGUIBg, kGUIFg, alpha);

    hueSlider.size = float2(size.x - 2.f * kButtonPad.x, 0.15f * size.y);
    hueSlider.position.x = position.x - size.x / 2.f + hueSlider.size.x / 2.f + kButtonPad.x;
    hueSlider.position.y = position.y + size.y / 2.f - hueSlider.size.y / 2.f - kButtonPad.y;

    svRectSize.y = size.y - hueSlider.size.y - 3.f * kButtonPad.y;
    svRectSize.x = svRectSize.y;
    svRectPos = position - size/2.f + kButtonPad + svRectSize / 2.f;

    const float2 csize = size - float2(svRectSize.x, hueSlider.size.y) - 3.f * kButtonPad;
    const float2 ccPos = position + flipY(size / 2.f) + flipX(kButtonPad + csize / 2.f);

    DMesh::Handle h(theDMesh());
    LineMesh<VertexPosColor> &lmesh = h.mp.line;

    // draw color picker
    {
        // 0 1
        // 2 3
        VertexPosColor verts[4];
        const uint indices[] = { 0,1,3, 0,3,2 };
        ShaderState s1 = ss;
        s1.color32(kGUIFg, alpha);

        setupHsvRect(verts, hueSlider.position, hueSlider.size/2.f, alpha, {
                float3(0.f, 1.f, 1.f), float3(M_TAUf, 1.f, 1.f),
                    float3(0.f, 1.f, 1.f), float3(M_TAUf, 1.f, 1.f) });

        DrawElements(ShaderHsv::instance(), ss, GL_TRIANGLES, verts, indices, arraySize(indices));
        s1.color32(hueSlider.getFGColor(), alpha);
        lmesh.color32(hueSlider.getFGColor(), alpha);
        lmesh.PushRect(hueSlider.position, hueSlider.size/2.f);

        const float hn = hsvColor.x / 360.f;
        setupHsvRect(verts, svRectPos, svRectSize/2.f, alpha, {
                float3(hn, 0.f, 1.f), float3(hn, 1.f, 1.f),
                    float3(hn, 0.f, 0.f), float3(hn, 1.f, 0.f) });

        DrawElements(ShaderHsv::instance(), ss, GL_TRIANGLES, verts, indices, arraySize(indices));
        lmesh.color32((svHovered || svDragging) ? kGUIFgActive : kGUIFg, alpha);
        lmesh.PushRect(svRectPos, svRectSize/2.f);
    }

    // draw indicators
    lmesh.color(GetContrastWhiteBlack(getColor()), alpha);
    lmesh.PushCircle(svRectPos - svRectSize/2.f + float2(hsvColor.y, hsvColor.z) * svRectSize,
                     4.f, 6);
    lmesh.color(GetContrastWhiteBlack(hsvf2rgbf(float3(hsvColor.x, 1.f, 1.f))), alpha);
    lmesh.PushRect(float2(hueSlider.position.x - hueSlider.size.x / 2.f + hueSlider.size.x * (hsvColor.x / 360.f),
                          hueSlider.position.y),
                   float2(kPadDist, hueSlider.size.y / 2.f));
    lmesh.Draw(ss, ShaderColor::instance());

    // draw selected color box
    ShaderState s1 = ss;
    s1.translateZ(1.1f);
    DrawFilledRect(s1, ccPos, csize/2.f, ALPHA_OPAQUE|getColor(), kGUIFg, alpha);
}


bool ColorPicker::HandleEvent(const Event* event, bool *valueChanged)
{
    if (hueSlider.HandleEvent(event, valueChanged))
    {
        hsvColor.x = hueSlider.getValueFloat() * 360.f;
        return true;
    }

    bool handled = false;
    if (event->isMouse())
        svHovered = intersectPointRectangle(event->pos, svRectPos, svRectSize/2.f);
    if (svHovered)
    {
        if (event->type == Event::MOUSE_DRAGGED ||
            event->type == Event::MOUSE_DOWN)
            svDragging = true;
        handled = (event->type != Event::MOUSE_MOVED);
    }

    if (event->isMouse() && svDragging)
    {
        if (event->type == Event::MOUSE_UP) {
            svDragging = false;
        } else {
            float2 pos = clamp((event->pos - (svRectPos - svRectSize / 2.f)) / svRectSize,
                               float2(0.f), float2(1.f));
            hsvColor.y = pos.x;
            hsvColor.z = pos.y;
            if (valueChanged)
                *valueChanged = true;
        }
        return true;
    }

    return handled;
}

static float2 getTextBoxPos(float2 point, float2 size, const View &view)
{
    for (int i=0; i<2; i++) {
        if (point[i] + size[i] > view.sizePoints[i]) {
            if (point[i] - size[i] > 0.f)
                size[i] = -size[i];
            else {
                point[i] = view.sizePoints[i] - size[i];
            }
        }
    }
    return round(point + 0.5f * size);
}

void TextBox::Draw(const ShaderState& ss1, float2 point, const string& text) const
{
    if ((fgColor&ALPHA_OPAQUE) == 0 || alpha < epsilon)
        return;

    point = floor(point) + f2(0.5f);
    
    ShaderState ss = ss1;
    const GLText* st = GLText::get(font, tSize, text);
    const float2 boxRad = max(5.f + 0.5f * st->getSize(), box);

    float2 center = view ? getTextBoxPos(point, rad + st->getSize(), *view) : point;

    ss.translate(center);
    ss.color32(bgColor, alpha);
    ss.translateZ(1.f);
    ShaderUColor::instance().DrawRect(ss, boxRad);
    ss.color32(fgColor, alpha);
    ss.translateZ(0.1f);
    ShaderUColor::instance().DrawLineRect(ss, boxRad);
    ss.translate(round(-0.5f * st->getSize()));
    st->render(&ss);
}

void TextBox::DrawSub(const ShaderState& ss1, float2 point, const string& text, const string &text2, float text2size) const
{
    if ((fgColor&ALPHA_OPAQUE) == 0 || alpha < epsilon)
        return;

    point = floor(point) + f2(0.5f);

    ShaderState ss = ss1;
    const GLText* st = GLText::get(font, tSize, text);
    const GLText* st2 = GLText::get(font, text2size, text2);
    
    float2 boxRad = max(5.f + 0.5f * st->getSize(), box);
    boxRad.x = max(boxRad.x, 5.f + 0.5f * st2->getSize().x);
    boxRad.y += st2->getSize().y/2.f;

    ss.translate(point);
    ss.color32(bgColor, alpha);
    ss.translateZ(1.f);
    ShaderUColor::instance().DrawRect(ss, boxRad);
    ss.color32(fgColor, alpha);
    ss.translateZ(0.1f);
    ShaderUColor::instance().DrawLineRect(ss, boxRad);
    float2 a = round(-0.5f * st->getSize() + justY(0.5f * st2->getSize()));
    ss.translate(a);
    st->render(&ss);
    ss.translate(round(f2(-0.5f, -1.f) * st2->getSize()) - a);
    st2->render(&ss);
}

bool OverlayMessage::isVisible() const
{
    return message.size() && (globals.renderTime < startTime + totalTime);
}


bool OverlayMessage::setMessage(string msg, uint clr)
{
    std::lock_guard<std::mutex> l(mutex);
    bool changed = (msg != message) || (globals.renderTime > startTime + totalTime);
    message = move(msg);
    startTime = globals.renderTime;
    if (clr)
        color = clr;
    return changed;
}

void OverlayMessage::setVisible(bool visible)
{
    if (visible)
        startTime = globals.renderTime;
    else
        startTime = 0.f;
}

void OverlayMessage::render(const ShaderState &ss)
{
    std::lock_guard<std::mutex> l(mutex);
    if (!isVisible())
        return;
    const float a = alpha * easeOutExpo(inv_lerp_clamp(startTime + totalTime, startTime, (float)globals.renderTime));
    if (border)
    {
        const GLText *txt = GLText::get(font, textSize, message);
        ShaderState s1 = ss;
        s1.translate(position);
        s1.translateZ(4.f);
        ShaderUColor::instance().DrawColorRect(s1, ALPHA(a*0.75f)|COLOR_BLACK,
                                               txt->getSize()/2.f + 2.f * kButtonPad);
    }
    size = GLText::Put(ss, position, align, font, SetAlphaAXXX(color, a), textSize, message);
}

bool HandleConfirmKey(const Event *event, int* slot, int selected, bool *sawUp,
                      int key0, int key1, bool *isConfirm)
{
    if (*slot >= 0 && selected != *slot) {
        *slot = -1;
        return false;
    }

    if (!(event->isKey() && (event->key == key0 || event->key == key1)))
        return false;

    if (*slot == -1 && event->isDown())
    {
        *sawUp = false;
        *slot = selected;
    }
    else if (*slot >= 0 && event->isUp())
    {
        *sawUp = true;
    }
    else if (*slot >= 0 && *sawUp && event->isDown())
    {
        *slot = -1;
        *sawUp = false;
        *isConfirm = true;
    }
    PLAY_BUTTON_PRESS();
    return true;
}

bool HandleEventSelected(int* selected, ButtonBase &current, int count, int cols,
                         const Event* event, bool* isActivate)
{
    if (event->isEnter())
    {
        if (current.active)
        {
            *isActivate     = true;
            current.pressed = true;
            PLAY_BUTTON_PRESS();
        }
        else
        {
            PLAY_BUTTON_HOVER();
        }
        return false;
    }

    if (event->isEnterUp())
    {
        current.pressed = false;
    }

    int2 translation = GET_MENU_TRANSLATION(event);
    if (translation != int2())
    {
        if (cols > 1)
        {
            translation = -translation;
            swap(translation.x, translation.y);
        }
        *selected = modulo(*selected - translation.y + translation.x * cols, count);
        PLAY_BUTTON_HOVER();
        return true;
    }

    return false;
}

bool ButtonHandleEvent(ButtonBase &button, const Event* event, bool* isActivate, bool* isPress, int *selected)
{
    if (!button.visible)
        return false;
    const bool wasHovered = button.hovered;

    bool handled = false;
    bool activate = false;
    bool press = false;

    handled = button.HandleEvent(event, &activate, &press);

    if ((isActivate && activate) || (isPress && press))
    {
        PLAY_BUTTON_PRESS();
    }
    else if (!wasHovered && button.hovered && button.active)
    {
        PLAY_BUTTON_HOVER();
        if (selected)
            *selected = button.index;
    }

    if (isActivate && activate)
        *isActivate = true;
    if (isPress && press)
        *isPress = true;

    return handled;
}


float2 renderButtonText(const ShaderState &ss, float2 pos, float width,
                        GLText::Align align, int font, uint color, float *fontSize,
                        float fmin, float fmax, const string& text)
{
    if (text.empty())
        return float2();
    if (*fontSize <= 0.f)
        *fontSize = fmax;
    float2 tx = GLText::Put(ss, pos, align, font, color, *fontSize, text);
    float ts = clamp(*fontSize * (width / tx.x), fmin, fmax);
    if (fabsf(*fontSize - ts) >= 1.f)
    {
        tx *= (ts / *fontSize);
        *fontSize = ts;
    }
    return tx;
}

Rect2d Scrollbar::thumb() const
{
    Rect2d r;
    if (total)
    {
        r.rad = float2(size.x, max(kScrollbarWidth/2.f, min(visible, total) * size.y / total)) / 2.f - kButtonPad;
        r.pos = position + justY(size.y/2.f * (1.f - (sfirst + min(sfirst + visible, (float)total)) / total));
        if (r.pos.y + r.rad.y > position.y + size.y/2.f)
            r.pos.y = position.y + size.y/2.f - r.rad.y;
        else if (r.pos.y - r.rad.y < position.y - size.y/2.f)
            r.pos.y = position.y - size.y/2.f + r.rad.y;
    }
    else
    {
        r.rad = float2(size.x, max(kScrollbarWidth/2.f, size.y)) / 2.f - kButtonPad;
        r.pos = position;
    }
    return r;
}

void Scrollbar::render(DMesh &mesh)
{
    mesh.translateZ(0.5f);
    // mesh.line.translateZ(0.1f);

    if (size.x == 0.f)
        size.x = kScrollbarWidth;

    if (first + visible > total) {
        first = max(0, total - visible);
        sfirst = first;
    }

    mesh.line.color32(defaultFGColor, alpha);
    // mesh.line.translateZ(-0.1f);
    mesh.line.PushRect(position, size / 2.f);
    // mesh.line.translateZ(0.1f);

    const Rect2d th = thumb();
    // mesh.line.color32(hovered ? hoveredFGColor : defaultFGColor, alpha);
    // mesh.line.PushRect(pos, rad);
    mesh.tri.color32(pressed ? pressedFGColor : hovered ? hoveredFGColor : defaultFGColor, alpha);
    mesh.tri.PushRect(th.pos, th.rad);

    mesh.translateZ(-0.5f);

    if (moved)
    {
        // create dummy event to recompute hovered button
        Event e;
        e.type = Event::MOUSE_MOVED;
        e.pos = KeyState::instance().cursorPosScreen;
        moved = false;
        pushEvent(&e);
    }
}

bool Scrollbar::HandleEvent(const Event *event)
{
    if (!event->isMouse() &&
        event->type != Event::SCROLL_WHEEL &&
        !event->isKeyDown(NSPageUpFunctionKey) &&
        !event->isKeyDown(NSPageDownFunctionKey))
    {
        return false;
    }
    if (event->isMouse())
        hovered = intersectPointRectangle(event->pos, position, size/2.f);
    if (total == 0) {
        first = 0;
        visible = 0;
        hovered = false;
        pressed = false;
        sfirst = 0.f;
        return false;
    }
    const int maxfirst = total - min(visible, total);
    const bool parentHovered = (!parent || parent->hovered || hovered);
    const int page = min(1, visible-1);

    if (parentHovered)
    {
        int delta = 0;
        if (event->type == Event::SCROLL_WHEEL && fabsf(event->vel.y) > epsilon) {
            delta = ((event->vel.y > 0) ? -1 : 1);
        } else if (event->isKeyDown(NSPageUpFunctionKey)) {
            delta = -page;
        } else if (event->isKeyDown(NSPageDownFunctionKey)) {
            delta = page;
        }

        const int nfirst = clamp(first + delta, 0, maxfirst);
        if (nfirst != first)
        {
            first = nfirst;
            sfirst = nfirst;
            moved = true;
            return true;
        }
    }

    if (!event->isMouse())
        return false;

    if (pressed)
    {
        if (event->type == Event::MOUSE_DRAGGED) {
            sfirst = clamp(sfirst + total * event->vel.y / size.y, 0.f, (float)maxfirst);
            first = floor_int(sfirst);
            return true;
        } else {
            pressed = false;
            return true;
        }
    }
    if (!(hovered && event->type == Event::MOUSE_DOWN))
        return false;
    const Rect2d th = thumb();
    if (intersectPointRectangle(event->pos, th.pos, th.rad)) {
        pressed = true;         // start dragging thumb
    } else {
        // clicking in bar off of thumb pages up or down
        first = clamp(first + ((event->pos.y > th.pos.y) ? -page : page), 0, maxfirst);
        sfirst = first;
    }
    return true;
}

void Scrollbar::makeVisible(int row)
{
    int fst = first;
    if (visible >= total)
        fst = 0;
    else if (row < fst)
        fst = row;
    else if (row >= fst + visible)
        fst = row - visible + 1;

    if (fst != first)
    {
        first = fst;
        sfirst = fst;
    }
}

void Scrollbar::setup(const WidgetBase *base, i2 dims, int widget_count)
{
    parent = base;
    visible = dims.x * dims.y;
    total = widget_count;
    if (first >= total)
        first = 0;
    
    alpha = base->alpha;
    bool vis = (total > visible);
    size.x = vis ? kScrollbarWidth : 0;
    size.y = base->size.y - 2.f * kButtonPad.y;
    position = base->position + f2x(base->size.x/2.f - (size.x/2.f + kButtonPad.x));
}

void ButtonWindowBase::render(const ShaderState &ss)
{
    DMesh::Handle h(theDMesh());
    h.mp.translateZ(-1.f);
    h.mp.line.color(hovered ? kGUIFgActive : kGUIFg, alpha);
    h.mp.line.PushRect(position, size/2.f);
    h.mp.tri.color(kGUIBg, alpha / 2.f);
    h.mp.tri.PushRect(position, size/2.f);
    h.mp.translateZ(1.f);

    std::lock_guard<std::mutex> l(mutex);

    const int count = buttons.size();
    scrollbar.total = (count + (dims.x - 1)) / dims.x;
    scrollbar.visible = dims.y;
    if (scrollbar.first >= scrollbar.total)
        scrollbar.first = 0;

    const int    first = scrollbar.first * dims.x;
    const int    last  = min(scrollbar.last() * dims.x, (int)count);
    const bool   sbvis = (first != 0 || last != count);
    const float  sw    = sbvis ? kScrollbarWidth : 0.f;
    const float2 bsize = size - kButtonPad;

    ButtonBase  **pDrag = dragPtr;
    ButtonBase  *drag  = (pDrag ? *pDrag : NULL);


    if (count)
    {
        const float2 bs = float2((bsize.x - sw), bsize.y) / float2(dims);
        float2 pos = position - flipY(bsize/2.f) + justX(bs.x / 2.f);
        const float posx = pos.x;
        for (int i=scrollbar.first; i<scrollbar.last(); i++)
        {
            pos.x = posx;
            pos.y -= bs.y/2.f;
            for (int j=0; j<dims.x; j++)
            {
                const int idx = i * dims.x + j;
                if (idx >= count)
                    break;
                ButtonBase *bu = buttons[idx];
                ((bu == drag) ? dragPos : bu->position) = pos;
                bu->size = bs - 2.f * kButtonPad;
                bu->alpha = alpha;
                if (bu != drag)
                    bu->renderButton(h.mp);
                pos.x += bs.x;
            }
            pos.y -= bs.y/2.f;
        }
    }

    h.Draw(ss);
    h.clear();

    for (int i=0; i<count; i++)
    {
        ButtonBase *bu = buttons[i];
        if (bu == extDragPtr)
            continue;
        bu->visible = (first <= i && i < last);
        if (bu->visible && bu != drag)
            bu->renderContents(ss);
    }
    for (int i=first; i<last; i++)
        buttons[i]->renderContents1(ss);

    if (sbvis)
    {
        // scrollbar
        scrollbar.alpha = alpha;
        scrollbar.position = position + justX(size/2.f - sw/2.f);
        scrollbar.size = float2(sw, size.y) - 2.f * kButtonPad;
        scrollbar.render(h.mp);
        h.Draw(ss);
        h.clear();
    }

    if (drag)
    {
        ShaderState s1 = ss;
        s1.translateZ(0.75f);
        // s1.translateZ(5.f);
        drag->render(s1);
    }
}

bool ButtonWindowBase::HandleEvent(const Event *event,
                                   ButtonBase **activated,
                                   ButtonBase **dragged,
                                   ButtonBase **dropped)
{
    if (scrollbar.HandleEvent(event))
        return true;

    if (event->isMouse())
        hovered = intersectPointRectangle(event->pos, position, size/2.f);
    if (!hovered)
    {
        foreach (ButtonBase *bu, buttons)
            bu->hovered = false;
        if (rearrange)
            dragPtr = NULL;
        return false;
    }

    if (dragPtr)
        activated = NULL;
    ButtonBase *drag = dragPtr ? *dragPtr : NULL;

    bool handled = false;
    foreach (ButtonBase *&bu, buttons)
    {
        bool isActivate = false;
        bool isPress = false;
        handled |= ButtonHandleEvent(*bu, event,
                                     activated ? &isActivate : NULL,
                                     dragged ? &isPress : NULL);
        if (isActivate) {
            *activated = bu;
            handled = true;
        }
        if (dragged && bu->pressed && event->type == Event::MOUSE_DRAGGED && !drag) {
            *dragged = bu;
            handled = true;
        }
        if (dropped && bu->hovered && event->type == Event::MOUSE_UP && bu != drag) {
            *dropped = bu;
            handled = true;
        }
    }

    if (dragged && drag && event->type == Event::MOUSE_DRAGGED)
    {
        *dragged = drag;
        handled = true;
    }
    
    return handled || (dropped && event->type == Event::MOUSE_UP);
}

ButtonBase ** ButtonWindowBase::getPtr(ButtonBase *but)
{
    if (!but)
        return NULL;
    foreach (ButtonBase *&bu, buttons) {
        if (bu == but)
            return &bu;
    }
    return NULL;
}


bool ButtonWindowBase::setupDragPtr(const Event *event, ButtonBase *drag)
{
    if (!event->isMouse())
        return NULL;
    if (drag && !dragPtr)
    {
        dragPos = drag->position;
        dragOffset = drag->position - event->pos;
        Reportf("dragOffset changed for %s", event->toString().c_str());
    }
    dragPtr = getPtr(drag);
    return drag && dragPtr;
}

// dragging the button around swaps with other buttons
// return button drag swapped with
ButtonBase *ButtonWindowBase::HandleRearrange(const Event *event, ButtonBase *drag)
{
    std::lock_guard<std::mutex> l(mutex);
    rearrange = true;
    if (!setupDragPtr(event, drag))
        return NULL;
    const float2 rad = size/2.f - kButtonPad;
    drag->position = clamp(event->pos + dragOffset,
                           position - rad + drag->size/2.f,
                           position + rad - drag->size/2.f);
    
    foreach (ButtonBase *&bu, buttons)
    {
        if (bu != drag &&
            bu->visible &&
            distance(drag->position, bu->position) + min_dim(bu->size)/6.f < distance(drag->position, dragPos))
        {
            std::swap(bu->position, dragPos);
            ButtonBase* other = bu;
            std::swap(bu, *dragPtr);
            dragPtr = &bu;
            ASSERT(*dragPtr == drag);
            return other; // button we swapped with
        }
    }
    return NULL;
}

void ButtonWindowBase::swapButtons(ButtonBase *a, ButtonBase *b)
{
    ButtonBase **pa = getPtr(a);
    ButtonBase **pb = getPtr(b);
    if (!pa || !pb || a == b)
        return;
    std::swap(a->index, b->index);
    std::swap(*pa, *pb);
}


// buttons can be dragged out and dropped into another widget (but not rearranged)
bool ButtonWindowBase::HandleDragExternal(const Event *event, ButtonBase *drag, ButtonBase **drop)
{
    if (!event->isMouse())
        return false;
    if (hovered)
        setupDragPtr(event, drag);
    if (!dragPtr)
        return false;
    drag = *dragPtr;
    drag->position = event->pos + dragOffset;
    if (event->type != Event::MOUSE_DRAGGED && event->type != Event::MOUSE_DOWN)
    {
        if (dragPtr && drop)
            *drop = *dragPtr;
        dragPtr = NULL;
    }
    return true;
}

void ButtonWindowBase::computeDims(int2 mn, int2 mx)
{
    if (buttons.empty())
        return;

    int iters = 16;
    float2 bsize;
    const float count = min(mx.x * mx.y, (int)buttons.size());
    int2 ds;
    do {
        ds.x++;
        ds.y = ceil_int(count / ds.x);
        bsize = size / float2(ds);
        ds = clamp(ds, mn, mx);
    } while ((bsize.x > 2.f * bsize.y || ds.x * ds.y < count) &&
             ds.x <= mx.x && ds.y >= mn.y &&
             --iters);

    dims = ds;
}

void ButtonWindowBase::popButton(ButtonBase *but)
{
    ASSERT(but->index >= 0 && buttons[but->index] == but);
    {
        std::lock_guard<std::mutex> l(mutex);
        vec_erase(buttons, but->index);
        for (int i=but->index; i<buttons.size(); i++)
            buttons[i]->index--;
        if (dragPtr && *dragPtr == but)
            dragPtr = NULL;
    }
    delete but;
}


void ButtonSelector::render(const ShaderState &ss)
{
    if (alpha < epsilon)
        return;

    ButtonLayout bl;
    bl.start(position + flipX(size/2.f));
    bl.buttonCount = dims;
    bl.setTotalSize(size);

    bl.buttonSize *= float2(0.95f, 0.8f);
    bl.buttonSize.x -= bl.buttonSize.y;

    DMesh::Handle h(theDMesh());

    const int count = buttons.size();

    for (int y=0; y<dims.y; y++)
    {
        for (int x=0; x<dims.x; x++)
        {
            const int idx = scrollbar.first*dims.x + y*dims.y + x;
            if (idx >= count)
                break;
            ButtonBase &but = *buttons[idx];
            bl.setupPosSize(&but);
            but.alpha = bl.getButtonAlpha(alpha);
            but.renderButton(h.mp, selected == but.index);
            if (selected == but.index)
                but.renderSelected(ss, but.defaultBGColor, but.hoveredLineColor, but.alpha);
            bl.index.x++;
        }
        bl.row();
    }

    if (count > dims.x * dims.y)
    {
        scrollbar.alpha = alpha;
        scrollbar.position = position + justX(size/2.f);
        scrollbar.size.y = size.y;
        scrollbar.render(h.mp);
    }

    h.Draw(ss);

    for (int y=0; y<dims.y; y++) {
        for (int x=0; x<dims.x; x++) {
            const int idx = scrollbar.first*dims.x + y*dims.y + x;
            if (idx >= count)
                break;
            buttons[idx]->renderContents(ss);
        }
    }

    for (int y=0; y<dims.y; y++) {
        for (int x=0; x<dims.x; x++) {
            const int idx = scrollbar.first*dims.x + y*dims.y + x;
            if (idx >= count)
                break;
            buttons[idx]->renderContents1(ss);
        }
    }
}


bool ButtonSelector::HandleEvent(const Event *event, int *pressed)
{
    if (event->type == Event::SCROLL_WHEEL && event->synthetic)
        return false;           // scrolling and up/down are the same gamepad buttons
    if (scrollbar.HandleEvent(event))
        return true;
    bool isActivate = false;
    if (HandleEventSelected(&selected, *buttons[selected], buttons.size(), dims.x, event, &isActivate))
    {
        scrollbar.makeVisible(selected / dims.x);
        return true;
    }

    if (isActivate) {
        *pressed = selected;
        return true;
    }

    foreach (ButtonBase *bu, buttons)
    {
        if (bu->index/dims.x < scrollbar.first || bu->index/dims.x >= scrollbar.last())
            continue;
        if (ButtonHandleEvent(*bu, event, &isActivate, NULL, &selected)) {
            if (isActivate) {
                *pressed = bu->index;
            }
            return true;
        }
    }

    return false;
}
