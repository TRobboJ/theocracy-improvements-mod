#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct {
    volatile LONG title_active, mouse_percent, edge_percent;
    volatile LONG mouse_x_remainder, mouse_y_remainder;
} SharedOptions;

typedef struct { LONG left, top, width, height; } GameRect;
typedef struct { LONG x, y; } GamePoint;
typedef struct { void *control, *argument; } GameEvent;
typedef void (__thiscall *ButtonCtor)(void *, const GameRect *, void *, const char *,
    int, int, int, int, int, int);
typedef void *(__thiscall *PanelCtor)(void *, int);
typedef void (__thiscall *SkinnedButtonCtor)(void *, const GamePoint *, void *, void *,
    void *, const char *, int, const GamePoint *, int, int, int, int, int);
typedef void *(__thiscall *ChildIterator)(void *);
typedef void (__thiscall *LoadGameArt)(void *, const char *, int);
typedef void (__thiscall *MoveControl)(void *, LONG, LONG, int);
typedef void (__thiscall *SliderCtor)(void *, const GamePoint *, int, void *, void *,
    int, int, int, int, int);
typedef LONG (__thiscall *ReadSlider)(void *);
typedef unsigned char (__thiscall *ReadGem)(void *);
typedef const char *(__thiscall *LocalizedText)(void *);
typedef void (__thiscall *GemCtor)(void *, const GamePoint *, void *, void *,
    int, int, int, int, int);
typedef void (__thiscall *PanelDraw)(void *, void *, const GamePoint *, void *);
typedef void (__cdecl *GameDrawText)(void *, void *, const char *, LONG, LONG, void *, int);
typedef void (__thiscall *SetValue)(void *, int);
typedef void (__thiscall *SetPoint)(void *, const GamePoint *);
typedef void (__thiscall *SetPointer)(void *, const void *);
typedef void (__thiscall *GameMethod)(void *);
typedef void *(__thiscall *PanelDtor)(void *, unsigned);
typedef void (__thiscall *PanelEvent)(void *, const GameEvent *);
typedef unsigned char (__thiscall *PanelInput)(void *, const unsigned *);
typedef void (__thiscall *ShowPanel)(void *, void *, int);
typedef GamePoint *(__cdecl *MeasureText)(GamePoint *, void *, const char *);

enum {
    SKIP_VIDEOS = 1200, MOUSE_SLIDER, SWITCH_SETTINGS_PAGE,
    RESET_BINDS, BINDING_ROW = 1300,
    MOUSE_SLIDER_Y = 191, CD_TRACKS_Y = 228, SKIP_VIDEOS_Y = 262
};

static SharedOptions *const shared = (SharedOptions *)0x006c3f00;
static HINSTANCE module;
static unsigned char options_control[0xe0] __attribute__((aligned(4)));
enum { CONTROLS_ROWS = 15, MAX_BINDINGS = 2 * CONTROLS_ROWS, MAX_CHILDREN = 48 };
static unsigned char page_button[0xe0] __attribute__((aligned(4)));
static BOOL controls_page;
static BOOL controls_art_ready;
static unsigned char controls_art[0x48] __attribute__((aligned(4)));
static struct { void *control; GamePoint position; } game_controls[MAX_CHILDREN];
static size_t game_control_count;
typedef struct {
    char action[64], key[32];
    WCHAR id[64];
    unsigned stock_code, default_code, active_code, draft_code;
    unsigned context;
    BOOL editable;
} Binding;
static Binding bindings[MAX_BINDINGS];
static unsigned char row_buttons[MAX_BINDINGS][0xe0] __attribute__((aligned(4)));
static unsigned char reset_button[0xe0] __attribute__((aligned(4)));
static unsigned char released_as[100];
enum { GAMEPLAY, DIALOG, TITLE_MENU, STARTUP_VIDEO };
enum { TITLE_LEFT_CLICK = 100, CONSUME_KEY = 255 };
static BOOL suppress_title_release;
static const WCHAR *const binding_sections[] = {
    L"Defaults.Gameplay", L"Defaults.Dialog", L"Defaults.TitleMenu", L"Defaults.StartupVideo"
};
static const WCHAR *const override_sections[] = {
    L"Overrides.Gameplay", L"Overrides.Dialog", L"Overrides.TitleMenu", L"Overrides.StartupVideo"
};
static int capture_row = -1;
static BOOL bindings_dirty;
static const char *controls_notice;
static void read_bindings(void);
static const unsigned *const game_key_table = (const unsigned *)0x005e2af8;
static size_t binding_count;
static unsigned char skip_gem[0xe0] __attribute__((aligned(4)));
static unsigned char mouse_slider[0x93] __attribute__((aligned(4)));
static void *options_panel;
static void **const camera_settings = (void **)0x00610664;
// The stock settings code accesses only the scroll-speed field at +0x4e.
static unsigned char title_scroll_settings[0x52] __attribute__((aligned(4)));

static void *use_title_scroll_settings(void) {
    void *previous = *camera_settings;
    if (!previous) *camera_settings = title_scroll_settings;
    return previous;
}
static void *panel_vtable[6];
static LONG draft_mouse;
static BOOL save_failed;
static char mouse_label[48];

static BOOL get_mod_path(WCHAR *path, const WCHAR *name) {
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    WCHAR *file = wcsrchr(path, L'\\');
    if (!file || file - path + lstrlenW(name) + 2 > MAX_PATH) return FALSE;
    lstrcpyW(file + 1, name);
    return TRUE;
}

static LONG read_percent(const WCHAR *path, const WCHAR *key) {
    UINT value = GetPrivateProfileIntW(L"Options", key, 100, path);
    return value >= 25 && value <= 200 ? (LONG)value : 100;
}

__declspec(dllexport) int __cdecl TheocracyOptions(void) {
    WCHAR path[MAX_PATH];
    if (!get_mod_path(path, L"Options.ini")) return 0;
    shared->mouse_percent = read_percent(path, L"MouseSpeed");
    read_bindings();

    return GetPrivateProfileIntW(L"Options", L"SkipOpeningVideos", 0, path) == 1;
}

static BOOL save_options(void) {
    WCHAR path[MAX_PATH], values[128];
    if (!get_mod_path(path, L"Options.ini")) return FALSE;
    int next = wsprintfW(values, L"MouseSpeed=%ld", draft_mouse) + 1;

    next += wsprintfW(values + next, L"SkipOpeningVideos=%d", !!((ReadGem)0x00451f90)(skip_gem)) + 1;
    values[next] = 0;
    if (!WritePrivateProfileSectionW(L"Options", values, path)) return FALSE;
    shared->mouse_x_remainder = 0;
    shared->mouse_y_remainder = 0;
    shared->mouse_percent = draft_mouse;

    return TRUE;
}

static void register_button(void *container, void *button, const GameRect *bounds,
                            void *font, const char *label, int command) {
    GamePoint offset = {2, 2};
    ((ButtonCtor)0x00550cd0)(button, bounds, font, label, 0, 0, 0, 1, command, 0);
    ((SetPoint)0x004ae3f0)(button, &offset);
    ((SetValue)0x004ae3e0)(button, 0);
    ((SetPointer)0x00551440)(button, (void *)0x006109c8);
    ((SetPointer)0x00551430)(button, *(void **)0x00616a7c);
    ((SetPointer)0x0044bed0)(container, button);
}

static unsigned game_code(unsigned vk) {
    for (unsigned i = 1; i < 100; ++i) {
        if (game_key_table[i] == vk) return i;
    }
    return 0;
}

static unsigned parse_stock_key(const WCHAR *key) {
    if (lstrcmpiW(key, L"MouseLeft") == 0) return TITLE_LEFT_CLICK;
    if (key[0] && !key[1]) return game_code((unsigned)CharUpperW((WCHAR *)(UINT_PTR)key[0]));
    if (key[0] == L'F' && key[1] >= L'1' && key[1] <= L'9') {
        WCHAR *end;
        unsigned f = wcstoul(key + 1, &end, 10);
        if (!*end && f <= 12) return game_code(VK_F1 + f - 1);
    }
    static const struct { const WCHAR *name; unsigned vk; } named[] = {
        {L"Backspace", VK_BACK}, {L"Space", VK_SPACE}, {L"Escape", VK_ESCAPE},
        {L"Tab", VK_TAB}, {L"Home", VK_HOME}, {L"End", VK_END},
        {L"Page Up", VK_PRIOR}, {L"Page Down", VK_NEXT},
        {L"Up", VK_UP}, {L"Down", VK_DOWN}, {L"Left", VK_LEFT}, {L"Right", VK_RIGHT}
    };
    for (size_t i = 0; i < ARRAYSIZE(named); ++i) {
        if (lstrcmpiW(key, named[i].name) == 0) return game_code(named[i].vk);
    }
    return 0;
}

static unsigned native_action_key(unsigned context, const WCHAR *id) {
    // These keys identify the original executable's dispatch targets.
    // The user's physical keys are supplied by Defaults and Overrides.
    static const struct { const WCHAR *id; unsigned vk; } gameplay[] = {
        {L"OpenSideMenu", VK_SPACE}, {L"PauseGame", VK_BACK},
        {L"OpenTeamHandlerPanel", 'T'}, {L"OpenGameSettings", 'O'},
        {L"NormalView", VK_F1}, {L"BirdsEyeView", VK_F2}, {L"StrategicView", VK_F3},
        {L"OpenProvinceInfoPanel", 'I'}, {L"OpenMissionObjectives", 'M'},
        {L"PlaceCaravanCamp", 'L'}, {L"OpenBuildingPanel", 'B'},
        {L"MoveCameraUp", VK_UP}, {L"MoveCameraDown", VK_DOWN},
        {L"MoveCameraLeft", VK_LEFT}, {L"MoveCameraRight", VK_RIGHT},
        {L"Formation1", VK_F9}, {L"Formation2", VK_F10},
        {L"Formation3", VK_F11}, {L"Formation4", VK_F12},
        {L"Stop", 'S'}, {L"HoldPosition", 'H'}, {L"Patrol", 'P'},
        {L"SaveGame", VK_F5}, {L"OpenStorageInfoPanel", 'P'}, {L"OpenDiplomacyPanel", 'D'}
    };
    if (context == TITLE_MENU && lstrcmpW(id, L"ActivatePointedItem") == 0) {
        return TITLE_LEFT_CLICK;
    }
    if (context == DIALOG && lstrcmpW(id, L"CloseActiveDialog") == 0) return game_code(VK_ESCAPE);
    if (context != GAMEPLAY) return 0;
    for (size_t i = 0; i < ARRAYSIZE(gameplay); ++i) {
        if (lstrcmpW(id, gameplay[i].id) == 0) return game_code(gameplay[i].vk);
    }
    return 0;
}
static BOOL assignable(unsigned code) {
    if (!code || code >= 100) return FALSE;
    unsigned vk = game_key_table[code];
    return (vk >= 'A' && vk <= 'Z') || (vk >= VK_F1 && vk <= VK_F12) ||
        (vk >= VK_LEFT && vk <= VK_DOWN) || vk == VK_SPACE || vk == VK_BACK ||
        vk == VK_TAB || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT;
}

static BOOL available_key(size_t row, unsigned code, BOOL use_draft) {
    for (size_t i = 0; i < binding_count; ++i) {
        if (i == row || bindings[i].context != bindings[row].context) continue;
        const Binding *other = &bindings[i];
        unsigned used = use_draft ? other->draft_code : other->active_code;
        if (used == code) return FALSE;
    }
    return TRUE;
}

static void resolve_loaded_bindings(BOOL use_defaults) {
    // All overrides are loaded before checking conflicts so a reassignment
    // can use a stock key freed by another action later in the file.
    BOOL changed;
    do {
        changed = FALSE;
        for (size_t i = 0; i < binding_count; ++i) {
            Binding *binding = &bindings[i];
            unsigned fallback = use_defaults ? binding->stock_code : binding->default_code;
            if (binding->active_code != fallback &&
                !available_key(i, binding->active_code, FALSE)) {
                binding->active_code = fallback;
                changed = TRUE;
            }
        }
    } while (changed);
    for (size_t i = 0; i < binding_count; ++i) {
        if (use_defaults) bindings[i].default_code = bindings[i].active_code;
        bindings[i].draft_code = bindings[i].active_code;
    }
}

static void key_caption(unsigned code, char *caption, size_t size) {
    if (code == TITLE_LEFT_CLICK) {
        snprintf(caption, size, "Left click");
        return;
    }
    unsigned vk = game_key_table[code];
    if (vk >= 'A' && vk <= 'Z') {
        snprintf(caption, size, "%c", (char)vk);
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        snprintf(caption, size, "F%u", vk - VK_F1 + 1);
    } else {
        unsigned scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        LONG parameter = (LONG)((scan & 0xff) << 16);
        if (scan & 0xff00) parameter |= 1 << 24;
        if (!GetKeyNameTextA(parameter, caption, (int)size)) {
            snprintf(caption, size, "Key %u", code);
        }
    }
}

static BOOL save_bindings(void) {
    if (!bindings_dirty) return TRUE;
    WCHAR path[MAX_PATH];
    if (!get_mod_path(path, L"keybinds.cfg")) return FALSE;
    for (unsigned context = GAMEPLAY; context <= TITLE_MENU; ++context) {
        WCHAR values[2048] = {0};
        size_t used = 0;
        for (size_t i = 0; i < binding_count; ++i) {
            const Binding *binding = &bindings[i];
            if (binding->context == context && binding->editable &&
                binding->draft_code != binding->default_code) {
                used += wsprintfW(values + used, L"%s=%u", binding->id, binding->draft_code) + 1;
            }
        }
        if (!WritePrivateProfileSectionW(override_sections[context], values, path)) return FALSE;
    }    for (size_t i = 0; i < binding_count; ++i) bindings[i].active_code = bindings[i].draft_code;
    bindings_dirty = FALSE;
    return TRUE;
}

static BOOL has_modal(const void *engine) {
    const unsigned char *state = engine;
    void *manager = *(void * const *)(state + 0x22);
    if (!manager) return FALSE;
    const unsigned char *top = ((ChildIterator)0x005507c0)(manager);
    return options_panel || (top && top[0x68]);
}

enum { GROUP_FIRST_KEY = 2, GROUP_COUNT = 10, GROUP_DOUBLE_TAP_MS = 350 };
static void *group_province;
static unsigned group_last_key;
static unsigned group_pending_key;
static DWORD group_last_press;
static unsigned group_held;

static void track_group_key(const unsigned *event, const void *engine) {
    unsigned key = event[1];
    if (event[0] != 8 && event[0] != 16) return;
    if (key == 0 || key >= 100) return;
    void *province = *(void **)0x006a42a8;
    if (!province || shared->title_active || !*camera_settings ||
        has_modal(engine) || group_province != province) {
        group_province = province;
        group_last_key = group_pending_key = group_held = 0;
        if (!province || shared->title_active || !*camera_settings || has_modal(engine)) return;
    }
    if (key < GROUP_FIRST_KEY || key >= GROUP_FIRST_KEY + GROUP_COUNT) {
        if (event[0] == 8) group_last_key = group_pending_key = 0;
        return;
    }
    unsigned mask = 1u << (key - GROUP_FIRST_KEY);
    if (event[0] == 16) {
        group_held &= ~mask;
        return;
    }
    if (group_held & mask) return;
    group_held |= mask;
    typedef unsigned char (__thiscall *KeyHeld)(const void *, unsigned);
    KeyHeld held = (KeyHeld)0x0044f330;
    if (held(engine, 0x39) || held(engine, 0x3a)) {
        group_last_key = group_pending_key = 0;
        return;
    }
    DWORD now = GetTickCount();
    group_pending_key = 0;
    if (group_last_key == key && now - group_last_press <= GROUP_DOUBLE_TAP_MS) {
        group_pending_key = key;
        group_last_key = 0;
    } else {
        group_last_key = key;
    }
    group_last_press = now;
}

__declspec(dllexport) void __cdecl TheocracyFocusLocation(void *province, LONG x, LONG y) {
    typedef void (__thiscall *FocusLocation)(void *, LONG, LONG);
    ((FocusLocation)0x004dd170)(province, x, y);
}

__declspec(dllexport) void __cdecl TheocracyRecallGroup(
        unsigned char *groups, unsigned slot, unsigned char *province) {
    typedef void (__thiscall *Recall)(void *, unsigned, void *);
    typedef unsigned (__thiscall *Count)(const void *);
    typedef void *(__thiscall *Entry)(void *, unsigned);
    typedef void *(__thiscall *Resolve)(void *);
    typedef unsigned char (__thiscall *ByteProperty)(const void *);
    typedef void (__thiscall *WorldBounds)(void *, GameRect *);
    ((Recall)0x004c8c70)(groups, slot, province);
    if (slot >= GROUP_COUNT || !group_pending_key ||
        group_pending_key != slot + GROUP_FIRST_KEY || province != group_province ||
        GetTickCount() - group_last_press > GROUP_DOUBLE_TAP_MS || !*camera_settings) return;
    unsigned player = ((ByteProperty)0x004348b0)(*camera_settings);
    if (groups != province + 0x400f1 + player * 200) return;
    group_pending_key = 0;

    void *members = groups + slot * 20;
    unsigned count = ((Count)0x00567d90)(members);
    void *target = NULL;
    for (unsigned i = 0; i < count; ++i) {
        void *entry = ((Entry)0x004e2fe0)(members, i);
        void *unit = ((Resolve)0x00401c40)(entry);
        if (!unit) continue;
        if (((ByteProperty)0x00407860)(unit)) {
            target = unit;
            break;
        }
        if (!target) target = unit;
    }
    if (target) {
        GameRect bounds;
        ((WorldBounds)0x00498b70)(target, &bounds);
        TheocracyFocusLocation(province, bounds.left + bounds.width / 2,
                              bounds.top + bounds.height / 2);
    }
}

static void remap_title_click(unsigned *event, const void *engine) {
    if (event[1] == 1 && suppress_title_release) {
        suppress_title_release = FALSE;
        event[0] = 0;
        return;
    }
    if (!shared->title_active || event[1] != 0 || has_modal(engine)) return;
    for (size_t i = 0; i < binding_count; ++i) {
        const Binding *binding = &bindings[i];
        if (binding->context == TITLE_MENU && binding->stock_code == TITLE_LEFT_CLICK &&
            binding->active_code != TITLE_LEFT_CLICK) {
            suppress_title_release = TRUE;
            event[0] = 0;
            return;
        }
    }
}
__declspec(dllexport) void __cdecl TheocracyKeyEvent(unsigned *event, const void *engine) {
    track_group_key(event, engine);
    if (event[0] == 4) {
        remap_title_click(event, engine);
        return;
    }
    const unsigned key = event[1];
    if ((event[0] != 8 && event[0] != 16) || key == 0 || key >= 100) return;
    BOOL pressed = event[0] == 8;
    if (!pressed && released_as[key]) {
        unsigned mapped = released_as[key];
        released_as[key] = 0;
        if (mapped == CONSUME_KEY) event[0] = 0;
        else if (mapped == TITLE_LEFT_CLICK) {
            event[0] = 4;
            event[1] = 1;
        } else event[1] = mapped;
        return;
    }
    if (options_panel && controls_page && capture_row >= 0) {
        if (!pressed) return;
        // Modifier transitions must still reach the game. Capturing them
        // would leave its held-key state stale across focus changes.
        const unsigned vk = game_key_table[key];
        if (vk >= VK_LSHIFT && vk <= VK_RMENU) return;
        if (vk == VK_ESCAPE) {
            capture_row = -1;
            controls_notice = NULL;
        } else if (!assignable(key)) {
            controls_notice = "Choose a single letter, function, or navigation key";
        } else if (!available_key((size_t)capture_row, key, TRUE)) {
            controls_notice = "That key is already assigned";
        } else {
            bindings[capture_row].draft_code = key;
            bindings_dirty = TRUE;
            capture_row = -1;
            controls_notice = "Press OK to save changes";
        }
        released_as[key] = CONSUME_KEY;
        event[0] = 0;
        ((GameMethod)0x0054eec0)(options_panel);
        return;
    }
    if (!pressed) return;
    const unsigned char *state = engine;
    void *manager = *(void * const *)(state + 0x22);
    if (!manager || (state[0xaa] & 7)) return;
    unsigned context = has_modal(engine) ? DIALOG :
                       (shared->title_active ? TITLE_MENU : GAMEPLAY);
    // Text-entry focus uses the stock keyboard unchanged.
    if (context != TITLE_MENU && !options_panel &&
        *(void **)((unsigned char *)manager + 0x24)) return;
    if (context == GAMEPLAY && !*camera_settings) return;
    BOOL suppress_stock = FALSE;
    for (size_t i = 0; i < binding_count; ++i) {
        const Binding *binding = &bindings[i];
        if (binding->context != context || !binding->editable ||
            binding->active_code == binding->stock_code) continue;
        if (binding->active_code == key) {
            released_as[key] = (unsigned char)binding->stock_code;
            if (binding->stock_code == TITLE_LEFT_CLICK) {
                event[0] = 4;
                event[1] = 0;
            } else event[1] = binding->stock_code;
            return;
        }
        if (binding->stock_code == key) suppress_stock = TRUE;
    }
    if (suppress_stock) {
        released_as[key] = CONSUME_KEY;
        event[0] = 0;
    }
}
__declspec(dllexport) int __cdecl TheocracyCameraHeld(unsigned stock_code, void *engine) {
    for (size_t i = 0; i < binding_count; ++i) {
        const Binding *binding = &bindings[i];
        if (binding->context != GAMEPLAY || binding->stock_code != stock_code) continue;
        if (binding->active_code != stock_code) {
            void *keyboard = *(void **)0x006c0420;
            if (!keyboard || options_panel) return 0;
            // The Windows keyboard adapter retains the original physical
            // state even when the event dispatcher translates a key.
            return ((unsigned char (__thiscall *)(void *, unsigned))0x00566590)(
                keyboard, binding->active_code);
        }
        break;
    }
    return ((unsigned char (__thiscall *)(void *, unsigned))0x0044f330)(engine, stock_code);
}
static void read_bindings(void) {
    WCHAR path[MAX_PATH];
    binding_count = 0;
    if (!get_mod_path(path, L"keybinds.cfg")) return;
    for (size_t section = 0; section < ARRAYSIZE(binding_sections); ++section) {
        WCHAR entries[4096];
        DWORD length = GetPrivateProfileSectionW(binding_sections[section], entries,
                                                  ARRAYSIZE(entries), path);
        if (!length || length >= ARRAYSIZE(entries) - 2) continue;
        for (WCHAR *line = entries; *line && binding_count < MAX_BINDINGS;
             line += lstrlenW(line) + 1) {
            const WCHAR *equals = wcschr(line, L'=');
            if (!equals || !equals[1]) continue;
            Binding *binding = &bindings[binding_count];
            size_t id_length = (size_t)(equals - line);
            if (id_length >= ARRAYSIZE(binding->id)) continue;
            wmemcpy(binding->id, line, id_length);
            binding->id[id_length] = 0;
            binding->context = (unsigned)section;
            binding->stock_code = native_action_key(binding->context, binding->id);
            unsigned configured = parse_stock_key(equals + 1);
            binding->default_code = assignable(configured) ? configured : binding->stock_code;
            binding->active_code = binding->stock_code;
            binding->draft_code = binding->stock_code;
            char *action = binding->action;
            size_t written = 0;
            for (const WCHAR *p = line; p < equals && written < 60; ++p) {
                BOOL upper = *p >= L'A' && *p <= L'Z';
                BOOL digit = *p >= L'0' && *p <= L'9';
                BOOL previous_word = p != line &&
                    ((p[-1] >= L'a' && p[-1] <= L'z') || (p[-1] >= L'0' && p[-1] <= L'9'));
                if ((upper || digit) && previous_word) action[written++] = ' ';
                action[written++] = *p <= 127 ? (char)*p : '?';
            }
            action[written] = '\0';
            const WCHAR *key = equals + 1;
            if (lstrcmpW(key, L"MouseLeft") == 0) key = L"Left click";
            if (lstrcmpW(key, L"Ctrl+Number") == 0) key = L"Ctrl + #";
            if (WideCharToMultiByte(CP_ACP, 0, key, -1, bindings[binding_count].key,
                                    sizeof bindings[binding_count].key, NULL, NULL)) {
                ++binding_count;
            }
        }
    }
    for (size_t i = 0; i < binding_count; ++i) {
        Binding *binding = &bindings[i];
        binding->editable = binding->stock_code && available_key(i, binding->stock_code, FALSE);
    }
    for (size_t i = 0; i < binding_count; ++i) {
        Binding *binding = &bindings[i];
        binding->active_code = binding->editable ? binding->default_code : binding->stock_code;
    }
    resolve_loaded_bindings(TRUE);
    for (size_t i = 0; i < binding_count; ++i) {
        Binding *binding = &bindings[i];
        WCHAR setting[32];
        GetPrivateProfileStringW(override_sections[binding->context], binding->id, L"",
                                 setting, ARRAYSIZE(setting), path);
        WCHAR *end;
        unsigned code = wcstoul(setting, &end, 10);
        if (end == setting || *end) code = parse_stock_key(setting);
        if (binding->editable && (assignable(code) ||
            (binding->context == TITLE_MENU && code == TITLE_LEFT_CLICK))) {
            binding->active_code = code;
        }
    }
    resolve_loaded_bindings(FALSE);
}

static GamePoint binding_position(size_t index) {
    return (GamePoint){37 + (LONG)(index / CONTROLS_ROWS) * 263,
                       53 + (LONG)(index % CONTROLS_ROWS) * 19};
}
static void change_page(void *panel) {
    MoveControl move = (MoveControl)0x00450860;
    controls_page = !controls_page;
    capture_row = -1;
    controls_notice = NULL;
    if (controls_page) {
        if (!controls_art_ready) {
            WCHAR path[MAX_PATH];
            if (get_mod_path(path, L"ControlsBackground.raw") &&
                GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
                ((LoadGameArt)0x00546cf0)(controls_art,
                    "Theocracy Improvements Mod/ControlsBackground.raw", 1);
                controls_art_ready = TRUE;
            }
        }
        game_control_count = 0;
        ChildIterator first = (ChildIterator)0x00429800;
        ChildIterator next = (ChildIterator)0x00418eb0;
        for (void *child = first(panel); child; child = next(child)) {
            if (child == page_button || child == (unsigned char *)panel + 0x482 ||
                child == reset_button) continue;
            unsigned id = *(unsigned short *)((unsigned char *)child + 0x38);
            if (id >= BINDING_ROW && id < BINDING_ROW + MAX_BINDINGS) continue;
            if (game_control_count == MAX_CHILDREN) break;
            GameRect *bounds = (GameRect *)((unsigned char *)child + 0x28);
            game_controls[game_control_count].control = child;
            game_controls[game_control_count].position = (GamePoint){bounds->left, bounds->top};
            ++game_control_count;
            move(child, bounds->left, -800, 0);
        }
    } else {
        for (size_t i = 0; i < game_control_count; ++i) {
            GamePoint position = game_controls[i].position;
            move(game_controls[i].control, position.x, position.y, 0);
        }
    }
    for (size_t i = 0; i < binding_count; ++i) {
        GamePoint position = binding_position(i);
        move(row_buttons[i], position.x, controls_page ? position.y : -800, 0);
    }
    move(reset_button, 42, controls_page ? 365 : -800, 0);
    ((SetPointer)0x00456c20)(page_button, controls_page ? "Game" : "Controls");
    ((GameMethod)0x0054eec0)(panel);
}

static void draw_controls(void *panel, void *surface, const GamePoint *origin, void *clip) {
    void **background = (void **)((unsigned char *)panel + 0x69);
    void *stock = *background;
    if (controls_art_ready) *background = controls_art;
    ((PanelDraw)0x0044c520)(panel, surface, origin, clip);
    *background = stock;
    GameDrawText draw = (GameDrawText)0x0054c750;
    void *font = (void *)0x00612a88;
    GamePoint title_size;
    void *title_font = (void *)0x006125a0;
    ((MeasureText)0x004ad680)(&title_size, title_font, "Controls");
    const GameRect *frame = (const GameRect *)((unsigned char *)panel + 0x28);
    draw(surface, title_font, "Controls",
         origin->x + (frame->width - title_size.x) / 2, origin->y + 13, clip, 0);
    if (!binding_count) {
        draw(surface, font, "No bindings found in keybinds.cfg",
             origin->x + 40, origin->y + 55, clip, 0);
    }
    for (size_t i = 0; i < binding_count; ++i) {
        GamePoint position = binding_position(i);
        LONG left = origin->x + position.x;
        LONG top = origin->y + position.y;
        const Binding *binding = &bindings[i];
        const char *key = binding->key;
        char caption[32];
        if ((int)i == capture_row) key = "Press a key";
        else if (binding->editable) {
            key_caption(binding->draft_code, caption, sizeof caption);
            key = caption;
        }
        draw(surface, font, binding->action, left, top, clip, 0);
        draw(surface, font, key, left + 168, top, clip, 0);
    }
    if (controls_notice) draw(surface, font, controls_notice,
        origin->x + 39, origin->y + 343, clip, 0);
}
static void __thiscall options_draw(void *panel, void *surface,
                                    const GamePoint *origin, void *clip) {
    if (controls_page) {
        draw_controls(panel, surface, origin, clip);
        return;
    }
    ((PanelDraw)0x004c7420)(panel, surface, origin, clip);
    GameDrawText draw = (GameDrawText)0x0054c750;
    void *font = (void *)0x00612a88;
    LONG value = ((ReadSlider)0x00486630)(mouse_slider);
    snprintf(mouse_label, sizeof mouse_label, "Mouse Speed: %ld%%", value);
    draw(surface, font, mouse_label, origin->x + 39, origin->y + MOUSE_SLIDER_Y - 15, clip, 0);
    draw(surface, font, ((LocalizedText)0x00401cc0)((void *)0x006a3988),
         origin->x + 39, origin->y + MOUSE_SLIDER_Y, clip, 0);
    draw(surface, font, ((LocalizedText)0x00401cc0)((void *)0x006a3880),
         origin->x + 177, origin->y + MOUSE_SLIDER_Y, clip, 0);
    draw(surface, font, save_failed ? "Could not save Options.ini" : "Skip opening videos",
         origin->x + 64, origin->y + SKIP_VIDEOS_Y, clip, 0);
}
static void * __thiscall destroy_options(void *panel, unsigned flags) {
    for (size_t i = 0; i < binding_count; ++i) ((GameMethod)0x005511a0)(row_buttons[i]);
    ((GameMethod)0x005511a0)(reset_button);
    ((GameMethod)0x005511a0)(page_button);
    ((GameMethod)0x00450a50)(mouse_slider);
    ((GameMethod)0x005511a0)(skip_gem);
    *(void **)panel = (void *)0x0058796c;
    options_panel = NULL;
    return ((PanelDtor)0x004c7dd0)(panel, flags);
}

static BOOL commit_options(void *panel) {
    draft_mouse = ((ReadSlider)0x00486630)(mouse_slider);
    if (save_options() && save_bindings()) return TRUE;
    controls_notice = "Could not save settings";
    save_failed = TRUE;
    ((GameMethod)0x0054eec0)(panel);
    return FALSE;
}

static unsigned char __thiscall options_input(void *panel, const unsigned *event) {
    if (event[0] == 8 && (event[1] == 0x1a || event[1] == 1 || event[1] == 0x48)
        && !commit_options(panel)) return 0;
    void *previous = use_title_scroll_settings();
    unsigned char result = ((PanelInput)0x004c6f90)(panel, event);
    *camera_settings = previous;
    return result;
}

static void __thiscall options_event(void *panel, const GameEvent *event) {
    if (!event || !event->control) return;
    const unsigned command = *(unsigned short *)((unsigned char *)event->control + 0x38);
    if (command == SWITCH_SETTINGS_PAGE) {
        change_page(panel);
        return;
    }
    if (controls_page && command == RESET_BINDS) {
        for (size_t i = 0; i < binding_count; ++i) bindings[i].draft_code = bindings[i].default_code;
        bindings_dirty = TRUE;
        capture_row = -1;
        controls_notice = "Defaults restored. Press OK to save.";
        ((GameMethod)0x0054eec0)(panel);
        return;
    }
    if (controls_page && command >= BINDING_ROW && command < BINDING_ROW + binding_count) {
        size_t row = command - BINDING_ROW;
        capture_row = bindings[row].editable ? (int)row : -1;
        controls_notice = capture_row >= 0 ? "Press a new key or Escape to cancel" :
            "This binding cannot be edited yet";
        ((GameMethod)0x0054eec0)(panel);
        return;
    }
    if (command <= 6) {
        if (command == 0 && !commit_options(panel)) return;
        void *previous = use_title_scroll_settings();
        ((PanelEvent)0x004c7010)(panel, event);
        *camera_settings = previous;
        return;
    }
    if (command == MOUSE_SLIDER || command == SKIP_VIDEOS) {
        ((GameMethod)0x0054eec0)(panel);
    }
}

static void attach_options(void *panel) {
    if (options_panel) return;
    options_panel = panel;
    WCHAR path[MAX_PATH];
    draft_mouse = shared->mouse_percent;
    BOOL skip = get_mod_path(path, L"Options.ini") &&
        GetPrivateProfileIntW(L"Options", L"SkipOpeningVideos", 0, path) == 1;
    save_failed = FALSE;
    controls_page = FALSE;
    capture_row = -1;
    controls_notice = NULL;
    bindings_dirty = FALSE;
    read_bindings();

    for (int i = 0; i < 6; ++i) panel_vtable[i] = ((void **)0x0058796c)[i];
    panel_vtable[0] = (void *)destroy_options;
    panel_vtable[1] = (void *)options_draw;
    panel_vtable[2] = (void *)options_input;
    panel_vtable[3] = (void *)options_event;
    *(void ***)panel = panel_vtable;

    GameRect *cd_tracks = (GameRect *)((unsigned char *)panel + 0x156 + 0x28);
    cd_tracks->top = CD_TRACKS_Y;
    GamePoint gem_position = {38, SKIP_VIDEOS_Y};
    ((GemCtor)0x005509a0)(skip_gem, &gem_position,
        (void *)0x006a3af0, (void *)0x006a38c8, 1, 0, 1, SKIP_VIDEOS, 0);
    ((SetPointer)0x00551430)(skip_gem, *(void **)0x00616a7c);
    ((SetPointer)0x00551440)(skip_gem, (void *)0x006109c8);
    ((SetPointer)0x0044bed0)(panel, skip_gem);
    ((SetValue)0x00451d00)(skip_gem, 1);
    if (skip) ((GameMethod)0x005514d0)(skip_gem);

    GamePoint slider_position = {88, MOUSE_SLIDER_Y};
    ((SliderCtor)0x00450410)(mouse_slider, &slider_position, 0,
        (void *)0x006a3a30, (void *)0x006a39d0, 25, 200, 0, MOUSE_SLIDER, 0);
    ((SetPointer)0x004c7740)(mouse_slider, *(void **)0x00616a7c);
    ((SetPointer)0x004c7750)(mouse_slider, (void *)0x00612be8);
    ((SetValue)0x00450e00)(mouse_slider, draft_mouse);
    ((SetPointer)0x0044bed0)(panel, mouse_slider);

    const GamePoint button_position = {324, 367};
    const GamePoint button_offset = {1, 1};
    ((SkinnedButtonCtor)0x00551000)(page_button, &button_position,
        (void *)0x00612420, (void *)0x006123d8, (void *)0x006b6a18,
        "Controls", 0, &button_offset, 0, 0, 1, SWITCH_SETTINGS_PAGE, 0);
    ((SetPointer)0x00551430)(page_button, *(void **)0x00616a7c);
    ((SetPointer)0x00551440)(page_button, (void *)0x006109c8);
    ((SetPointer)0x0044bed0)(panel, page_button);
    void *font = (void *)0x00612a88;
    const GameRect reset_bounds = {42, -800, 158, 22};
    register_button(panel, reset_button, &reset_bounds, font, "Revert to Default", RESET_BINDS);
    for (size_t i = 0; i < binding_count; ++i) {
        const GameRect row = {0, -800, 246, 19};
        register_button(panel, row_buttons[i], &row, font, "", BINDING_ROW + (int)i);
    }
}

static void open_options(void) {
    if (options_panel) return;
    void *allocation = ((void *(__cdecl *)(unsigned))0x0056cf10)(0x642);
    if (!allocation) return;
    void *previous = use_title_scroll_settings();
    if (!previous) ((GameMethod)0x0047e430)(title_scroll_settings);
    void *panel = ((PanelCtor)0x004c6a50)(allocation, 0);
    *camera_settings = previous;
    ((ShowPanel)0x00471fb0)(*(void **)0x006c030c, panel, 1);
}
__declspec(dllexport) void __cdecl TheocracyMenuAction(
    int action, void *screen, void *help, void *credits) {
    if (action == 1) { open_options(); return; }
    if (action == 3) { attach_options(screen); return; }
    if (action == 2) {
        if (shared->title_active) {
            shared->title_active = 0;
            ((GameMethod)0x005511a0)(options_control);
        }
        return;
    }
    const GameRect *help_rect = (const GameRect *)((unsigned char *)help + 0x28);
    const GameRect *credits_rect = (const GameRect *)((unsigned char *)credits + 0x28);
    GameRect bounds = *help_rect;
    bounds.top = (help_rect->top + credits_rect->top) / 2;
    GamePoint size;
    void *font = (void *)0x0069bc78;
    ((MeasureText)0x004ad680)(&size, font, "Options");
    bounds.width = size.x;
    register_button(screen, options_control, &bounds, font, "Options", 12);
    shared->title_active = 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}




















/* Province zoom uses the stock software surface and leaves the HUD draw at 1:1. */
typedef struct {
    DWORD vtable;
    LONG width, height;
    unsigned short *pixels;
    DWORD format;
    LONG pitch;
    DWORD auxiliary;
    LONG auxiliary_pitch;
} ProvinceSurface;
typedef void (__thiscall *WorldDraw)(void *, ProvinceSurface *, const GamePoint *);
typedef GamePoint *(__thiscall *LocalPoint)(void *, GamePoint *);

static void *zoom_province;
static void *zoom_viewport;
static int zoom_distance = 100;
static int zoom_wheel_remainder;
static int zoom_left, zoom_top, zoom_width, zoom_height;
static int zoom_view_width, zoom_view_height;
static unsigned short *zoom_pixels;
static SIZE_T zoom_capacity;

typedef struct { int pixel, next; unsigned weight; } ZoomSample;

static ZoomSample zoom_sample(int output, int source_size, int output_size) {
    /* Map pixel centers. When enlarging, blend only at source pixel edges. */
    int position = ((2 * output + 1) * source_size * 16) / output_size - 16;
    if (position <= 0) return (ZoomSample){0, 0, 0};
    int pixel = position / 32;
    if (pixel >= source_size - 1)
        return (ZoomSample){source_size - 1, source_size - 1, 0};
    int weight = position % 32;
    if (source_size < output_size) {
        weight = 16 + (weight - 16) * output_size / source_size;
        if (weight < 0) weight = 0;
        if (weight > 32) weight = 32;
    }
    return (ZoomSample){pixel, pixel + 1, (unsigned)weight};
}

static unsigned short blend_rgb565(unsigned a, unsigned b, unsigned weight) {
    const unsigned lanes = 0x07e0f81f;
    a = (a | (a << 16)) & lanes;
    b = (b | (b << 16)) & lanes;
    unsigned color = ((a * (32 - weight) + b * weight + 0x02008010) >> 5) & lanes;
    return (unsigned short)(color | (color >> 16));
}

static const unsigned short *zoom_filter_row(int row, const unsigned short *source,
        const ZoomSample *horizontal, int width, unsigned short *filtered,
        int *cached_row) {
    if (row == *cached_row) return filtered;
    for (int x = 0; x < width; ++x) {
        ZoomSample sample = horizontal[x];
        filtered[x] = blend_rgb565(source[sample.pixel], source[sample.next], sample.weight);
    }
    *cached_row = row;
    return filtered;
}

__declspec(dllexport) void __cdecl TheocracyZoomWheel(int delta) {
    void *engine = *(void **)0x006c030c;
    if (!zoom_viewport || shared->title_active || !engine || has_modal(engine) ||
        zoom_province != *(void **)0x006a42a8)
        return;

    zoom_wheel_remainder += delta;
    int steps = zoom_wheel_remainder / WHEEL_DELTA;
    zoom_wheel_remainder %= WHEEL_DELTA;
    int distance = zoom_distance - steps * 5;
    zoom_distance = distance < 70 ? 70 : distance > 150 ? 150 : distance;
}

__declspec(dllexport) GamePoint *__cdecl TheocracyZoomPoint(void *viewport, GamePoint *point) {
    ((LocalPoint)0x0054f160)(viewport, point);
    if (viewport == zoom_viewport && zoom_width && zoom_height) {
        point->x = point->x * zoom_width / zoom_view_width + zoom_left;
        point->y = point->y * zoom_height / zoom_view_height + zoom_top;
    }
    return point;
}

__declspec(dllexport) void __cdecl TheocracyZoomDraw(
        void *province, ProvinceSurface *surface, const GamePoint *origin) {
    const WorldDraw draw = (WorldDraw)0x004d5240;
    unsigned char *state = province;
    void *viewport = *(void **)(state + 0x40daf);
    int mode = *(int *)(state + 0x40b17);
    zoom_viewport = NULL;
    zoom_width = 0;
    zoom_height = 0;
    if (province != zoom_province) {
        zoom_province = province;
        zoom_distance = 100;
        zoom_wheel_remainder = 0;
    }
    if (!viewport || !surface || surface->vtable != 0x005898e8 ||
        surface->format != 5 || !surface->pixels || mode != 0) {
        draw(province, surface, origin);
        return;
    }

    GameRect *rect = (GameRect *)((unsigned char *)viewport + 0x28);
    const GameRect original = *rect;
    enum { MAX_ZOOM_VIEW_WIDTH = 800 };
    ZoomSample horizontal[MAX_ZOOM_VIEW_WIDTH];
    if (original.left < 0 || original.top < 0 ||
        original.width <= 0 || original.height <= 0 ||
        original.width > MAX_ZOOM_VIEW_WIDTH ||
        original.width > surface->width - original.left ||
        original.height > surface->height - original.top) {
        draw(province, surface, origin);
        return;
    }
    zoom_viewport = viewport;
    if (zoom_distance == 100) {
        draw(province, surface, origin);
        return;
    }

    const int width = (original.width * zoom_distance + 50) / 100;
    const int height = (original.height * zoom_distance + 50) / 100;
    const int scratch_width = original.left + width;
    const int scratch_height = original.top + height;
    const SIZE_T count = (SIZE_T)scratch_width * scratch_height;
    if (count > zoom_capacity) {
        HANDLE heap = GetProcessHeap();
        unsigned short *pixels = zoom_pixels
            ? HeapReAlloc(heap, 0, zoom_pixels, count * sizeof(*pixels))
            : HeapAlloc(heap, 0, count * sizeof(*pixels));
        if (!pixels) {
            draw(province, surface, origin);
            return;
        }
        zoom_pixels = pixels;
        zoom_capacity = count;
    }
    memset(zoom_pixels, 0, count * sizeof(*zoom_pixels));

    const ProvinceSurface original_surface = *surface;
    GamePoint *camera = (GamePoint *)(state + 0x40da7);
    const GamePoint original_camera = *camera;
    zoom_left = (original.width - width) / 2;
    zoom_top = (original.height - height) / 2;
    rect->width = width;
    rect->height = height;
    camera->x += zoom_left;
    camera->y += zoom_top;
    surface->width = scratch_width;
    surface->height = scratch_height;
    surface->pixels = zoom_pixels;
    surface->pitch = scratch_width * sizeof(*zoom_pixels);
    surface->auxiliary_pitch = surface->pitch;
    draw(province, surface, origin);
    *surface = original_surface;
    *camera = original_camera;
    *rect = original;

    for (int x = 0; x < original.width; ++x)
        horizontal[x] = zoom_sample(x, width, original.width);
    unsigned short filtered[2][MAX_ZOOM_VIEW_WIDTH];
    int cached_rows[2] = {-1, -1};
    const unsigned short *sample_origin =
        zoom_pixels + original.top * scratch_width + original.left;
    for (int y = 0; y < original.height; ++y) {
        unsigned short *destination = (unsigned short *)(
            (unsigned char *)surface->pixels + (original.top + y) * surface->pitch)
            + original.left;
        ZoomSample sample_y = zoom_sample(y, height, original.height);
        int upper_slot = sample_y.pixel % 2;
        const unsigned short *upper = zoom_filter_row(sample_y.pixel,
            sample_origin + sample_y.pixel * scratch_width, horizontal, original.width,
            filtered[upper_slot], &cached_rows[upper_slot]);
        if (sample_y.weight == 0) {
            memcpy(destination, upper, original.width * sizeof(*upper));
            continue;
        }
        int lower_slot = sample_y.next % 2;
        const unsigned short *lower = zoom_filter_row(sample_y.next,
            sample_origin + sample_y.next * scratch_width, horizontal, original.width,
            filtered[lower_slot], &cached_rows[lower_slot]);
        for (int x = 0; x < original.width; ++x)
            destination[x] = blend_rgb565(upper[x], lower[x], sample_y.weight);
    }
    zoom_width = width;
    zoom_height = height;
    zoom_view_width = original.width;
    zoom_view_height = original.height;
}
