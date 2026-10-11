#include <common.h>
#include <CharacterRegistry.h>
#include <VehiclePartRegistry.h>
#include <VanillaRacerPatch.h>
#include <platform/native_assets.h>
#include <platform/native_mod_packages.h>
#include <platform/native_perf.h>
#include <platform/native_gltf.h>
#include <platform/native_obj.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(CTR_NATIVE)

#define NATIVE_MODS_BUTTON_W 64
#define NATIVE_MODS_BUTTON_H 20
#define NATIVE_MODS_BUTTON_X (512 - 6 - NATIVE_MODS_BUTTON_W)
#define NATIVE_MODS_BUTTON_Y (216 - 6 - NATIVE_MODS_BUTTON_H)
#define NATIVE_TEST_BUTTON_X 6
#define NATIVE_TEST_BUTTON_Y NATIVE_MODS_BUTTON_Y
/* TEMP: hide TEST visually for the 2026-09-30 video build only.
 * Keep launcher focus/navigation/input intact; restore to 1 next build. */
#define NATIVE_TEST_LAUNCHER_VISIBLE 0

#define NATIVE_MODS_PANEL_X 270
#define NATIVE_MODS_PANEL_Y 6
#define NATIVE_MODS_PANEL_W 236
#define NATIVE_MODS_PANEL_H 204
#define NATIVE_MODS_PANEL_CENTER_X (NATIVE_MODS_PANEL_X + NATIVE_MODS_PANEL_W / 2)
#define NATIVE_MODS_CONTENT_X (NATIVE_MODS_PANEL_X + 8)
#define NATIVE_MODS_CONTENT_W (NATIVE_MODS_PANEL_W - 16)
#define NATIVE_MODS_TITLE_Y 14
#define NATIVE_MODS_LIST_Y 40
#define NATIVE_MODS_FOOTER_Y 190
#define NATIVE_MODS_ROW_LINE_H 11
#define NATIVE_MODS_ROW_GAP 3
#define NATIVE_MODS_SLIDE 512
#define NATIVE_MODS_TRANSITION_FRAMES 12
#define NATIVE_MODS_CHARACTER_ROWS 5
#define NATIVE_MODS_INSTALLED_ROWS 5
#define NATIVE_MODS_WHEEL_ROWS 5
#define NATIVE_MODS_KART_ROWS 5
#define NATIVE_MODS_MAX_INSTALLED 128
#define NATIVE_MODS_MAX_WHEELS 32
#define NATIVE_MODS_MAX_KARTS 64

enum NativeModsView
{
    NATIVE_MODS_VIEW_ROOT = 0,
    NATIVE_MODS_VIEW_INSTALLED,
    NATIVE_MODS_VIEW_CHARACTERS,
    NATIVE_MODS_VIEW_KARTS,
    NATIVE_MODS_VIEW_WHEELS,
    NATIVE_MODS_VIEW_TRACKS,
    NATIVE_MODS_VIEW_GENERAL,
};

enum NativeModsState
{
    NATIVE_MODS_CLOSED = 0,
    NATIVE_MODS_OPENING,
    NATIVE_MODS_OPEN,
    NATIVE_MODS_SWITCHING,
    NATIVE_MODS_CLOSING,
};
static const char *s_nativeModsRootNames[] = {
    "INSTALLED MODS",
    "CHARACTERS",
    "KARTS",
    "WHEELS",
    "TRACKS",
    "GENERAL"
};

#define NATIVE_MODS_ROOT_COUNT ((int)(sizeof(s_nativeModsRootNames) / sizeof(s_nativeModsRootNames[0])))

struct NativeInstalledMod
{
    char slug[64];
    char displayName[64];
    char iniPath[1536];
    int enabled;
    int engineClass;
    int hasWheels;
};

struct NativeWheelMod
{
    char slug[64];
    char displayName[64];
    char assetName[64];
    char iniPath[1536];
    int enabled;
};

/* Installed kart packs are independent of the active vehicle-part registry.
   Disabled karts must remain visible so they can be re-enabled in Mods. */
struct NativeKartMod
{
    char slug[64];
    char displayName[64];
    char iniPath[1536];
    int enabled;
};

static struct NativeInstalledMod s_nativeInstalledMods[NATIVE_MODS_MAX_INSTALLED];
static int s_nativeInstalledModCount;
static int s_nativeInstalledRow;
static int s_nativeInstalledPage;

static struct NativeKartMod s_nativeKartMods[NATIVE_MODS_MAX_KARTS];
static int s_nativeKartModCount;
static int s_nativeKartRow;
static int s_nativeKartPage;

static struct NativeWheelMod s_nativeWheelMods[NATIVE_MODS_MAX_WHEELS];
static int s_nativeWheelModCount;
static int s_nativeWheelRow;
static int s_nativeWheelPage;
static int s_nativeWheelSelectionApplied;

static int s_nativeModsState;
static int s_nativeModsView;
static int s_nativeModsNextView;
static int s_nativeModsRow;
static int s_nativeModsCharacterRow;
static int s_nativeModsCharacterPage;
static int s_nativeModsReturnRow;
static int s_nativeModsLauncherSelected;
static int s_nativeTestLauncherSelected;
static int s_nativeModsLauncherReturnRow;
static u16 s_nativeModsLauncherSavedMenuBits;
static int s_nativeModsLauncherMenuLocked;
static u8 s_nativeModsTitleWasHidden[TITLE_INSTANCE_COUNT];
static int s_nativeModsTitleHidden;
static u16 s_nativeModsSavedState;
static int s_nativeModsTransitionFrame;
static int s_nativeModsTransitionDirection;

static struct TransitionMeta s_nativeModsTransitionMeta[2] = {
    {0, 0, 0, 0, 0},
    {0, 0, -1, 0, 0},
};

static int MM_Mods_PointInRect(int x, int y, const RECT *r)
{
    return (x >= r->x) && (x < r->x + r->w) &&
           (y >= r->y) && (y < r->y + r->h);
}

static void MM_Mods_CopyTextFit(char *dst, size_t dstSize,
                                const char *src, int fontType,
                                int maxWidth)
{
    size_t len;

    if (dst == NULL || dstSize == 0)
        return;

    snprintf(dst, dstSize, "%s", src ? src : "");
    if (DecalFont_GetLineWidth(dst, (s16)fontType) <= maxWidth)
        return;

    len = strlen(dst);
    while (len > 3)
    {
        dst[len - 3] = '.';
        dst[len - 2] = '.';
        dst[len - 1] = '.';
        dst[len] = 0;

        if (DecalFont_GetLineWidth(dst, (s16)fontType) <= maxWidth)
            return;

        len--;
        dst[len] = 0;
    }

    snprintf(dst, dstSize, "...");
}

static void MM_Mods_DrawCenteredFit(const char *text, int x, int y,
                                    int fontType, int flags,
                                    int maxWidth)
{
    char fitted[128];

    MM_Mods_CopyTextFit(fitted, sizeof(fitted), text,
                        fontType, maxWidth);
    DecalFont_DrawLine(fitted, x, y, (s16)fontType,
                       JUSTIFY_CENTER | flags);
}

static int MM_Mods_WrapText2(const char *text, int fontType, int maxWidth,
                             char *line1, size_t line1Size,
                             char *line2, size_t line2Size)
{
    char temp[128];
    int bestBreak = -1;
    int len;

    if (line1 == NULL || line1Size == 0 ||
        line2 == NULL || line2Size == 0)
        return 0;

    line1[0] = 0;
    line2[0] = 0;
    snprintf(temp, sizeof(temp), "%s", text ? text : "");

    if (DecalFont_GetLineWidth(temp, (s16)fontType) <= maxWidth)
    {
        snprintf(line1, line1Size, "%s", temp);
        return 1;
    }

    len = (int)strlen(temp);

    /* Prefer the last whole-word break that keeps line 1 inside the row. */
    for (int i = 1; i < len; i++)
    {
        char saved;

        if (temp[i] != ' ')
            continue;

        saved = temp[i];
        temp[i] = 0;
        if (DecalFont_GetLineWidth(temp, (s16)fontType) <= maxWidth)
            bestBreak = i;
        temp[i] = saved;
    }

    if (bestBreak < 1)
    {
        /* No useful spaces: split at the largest character boundary that
         * fits, then fit the remainder onto the second line. */
        for (int i = 1; i < len; i++)
        {
            char saved = temp[i];
            temp[i] = 0;
            if (DecalFont_GetLineWidth(temp, (s16)fontType) <= maxWidth)
                bestBreak = i;
            temp[i] = saved;
        }
    }

    if (bestBreak < 1)
    {
        MM_Mods_CopyTextFit(line1, line1Size, temp,
                            fontType, maxWidth);
        return 1;
    }

    {
        int split = bestBreak;
        char saved = temp[split];
        char *second;
        int firstLen;

        temp[split] = 0;
        firstLen = (int)strlen(temp);
        while (firstLen > 0 && temp[firstLen - 1] == ' ')
            temp[--firstLen] = 0;

        snprintf(line1, line1Size, "%s", temp);
        temp[split] = saved;

        second = (saved == ' ') ? &temp[split + 1] : &temp[split];
        while (*second == ' ')
            second++;

        MM_Mods_CopyTextFit(line2, line2Size, second,
                            fontType, maxWidth);
    }

    return line2[0] ? 2 : 1;
}

static int MM_Mods_RowHeight(int lineCount)
{
    if (lineCount < 1)
        lineCount = 1;

    return lineCount * NATIVE_MODS_ROW_LINE_H + 5;
}

static void MM_Mods_SetLauncherMenuLock(struct RectMenu *mainMenu,
                                           int locked)
{
    const u16 mask = HIDE_ROW_HIGHLIGHT;

    if (mainMenu == NULL)
        return;

    if (locked)
    {
        if (!s_nativeModsLauncherMenuLocked)
        {
            s_nativeModsLauncherSavedMenuBits =
                mainMenu->state & mask;
            s_nativeModsLauncherMenuLocked = 1;
        }

        mainMenu->state |= mask;
        return;
    }

    if (s_nativeModsLauncherMenuLocked)
    {
        mainMenu->state =
            (mainMenu->state & ~mask) |
            s_nativeModsLauncherSavedMenuBits;
        s_nativeModsLauncherMenuLocked = 0;
    }
}

static void MM_Mods_HideTitleInstances(void)
{
    if (D230.titleObj == NULL)
        return;

    for (int i = 0; i < TITLE_INSTANCE_COUNT; i++)
    {
        struct Instance *inst = D230.titleObj->i[i];

        if (!s_nativeModsTitleHidden)
        {
            s_nativeModsTitleWasHidden[i] =
                (inst != NULL && (inst->flags & HIDE_MODEL) != 0) ? 1 : 0;
        }

        /* MM_Title_ThTick clears HIDE_MODEL every title frame, so reapply
         * this while the Mods overlay is alive. The checkered flag is level
         * geometry, not one of these title instances, and remains visible. */
        if (inst != NULL)
            inst->flags |= HIDE_MODEL;
    }

    s_nativeModsTitleHidden = 1;
}

static void MM_Mods_RestoreTitleInstances(void)
{
    if (!s_nativeModsTitleHidden)
        return;

    if (D230.titleObj != NULL)
    {
        for (int i = 0; i < TITLE_INSTANCE_COUNT; i++)
        {
            struct Instance *inst = D230.titleObj->i[i];
            if (inst == NULL)
                continue;

            if (s_nativeModsTitleWasHidden[i])
                inst->flags |= HIDE_MODEL;
            else
                inst->flags &= ~HIDE_MODEL;
        }
    }

    s_nativeModsTitleHidden = 0;
}

static void MM_Mods_BeginTransition(int direction)
{
    s_nativeModsTransitionDirection = direction;
    s_nativeModsTransitionFrame = 0;
    s_nativeModsTransitionMeta[0].distX = (s16)(NATIVE_MODS_SLIDE * direction);
    s_nativeModsTransitionMeta[0].distY = 0;
    s_nativeModsTransitionMeta[0].headStart = 0;
    s_nativeModsTransitionMeta[0].currX = 0;
    s_nativeModsTransitionMeta[0].currY = 0;
}
static int MM_Mods_UpdateTransition(void)
{
    int ticks;

    MM_TransitionInOut(s_nativeModsTransitionMeta,
                       s_nativeModsTransitionFrame,
                       NATIVE_MODS_TRANSITION_FRAMES);

    ticks = Platform_GetLegacy30HzTicks();
    if (ticks > 0)
    {
        s_nativeModsTransitionFrame += ticks;
        if (s_nativeModsTransitionFrame >= NATIVE_MODS_TRANSITION_FRAMES)
        {
            s_nativeModsTransitionFrame = NATIVE_MODS_TRANSITION_FRAMES;
            MM_TransitionInOut(s_nativeModsTransitionMeta,
                               s_nativeModsTransitionFrame,
                               NATIVE_MODS_TRANSITION_FRAMES);
            return 1;
        }
    }

    return 0;
}

static int MM_Mods_DrawLauncher(int selected)
{
    struct GameTracker *gGT = sdata->gGT;
    RECT button = {NATIVE_MODS_BUTTON_X, NATIVE_MODS_BUTTON_Y,
                   NATIVE_MODS_BUTTON_W, NATIVE_MODS_BUTTON_H};
    RECT highlight = {(s16)(button.x + 2), (s16)(button.y + 2),
                      (s16)(button.w - 4), (s16)(button.h - 4)};
    int mouseX = -1;
    int mouseY = -1;
    int hover = RECTMENU_NativeTouchGet(&mouseX, &mouseY) &&
                MM_Mods_PointInRect(mouseX, mouseY, &button);

    DecalFont_DrawLine("MODS",
                       button.x + button.w / 2,
                       button.y + 5,
                       FONT_SMALL,
                       JUSTIFY_CENTER | WHITE);

    if (selected || hover)
        CTR_Box_DrawClearBox(&highlight, &sdata->menuRowHighlight_Normal,
                             TRANS_50_DECAL, gGT->backBuffer->otMem.uiOT);

    RECTMENU_DrawInnerRect(&button, 0, gGT->backBuffer->otMem.uiOT);

    return hover && RECTMENU_NativeTouchConsumeRect(&button);
}

static int MM_Test_DrawLauncher(int selected)
{
    struct GameTracker *gGT = sdata->gGT;
    RECT button = {NATIVE_TEST_BUTTON_X, NATIVE_TEST_BUTTON_Y,
                   NATIVE_MODS_BUTTON_W, NATIVE_MODS_BUTTON_H};
    RECT highlight = {(s16)(button.x + 2), (s16)(button.y + 2),
                      (s16)(button.w - 4), (s16)(button.h - 4)};
    int mouseX = -1;
    int mouseY = -1;
    int hover = RECTMENU_NativeTouchGet(&mouseX, &mouseY) &&
                MM_Mods_PointInRect(mouseX, mouseY, &button);

    if (NATIVE_TEST_LAUNCHER_VISIBLE)
    {
        DecalFont_DrawLine("TEST",
                           button.x + button.w / 2,
                           button.y + 5,
                           FONT_SMALL,
                           JUSTIFY_CENTER | WHITE);

        if (selected || hover)
            CTR_Box_DrawClearBox(&highlight, &sdata->menuRowHighlight_Normal,
                                 TRANS_50_DECAL, gGT->backBuffer->otMem.uiOT);

        RECTMENU_DrawInnerRect(&button, 0, gGT->backBuffer->otMem.uiOT);
    }

    return hover && RECTMENU_NativeTouchConsumeRect(&button);
}

static void MM_Test_Start(struct RectMenu *mainMenu)
{
    struct GameTracker *gGT = sdata->gGT;
    const int modCount = CharacterRegistry_GetModCount();

    if ((gGT == NULL) || (mainMenu == NULL))
        return;

    s_nativeModsLauncherSelected = 0;
    s_nativeTestLauncherSelected = 0;

    if (mainMenu->rowSelected < 0)
        mainMenu->rowSelected = (s16)s_nativeModsLauncherReturnRow;

    NativePerf_SetStressTestActive(1);
    gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
    D230.titleMenuState = TITLE_MENU_STATE_EXITING;
    D230.desiredMenuIndex = MM_EXIT_ROUTE_DEMO;

    Platform_Log("[PERF TEST] requested track=COCO_PARK bots=8 items=on custom_pool=%d\n",
                 modCount);
    Platform_LogFlush();
    RECTMENU_ClearInput();
}

static void MM_Mods_DrawPanelTitle(int offsetX, const char *title)
{
    MM_Mods_DrawCenteredFit(title,
                            NATIVE_MODS_PANEL_CENTER_X + offsetX,
                            NATIVE_MODS_TITLE_Y,
                            FONT_BIG, ORANGE,
                            NATIVE_MODS_CONTENT_W - 8);
}

static void MM_Mods_DrawPanelBackground(int offsetX)
{
    struct GameTracker *gGT = sdata->gGT;
    RECT panel = {
        (s16)(NATIVE_MODS_PANEL_X + offsetX),
        NATIVE_MODS_PANEL_Y,
        NATIVE_MODS_PANEL_W,
        NATIVE_MODS_PANEL_H
    };

    /* Called last so it is rendered first, behind all menu text. */
    RECTMENU_DrawInnerRect(&panel, 4, gGT->backBuffer->otMem.uiOT);
}
static void MM_Mods_DrawRoot(int offsetX, int interactive)
{
    struct GameTracker *gGT = sdata->gGT;
    u32 *ot = gGT->backBuffer->otMem.uiOT;
    int mouseX = -1;
    int mouseY = -1;
    int y = NATIVE_MODS_LIST_Y;

    MM_Mods_DrawPanelTitle(offsetX, "MODS");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    for (int i = 0; i < NATIVE_MODS_ROOT_COUNT; i++)
    {
        RECT rowRect = {
            (s16)(NATIVE_MODS_CONTENT_X + offsetX),
            (s16)(y - 3),
            NATIVE_MODS_CONTENT_W,
            18
        };

        if (interactive && MM_Mods_PointInRect(mouseX, mouseY, &rowRect))
            s_nativeModsRow = i;

        MM_Mods_DrawCenteredFit(s_nativeModsRootNames[i],
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                y,
                                FONT_SMALL, WHITE,
                                NATIVE_MODS_CONTENT_W - 12);

        if (s_nativeModsRow == i)
            CTR_Box_DrawClearBox(&rowRect, &sdata->menuRowHighlight_Normal,
                                 TRANS_50_DECAL, ot);

        if (interactive && (s_nativeModsRow == i) &&
            RECTMENU_NativeTouchConsumeRect(&rowRect))
        {
            s_nativeModsView = NATIVE_MODS_VIEW_INSTALLED + i;
            s_nativeModsRow = 0;
            RECTMENU_ClearInput();
            break;
        }

        y += 23;
    }

    MM_Mods_DrawCenteredFit("SELECT A CATEGORY",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX,
                            NATIVE_MODS_FOOTER_Y,
                            FONT_SMALL, ORANGE,
                            NATIVE_MODS_CONTENT_W - 8);

    MM_Mods_DrawPanelBackground(offsetX);
}
static char *MM_Mods_Trim(char *text)
{
    char *end;

    while (*text && isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = 0;
    return text;
}

static int MM_Mods_EqualsIgnoreCase(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }

    return (*a == 0) && (*b == 0);
}

static int MM_Mods_ParseBool(const char *value, int fallback)
{
    if (MM_Mods_EqualsIgnoreCase(value, "true") ||
        MM_Mods_EqualsIgnoreCase(value, "yes") ||
        !strcmp(value, "1") ||
        MM_Mods_EqualsIgnoreCase(value, "on"))
        return 1;

    if (MM_Mods_EqualsIgnoreCase(value, "false") ||
        MM_Mods_EqualsIgnoreCase(value, "no") ||
        !strcmp(value, "0") ||
        MM_Mods_EqualsIgnoreCase(value, "off"))
        return 0;

    return fallback;
}

static int MM_Mods_ParseEngine(const char *value, int fallback)
{
    if (MM_Mods_EqualsIgnoreCase(value, "turn")) return TURN;
    if (MM_Mods_EqualsIgnoreCase(value, "accel")) return ACCEL;
    if (MM_Mods_EqualsIgnoreCase(value, "speed")) return SPEED;
    if (MM_Mods_EqualsIgnoreCase(value, "balanced")) return BALANCED;
    return fallback;
}

static int MM_Mods_IsDirectory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int MM_Mods_InstalledCompare(const void *a, const void *b)
{
    const struct NativeInstalledMod *ma = (const struct NativeInstalledMod *)a;
    const struct NativeInstalledMod *mb = (const struct NativeInstalledMod *)b;
    const unsigned char *sa = (const unsigned char *)ma->displayName;
    const unsigned char *sb = (const unsigned char *)mb->displayName;

    while (*sa && *sb)
    {
        int ca = tolower(*sa);
        int cb = tolower(*sb);
        if (ca != cb) return ca - cb;
        sa++;
        sb++;
    }

    return (int)*sa - (int)*sb;
}

static int MM_Mods_KartCompare(const void *a, const void *b)
{
    const struct NativeKartMod *ma = (const struct NativeKartMod *)a;
    const struct NativeKartMod *mb = (const struct NativeKartMod *)b;
    const unsigned char *sa = (const unsigned char *)ma->displayName;
    const unsigned char *sb = (const unsigned char *)mb->displayName;
    while (*sa && *sb)
    {
        int ca = tolower(*sa), cb = tolower(*sb);
        if (ca != cb) return ca - cb;
        ++sa;
        ++sb;
    }
    return (int)*sa - (int)*sb;
}

/* Keep the same display-name comparer for kart and racer entries; both
   start with slug[64], displayName[64]. */
static void MM_Mods_ReadKartMod(const char *folderName, const char *iniPath)
{
    struct NativeKartMod *mod;
    FILE *file;
    char section[64] = "";
    char line[1024];

    if (s_nativeKartModCount >= NATIVE_MODS_MAX_KARTS)
        return;

    file = fopen(iniPath, "rb");
    if (file == NULL)
        return;

    mod = &s_nativeKartMods[s_nativeKartModCount];
    memset(mod, 0, sizeof(*mod));
    snprintf(mod->slug, sizeof(mod->slug), "%s", folderName);
    snprintf(mod->displayName, sizeof(mod->displayName), "%s", folderName);
    snprintf(mod->iniPath, sizeof(mod->iniPath), "%s", iniPath);
    mod->enabled = 1;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *input = MM_Mods_Trim(line);
        char *separator;
        char *key;
        char *value;

        if (!*input || *input == ';' || *input == '#')
            continue;

        if (*input == '[')
        {
            char *close = strchr(input, ']');
            if (close != NULL)
            {
                *close = 0;
                snprintf(section, sizeof(section), "%s",
                         MM_Mods_Trim(input + 1));
            }
            continue;
        }

        if (!MM_Mods_EqualsIgnoreCase(section, "kart"))
            continue;

        separator = strchr(input, '=');
        if (separator == NULL)
            continue;
        *separator = 0;
        key = MM_Mods_Trim(input);
        value = MM_Mods_Trim(separator + 1);

        if (MM_Mods_EqualsIgnoreCase(key, "name"))
            snprintf(mod->displayName, sizeof(mod->displayName), "%s", value);
        else if (MM_Mods_EqualsIgnoreCase(key, "enabled"))
            mod->enabled = MM_Mods_ParseBool(value, mod->enabled);
    }

    fclose(file);
    s_nativeKartModCount++;
}

static int MM_Mods_WheelCompare(const void *a, const void *b)
{
    const struct NativeWheelMod *ma = (const struct NativeWheelMod *)a;
    const struct NativeWheelMod *mb = (const struct NativeWheelMod *)b;
    const unsigned char *sa = (const unsigned char *)ma->displayName;
    const unsigned char *sb = (const unsigned char *)mb->displayName;

    while (*sa && *sb)
    {
        int ca = tolower(*sa);
        int cb = tolower(*sb);
        if (ca != cb) return ca - cb;
        sa++;
        sb++;
    }

    return (int)*sa - (int)*sb;
}

static void MM_Mods_ReadWheelMod(const char *folderName, const char *iniPath)
{
    struct NativeWheelMod *mod;
    FILE *file;
    char section[64] = "";
    char line[1024];

    if (s_nativeWheelModCount >= NATIVE_MODS_MAX_WHEELS)
        return;

    file = fopen(iniPath, "rb");
    if (file == NULL)
        return;

    mod = &s_nativeWheelMods[s_nativeWheelModCount];
    memset(mod, 0, sizeof(*mod));
    snprintf(mod->slug, sizeof(mod->slug), "%s", folderName);
    snprintf(mod->displayName, sizeof(mod->displayName), "%s", folderName);
    snprintf(mod->assetName, sizeof(mod->assetName), "%s", folderName);
    snprintf(mod->iniPath, sizeof(mod->iniPath), "%s", iniPath);

    while (fgets(line, sizeof(line), file))
    {
        char *text = MM_Mods_Trim(line);

        if (!*text || *text == ';' || *text == '#')
            continue;

        if (*text == '[')
        {
            char *close = strchr(text, ']');
            if (close != NULL)
            {
                *close = 0;
                snprintf(section, sizeof(section), "%s",
                         MM_Mods_Trim(text + 1));
            }
            continue;
        }

        if (MM_Mods_EqualsIgnoreCase(section, "wheel") ||
            MM_Mods_EqualsIgnoreCase(section, "wheels"))
        {
            char *equals = strchr(text, '=');
            if (equals != NULL)
            {
                char *key;
                char *value;

                *equals = 0;
                key = MM_Mods_Trim(text);
                value = MM_Mods_Trim(equals + 1);

                if (MM_Mods_EqualsIgnoreCase(key, "name"))
                    snprintf(mod->displayName, sizeof(mod->displayName),
                             "%s", value);
                else if (MM_Mods_EqualsIgnoreCase(key, "asset_name"))
                    snprintf(mod->assetName, sizeof(mod->assetName),
                             "%s", value);
                else if (MM_Mods_EqualsIgnoreCase(key, "enabled"))
                    mod->enabled =
                        MM_Mods_ParseBool(value, mod->enabled);
            }
        }
        else if (MM_Mods_EqualsIgnoreCase(section, "model"))
        {
            char *equals = strchr(text, '=');
            if (equals != NULL)
            {
                char *key;
                char *value;

                *equals = 0;
                key = MM_Mods_Trim(text);
                value = MM_Mods_Trim(equals + 1);

                /* Match VehiclePartRegistry: wheel packs normally keep
                 * asset_name under [model], not [wheel]. The Mods menu must
                 * use that same asset or it selects the folder slug and the
                 * renderer falls back even though the pack is enabled. */
                if (MM_Mods_EqualsIgnoreCase(key, "asset") ||
                    MM_Mods_EqualsIgnoreCase(key, "asset_name"))
                    snprintf(mod->assetName, sizeof(mod->assetName),
                             "%s", value);
            }
        }
    }

    fclose(file);

    if (mod->assetName[0] != 0)
        s_nativeWheelModCount++;
}

static void MM_Mods_ReadInstalledMod(const char *folderName, const char *iniPath)
{
    struct NativeInstalledMod *mod;
    FILE *file;
    char section[64] = "";
    char line[1024];

    if (s_nativeInstalledModCount >= NATIVE_MODS_MAX_INSTALLED)
        return;

    file = fopen(iniPath, "rb");
    if (file == NULL)
        return;

    mod = &s_nativeInstalledMods[s_nativeInstalledModCount];
    memset(mod, 0, sizeof(*mod));
    snprintf(mod->slug, sizeof(mod->slug), "%s", folderName);
    snprintf(mod->displayName, sizeof(mod->displayName), "%s", folderName);
    snprintf(mod->iniPath, sizeof(mod->iniPath), "%s", iniPath);
    mod->enabled = 1;
    mod->engineClass = BALANCED;
    mod->hasWheels = 0;

    while (fgets(line, sizeof(line), file))
    {
        char *text = MM_Mods_Trim(line);

        if (!*text || *text == ';' || *text == '#')
            continue;

        if (*text == '[')
        {
            char *close = strchr(text, ']');
            if (close != NULL)
            {
                *close = 0;
                snprintf(section, sizeof(section), "%s", MM_Mods_Trim(text + 1));
            }
            continue;
        }

        if (MM_Mods_EqualsIgnoreCase(section, "character"))
        {
            char *equals = strchr(text, '=');
            if (equals != NULL)
            {
                char *key;
                char *value;

                *equals = 0;
                key = MM_Mods_Trim(text);
                value = MM_Mods_Trim(equals + 1);

                if (MM_Mods_EqualsIgnoreCase(key, "name"))
                    snprintf(mod->displayName, sizeof(mod->displayName), "%s", value);
                else if (MM_Mods_EqualsIgnoreCase(key, "enabled"))
                    mod->enabled = MM_Mods_ParseBool(value, mod->enabled);
                else if (MM_Mods_EqualsIgnoreCase(key, "engine"))
                    mod->engineClass = MM_Mods_ParseEngine(value, mod->engineClass);
                else if (MM_Mods_EqualsIgnoreCase(key, "has_wheels"))
                    mod->hasWheels = MM_Mods_ParseBool(value, mod->hasWheels);
            }
        }
    }

    fclose(file);
    s_nativeInstalledModCount++;
}

static void MM_Mods_ScanInstalled(void)
{
    char modsDir[1024];
    DIR *dir;
    struct dirent *entry;

    s_nativeInstalledModCount = 0;
    s_nativeWheelModCount = 0;
    s_nativeKartModCount = 0;

    if (NativeAssets_BuildPath("mods/racers", modsDir, sizeof(modsDir)))
    {
        dir = opendir(modsDir);
        if (dir != NULL)
        {
            while ((entry = readdir(dir)) != NULL)
            {
                char folderPath[1200];
                char iniPath[1400];

                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                    continue;

                snprintf(folderPath, sizeof(folderPath), "%s/%s",
                         modsDir, entry->d_name);
                if (!MM_Mods_IsDirectory(folderPath))
                    continue;

                snprintf(iniPath, sizeof(iniPath), "%s/character.ini", folderPath);
                MM_Mods_ReadInstalledMod(entry->d_name, iniPath);

                if (s_nativeInstalledModCount >= NATIVE_MODS_MAX_INSTALLED)
                    break;
            }
            closedir(dir);
        }
    }

    /* Kart packs live in mods/karts, including disabled ones; listing
       them must not depend on any racers or wheels being installed. */
    if (NativeAssets_BuildPath("mods/karts", modsDir, sizeof(modsDir)))
    {
        dir = opendir(modsDir);
        if (dir != NULL)
        {
            while ((entry = readdir(dir)) != NULL)
            {
                char folderPath[1200];
                char iniPath[1400];

                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                    continue;
                snprintf(folderPath, sizeof(folderPath), "%s/%s",
                         modsDir, entry->d_name);
                if (!MM_Mods_IsDirectory(folderPath))
                    continue;

                snprintf(iniPath, sizeof(iniPath), "%s/kart.ini", folderPath);
                MM_Mods_ReadKartMod(entry->d_name, iniPath);

                if (s_nativeKartModCount >= NATIVE_MODS_MAX_KARTS)
                    break;
            }
            closedir(dir);
        }
    }

    /* Wheel packs live in their own vehicle-part namespace. Keep them out of
     * CharacterRegistry so they never appear as playable racers. */
    if (NativeAssets_BuildPath("mods/wheels", modsDir, sizeof(modsDir)))
    {
        dir = opendir(modsDir);
        if (dir != NULL)
        {
            while ((entry = readdir(dir)) != NULL)
            {
                char folderPath[1200];
                char iniPath[1400];

                if (!strcmp(entry->d_name, ".") ||
                    !strcmp(entry->d_name, ".."))
                    continue;

                snprintf(folderPath, sizeof(folderPath),
                         "%s/%s", modsDir, entry->d_name);
                if (!MM_Mods_IsDirectory(folderPath))
                    continue;

                snprintf(iniPath, sizeof(iniPath),
                         "%s/wheels.ini", folderPath);
                MM_Mods_ReadWheelMod(entry->d_name, iniPath);

                if (s_nativeWheelModCount >= NATIVE_MODS_MAX_WHEELS)
                    break;
            }

            closedir(dir);
        }
    }

    /* SM64CoopDX-style web-ID modules live in <game>/mods/<ID>.
       Scan that root separately from the original assets/mods/<category>
       folders, so INSTALLED MODS lists both generations without mixing
       their on-disk contents. Each ID/type is validated first. */
    if (NativeAssets_BuildPath("mods", modsDir, sizeof(modsDir)) &&
        (dir = opendir(modsDir)) != NULL)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            char folderPath[1200];
            char iniPath[1400];
            const char *slug = entry->d_name;
            if (!strcmp(slug,".") || !strcmp(slug,".."))
                continue;
            snprintf(folderPath,sizeof(folderPath),"%s/%s",modsDir,slug);
            if (!MM_Mods_IsDirectory(folderPath))
                continue;
            if (s_nativeInstalledModCount < NATIVE_MODS_MAX_INSTALLED &&
                NativeModPackage_IsValid(slug,"Character"))
            {
                snprintf(iniPath,sizeof(iniPath),"%s/character.ini",folderPath);
                MM_Mods_ReadInstalledMod(slug,iniPath);
            }
            if (s_nativeKartModCount < NATIVE_MODS_MAX_KARTS &&
                NativeModPackage_IsValid(slug,"Kart"))
            {
                snprintf(iniPath,sizeof(iniPath),"%s/kart.ini",folderPath);
                MM_Mods_ReadKartMod(slug,iniPath);
            }
            if (s_nativeWheelModCount < NATIVE_MODS_MAX_WHEELS &&
                NativeModPackage_IsValid(slug,"Wheels"))
            {
                snprintf(iniPath,sizeof(iniPath),"%s/wheels.ini",folderPath);
                MM_Mods_ReadWheelMod(slug,iniPath);
            }
        }
        closedir(dir);
    }

    if (s_nativeInstalledModCount > 1)
        qsort(s_nativeInstalledMods,
              (size_t)s_nativeInstalledModCount,
              sizeof(s_nativeInstalledMods[0]),
              MM_Mods_InstalledCompare);

    if (s_nativeKartModCount > 1)
        qsort(s_nativeKartMods,
              (size_t)s_nativeKartModCount,
              sizeof(s_nativeKartMods[0]),
              MM_Mods_KartCompare);

    if (s_nativeWheelModCount > 1)
        qsort(s_nativeWheelMods,
              (size_t)s_nativeWheelModCount,
              sizeof(s_nativeWheelMods[0]),
              MM_Mods_WheelCompare);

    if (s_nativeInstalledPage < 0) s_nativeInstalledPage = 0;
    if (s_nativeInstalledRow < 0) s_nativeInstalledRow = 0;
    if (s_nativeKartPage < 0) s_nativeKartPage = 0;
    if (s_nativeKartRow < 0) s_nativeKartRow = 0;
    if (s_nativeWheelPage < 0) s_nativeWheelPage = 0;
    if (s_nativeWheelRow < 0) s_nativeWheelRow = 0;
}

static int MM_Mods_WriteEnabled(const char *path, const char *sectionName, int enabled)
{
    FILE *file;
    char *input;
    char *output;
    long fileSizeLong;
    size_t fileSize;
    size_t inputPos = 0;
    size_t outputPos = 0;
    size_t outputCapacity;
    int inCharacter = 0;
    int sawCharacter = 0;
    int wroteEnabled = 0;
    const char *writeSection =
        MM_Mods_EqualsIgnoreCase(sectionName, "wheel") ? "wheels" : sectionName;

    file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    fileSizeLong = ftell(file);
    if (fileSizeLong < 0 || fileSizeLong > 1024 * 1024)
    {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    fileSize = (size_t)fileSizeLong;
    input = (char *)malloc(fileSize + 1);
    outputCapacity = fileSize + 512;
    output = (char *)malloc(outputCapacity);

    if (input == NULL || output == NULL)
    {
        free(input);
        free(output);
        fclose(file);
        return 0;
    }

    if (fileSize > 0 && fread(input, 1, fileSize, file) != fileSize)
    {
        free(input);
        free(output);
        fclose(file);
        return 0;
    }

    fclose(file);
    input[fileSize] = 0;

    while (inputPos < fileSize)
    {
        size_t lineStart = inputPos;
        size_t lineEnd;
        size_t newlineEnd;
        size_t contentLength;
        size_t copyLength;
        char parseLine[1024];
        char *text;
        int isSection = 0;

        while (inputPos < fileSize && input[inputPos] != '\r' && input[inputPos] != '\n')
            inputPos++;

        lineEnd = inputPos;

        if (inputPos < fileSize && input[inputPos] == '\r') inputPos++;
        if (inputPos < fileSize && input[inputPos] == '\n') inputPos++;
        newlineEnd = inputPos;

        contentLength = lineEnd - lineStart;
        copyLength = contentLength < sizeof(parseLine) - 1 ?
                     contentLength : sizeof(parseLine) - 1;
        memcpy(parseLine, input + lineStart, copyLength);
        parseLine[copyLength] = 0;
        text = MM_Mods_Trim(parseLine);

        if (*text == '[')
        {
            char *close = strchr(text, ']');
            if (close != NULL)
            {
                isSection = 1;

                if (inCharacter && !wroteEnabled)
                {
                    const char *insert = enabled ? "enabled = true\n" : "enabled = false\n";
                    size_t insertLength = strlen(insert);
                    memcpy(output + outputPos, insert, insertLength);
                    outputPos += insertLength;
                    wroteEnabled = 1;
                }

                *close = 0;
                {
                    const char *foundSection = MM_Mods_Trim(text + 1);
                    inCharacter =
                        MM_Mods_EqualsIgnoreCase(foundSection, sectionName) ||
                        (MM_Mods_EqualsIgnoreCase(sectionName, "wheel") &&
                         MM_Mods_EqualsIgnoreCase(foundSection, "wheels")) ||
                        (MM_Mods_EqualsIgnoreCase(sectionName, "wheels") &&
                         MM_Mods_EqualsIgnoreCase(foundSection, "wheel"));
                }
                if (inCharacter) sawCharacter = 1;
            }
        }

        if (inCharacter && !isSection)
        {
            char keyLine[1024];
            char *keyText;
            char *equals;

            snprintf(keyLine, sizeof(keyLine), "%s", text);
            keyText = MM_Mods_Trim(keyLine);
            equals = strchr(keyText, '=');

            if (equals != NULL)
            {
                *equals = 0;
                if (MM_Mods_EqualsIgnoreCase(MM_Mods_Trim(keyText), "enabled"))
                {
                    const char *replacement = enabled ? "enabled = true" : "enabled = false";
                    size_t replacementLength = strlen(replacement);
                    size_t newlineLength = newlineEnd - lineEnd;

                    memcpy(output + outputPos, replacement, replacementLength);
                    outputPos += replacementLength;
                    if (newlineLength > 0)
                    {
                        memcpy(output + outputPos, input + lineEnd, newlineLength);
                        outputPos += newlineLength;
                    }
                    wroteEnabled = 1;
                    continue;
                }
            }
        }

        copyLength = newlineEnd - lineStart;
        memcpy(output + outputPos, input + lineStart, copyLength);
        outputPos += copyLength;
    }

    if (sawCharacter && !wroteEnabled)
    {
        const char *insert = enabled ? "\nenabled = true\n" : "\nenabled = false\n";
        size_t insertLength = strlen(insert);
        memcpy(output + outputPos, insert, insertLength);
        outputPos += insertLength;
    }
    else if (!sawCharacter)
    {
        char insert[128];
        int insertLength = snprintf(
            insert,
            sizeof(insert),
            "\n[%s]\nenabled = %s\n",
            writeSection,
            enabled ? "true" : "false");

        if (insertLength <= 0 || (size_t)insertLength >= sizeof(insert))
        {
            free(input);
            free(output);
            return 0;
        }

        memcpy(output + outputPos, insert, (size_t)insertLength);
        outputPos += (size_t)insertLength;
    }

    file = fopen(path, "wb");
    if (file == NULL)
    {
        free(input);
        free(output);
        return 0;
    }

    if (outputPos > 0 && fwrite(output, 1, outputPos, file) != outputPos)
    {
        fclose(file);
        free(input);
        free(output);
        return 0;
    }

    fclose(file);
    free(input);
    free(output);
    return 1;
}

static void MM_Mods_ToggleInstalled(int absoluteIndex)
{
    struct NativeInstalledMod *mod;
    int nextEnabled;

    if (absoluteIndex < 0 || absoluteIndex >= s_nativeInstalledModCount)
        return;

    mod = &s_nativeInstalledMods[absoluteIndex];
    nextEnabled = !mod->enabled;

    if (!MM_Mods_WriteEnabled(mod->iniPath, "character", nextEnabled))
    {
        OtherFX_Play(2, 1);
        return;
    }

    MM_Mods_ScanInstalled();

    /* Character definitions and vanilla separation assets can change together.
       Drop patch metadata first so a newly added/fixed mask or body patch is
       re-read the next time the racer is drawn. */
    VanillaRacerPatch_Shutdown();
    NativeGltf_PrepareRegistryRefresh();
    CharacterRegistry_Shutdown();
    CharacterRegistry_Init();
    s_nativeModsCharacterPage = 0;
    s_nativeModsCharacterRow = 0;

    OtherFX_Play(1, 1);
}

static void MM_Mods_ApplyWheelSelection(void)
{
    int defaultWheel = -1;

    /* Wheel packs are availability toggles, not a mutually-exclusive radio
     * group. Keep every enabled wheels.ini registered so the loadout screen
     * can offer all of them at once. For racers left on INHERIT, retain the
     * first enabled pack as a harmless global/default fallback without
     * rewriting any other pack to OFF. */
    for (int i = 0; i < s_nativeWheelModCount; i++)
    {
        if (s_nativeWheelMods[i].enabled && defaultWheel < 0)
            defaultWheel = i;
    }

    VehiclePartRegistry_Shutdown();
    VehiclePartRegistry_Init();

    NativeObj_SetRetailWheelOverrideAsset(
        defaultWheel >= 0 ? s_nativeWheelMods[defaultWheel].assetName : "");
    s_nativeWheelSelectionApplied = 1;
}

static void MM_Mods_SelectKart(int absoluteIndex)
{
    struct NativeKartMod *mod;
    int nextEnabled;

    if (absoluteIndex < 0 || absoluteIndex >= s_nativeKartModCount)
        return;

    mod = &s_nativeKartMods[absoluteIndex];
    nextEnabled = !mod->enabled;

    if (!MM_Mods_WriteEnabled(mod->iniPath, "kart", nextEnabled))
    {
        OtherFX_Play(2, 1);
        return;
    }

    /* All three namespaces share VehiclePartRegistry. Refresh the roster
       so enabled kart packs appear in the race kart/color selector. */
    MM_Mods_ScanInstalled();
    MM_Mods_ApplyWheelSelection();

    Platform_Log("[MODS] kart pack %s (%s) - registered=%d\n",
                 nextEnabled ? "enabled" : "disabled",
                 mod->slug, VehiclePartRegistry_GetCount(VEHICLE_PART_KART));
    Platform_LogFlush();
    OtherFX_Play(1, 1);
}

static void MM_Mods_SelectWheel(int absoluteIndex)
{
    struct NativeWheelMod *mod;
    int nextEnabled;

    if (absoluteIndex < 0 || absoluteIndex >= s_nativeWheelModCount)
        return;

    mod = &s_nativeWheelMods[absoluteIndex];
    nextEnabled = !mod->enabled;

    /* Independent toggle: enabling one wheel pack must never disable another.
     * Multiple packs can coexist and are then all available in the per-racer
     * wheel picker. */
    if (!MM_Mods_WriteEnabled(mod->iniPath, "wheel", nextEnabled))
    {
        OtherFX_Play(2, 1);
        return;
    }

    mod->enabled = nextEnabled;
    MM_Mods_ApplyWheelSelection();

    Platform_Log("[MODS] wheel pack %s name=%s asset=%s\n",
                 nextEnabled ? "enabled" : "disabled",
                 mod->displayName,
                 mod->assetName);
    Platform_LogFlush();
    OtherFX_Play(1, 1);
}

static const char *MM_Mods_EngineName(int engine)
{
    switch (engine)
    {
        case TURN: return "TURN";
        case ACCEL: return "ACCEL";
        case SPEED: return "SPEED";
        case BALANCED: return "BALANCED";
        default: return "UNKNOWN";
    }
}

static int MM_Mods_DrawWrappedRow(int offsetX, int y,
                                  const char *name, const char *status,
                                  int selected, int interactive,
                                  int mouseX, int mouseY,
                                  int *hovered, int *clicked)
{
    struct GameTracker *gGT = sdata->gGT;
    u32 *ot = gGT->backBuffer->otMem.uiOT;
    char line1[96];
    char line2[96];
    int statusWidth = 0;
    int nameWidth;
    int lineCount;
    int rowHeight;
    int statusY;
    RECT rowRect;

    if (status != NULL && status[0] != 0)
        statusWidth = DecalFont_GetLineWidth((char *)status, FONT_SMALL);

    nameWidth = NATIVE_MODS_CONTENT_W - 18;
    if (statusWidth > 0)
        nameWidth -= statusWidth + 14;
    if (nameWidth < 72)
        nameWidth = 72;

    lineCount = MM_Mods_WrapText2(name, FONT_SMALL, nameWidth,
                                  line1, sizeof(line1),
                                  line2, sizeof(line2));
    rowHeight = MM_Mods_RowHeight(lineCount);

    rowRect.x = (s16)(NATIVE_MODS_CONTENT_X + offsetX);
    rowRect.y = (s16)(y - 3);
    rowRect.w = NATIVE_MODS_CONTENT_W;
    rowRect.h = (s16)rowHeight;

    *hovered = interactive &&
               MM_Mods_PointInRect(mouseX, mouseY, &rowRect);
    *clicked = interactive && *hovered &&
               RECTMENU_NativeTouchConsumeRect(&rowRect);

    DecalFont_DrawLine(line1,
                       NATIVE_MODS_CONTENT_X + 7 + offsetX, y,
                       FONT_SMALL, WHITE);

    if (lineCount > 1 && line2[0] != 0)
    {
        DecalFont_DrawLine(line2,
                           NATIVE_MODS_CONTENT_X + 7 + offsetX,
                           y + NATIVE_MODS_ROW_LINE_H,
                           FONT_SMALL, WHITE);
    }

    if (statusWidth > 0)
    {
        statusY = y +
            ((lineCount - 1) * NATIVE_MODS_ROW_LINE_H) / 2;
        DecalFont_DrawLine((char *)status,
                           NATIVE_MODS_CONTENT_X +
                           NATIVE_MODS_CONTENT_W - 7 + offsetX,
                           statusY,
                           FONT_SMALL, JUSTIFY_RIGHT |
                           ((status[1] == 'O' && status[2] == 'N') ?
                            WHITE : ORANGE));
    }

    if (selected || *hovered)
        CTR_Box_DrawClearBox(&rowRect, &sdata->menuRowHighlight_Normal,
                             TRANS_50_DECAL, ot);

    return rowHeight + NATIVE_MODS_ROW_GAP;
}

static void MM_Mods_DrawInstalled(int offsetX, int interactive)
{
    int pageCount = (s_nativeInstalledModCount + NATIVE_MODS_INSTALLED_ROWS - 1) /
                    NATIVE_MODS_INSTALLED_ROWS;
    int start;
    int visible;
    int mouseX = -1;
    int mouseY = -1;
    int y = NATIVE_MODS_LIST_Y;
    char footer[96];

    if (pageCount < 1) pageCount = 1;
    if (s_nativeInstalledPage >= pageCount) s_nativeInstalledPage = pageCount - 1;
    if (s_nativeInstalledPage < 0) s_nativeInstalledPage = 0;

    start = s_nativeInstalledPage * NATIVE_MODS_INSTALLED_ROWS;
    visible = s_nativeInstalledModCount - start;
    if (visible > NATIVE_MODS_INSTALLED_ROWS) visible = NATIVE_MODS_INSTALLED_ROWS;
    if (visible < 0) visible = 0;
    if (s_nativeInstalledRow >= visible)
        s_nativeInstalledRow = visible > 0 ? visible - 1 : 0;

    MM_Mods_DrawPanelTitle(offsetX, "INSTALLED MODS");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    if (visible == 0)
    {
        MM_Mods_DrawCenteredFit("NO RACER MODS INSTALLED",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                96,
                                FONT_SMALL, WHITE,
                                NATIVE_MODS_CONTENT_W - 10);
    }

    for (int i = 0; i < visible; i++)
    {
        int absoluteIndex = start + i;
        struct NativeInstalledMod *mod = &s_nativeInstalledMods[absoluteIndex];
        char status[8];
        int hovered = 0;
        int clicked = 0;
        int stride;

        snprintf(status, sizeof(status), "[%s]",
                 mod->enabled ? "ON" : "OFF");

        stride = MM_Mods_DrawWrappedRow(
            offsetX, y, mod->displayName, status,
            s_nativeInstalledRow == i, interactive,
            mouseX, mouseY, &hovered, &clicked);

        if (hovered)
            s_nativeInstalledRow = i;

        if (clicked)
        {
            MM_Mods_ToggleInstalled(absoluteIndex);
            RECTMENU_ClearInput();
            MM_Mods_DrawPanelBackground(offsetX);
            return;
        }

        y += stride;
    }

    if (s_nativeInstalledModCount > 0)
    {
        snprintf(footer, sizeof(footer), "CROSS: TOGGLE   PAGE %d/%d",
                 s_nativeInstalledPage + 1, pageCount);
        MM_Mods_DrawCenteredFit(footer,
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y,
                                FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
    }
    else
    {
        MM_Mods_DrawCenteredFit("ADD MODS TO ASSETS/MODS/RACERS",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y,
                                FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
    }

    MM_Mods_DrawPanelBackground(offsetX);
}
static void MM_Mods_DrawCharacters(int offsetX, int interactive)
{
    int modCount = s_nativeInstalledModCount;
    int pageCount = (modCount + NATIVE_MODS_CHARACTER_ROWS - 1) /
                    NATIVE_MODS_CHARACTER_ROWS;
    int start;
    int visible;
    int mouseX = -1;
    int mouseY = -1;
    int y = NATIVE_MODS_LIST_Y;
    char footer[96];

    if (pageCount < 1) pageCount = 1;
    if (s_nativeModsCharacterPage >= pageCount) s_nativeModsCharacterPage = pageCount - 1;
    if (s_nativeModsCharacterPage < 0) s_nativeModsCharacterPage = 0;

    start = s_nativeModsCharacterPage * NATIVE_MODS_CHARACTER_ROWS;
    visible = modCount - start;
    if (visible > NATIVE_MODS_CHARACTER_ROWS) visible = NATIVE_MODS_CHARACTER_ROWS;
    if (visible < 0) visible = 0;
    if (s_nativeModsCharacterRow >= visible)
        s_nativeModsCharacterRow = visible > 0 ? visible - 1 : 0;

    MM_Mods_DrawPanelTitle(offsetX, "CHARACTERS");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    if (visible == 0)
    {
        MM_Mods_DrawCenteredFit("NO CHARACTER MODS FOUND",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                92,
                                FONT_SMALL, WHITE,
                                NATIVE_MODS_CONTENT_W - 10);
        MM_Mods_DrawCenteredFit("ADD MODS TO ASSETS/MODS/RACERS",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y,
                                FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
        MM_Mods_DrawPanelBackground(offsetX);
        return;
    }

    for (int i = 0; i < visible; i++)
    {
        int absoluteIndex = start + i;
        struct NativeInstalledMod *mod = &s_nativeInstalledMods[absoluteIndex];
        char status[8];
        int hovered = 0;
        int clicked = 0;
        int stride;

        snprintf(status, sizeof(status), "[%s]",
                 mod->enabled ? "ON" : "OFF");

        stride = MM_Mods_DrawWrappedRow(
            offsetX, y, mod->displayName, status,
            s_nativeModsCharacterRow == i, interactive,
            mouseX, mouseY, &hovered, &clicked);

        if (hovered)
            s_nativeModsCharacterRow = i;

        if (clicked)
        {
            MM_Mods_ToggleInstalled(absoluteIndex);
            RECTMENU_ClearInput();
            MM_Mods_DrawPanelBackground(offsetX);
            return;
        }

        y += stride;
    }

    {
        struct NativeInstalledMod *selected =
            &s_nativeInstalledMods[start + s_nativeModsCharacterRow];

        snprintf(footer, sizeof(footer), "%s | W:%s | PAGE %d/%d",
                 MM_Mods_EngineName(selected->engineClass),
                 selected->hasWheels ? "YES" : "NO",
                 s_nativeModsCharacterPage + 1,
                 pageCount);
        MM_Mods_DrawCenteredFit(footer,
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y,
                                FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
    }

    MM_Mods_DrawPanelBackground(offsetX);
}

static void MM_Mods_DrawKarts(int offsetX, int interactive)
{
    int pageCount =
        (s_nativeKartModCount + NATIVE_MODS_KART_ROWS - 1) /
        NATIVE_MODS_KART_ROWS;
    int start;
    int visible;
    int mouseX = -1;
    int mouseY = -1;
    int y = NATIVE_MODS_LIST_Y;
    char footer[96];

    if (pageCount < 1) pageCount = 1;
    if (s_nativeKartPage >= pageCount) s_nativeKartPage = pageCount - 1;
    if (s_nativeKartPage < 0) s_nativeKartPage = 0;

    start = s_nativeKartPage * NATIVE_MODS_KART_ROWS;
    visible = s_nativeKartModCount - start;
    if (visible > NATIVE_MODS_KART_ROWS) visible = NATIVE_MODS_KART_ROWS;
    if (visible < 0) visible = 0;
    if (s_nativeKartRow >= visible)
        s_nativeKartRow = visible > 0 ? visible - 1 : 0;

    MM_Mods_DrawPanelTitle(offsetX, "KARTS");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    if (visible == 0)
    {
        MM_Mods_DrawCenteredFit("NO KART MODS FOUND",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                92, FONT_SMALL, WHITE,
                                NATIVE_MODS_CONTENT_W - 10);
        MM_Mods_DrawCenteredFit("ADD KART.INI TO ASSETS/MODS/KARTS",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y, FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
        MM_Mods_DrawPanelBackground(offsetX);
        return;
    }

    for (int i = 0; i < visible; i++)
    {
        int absoluteIndex = start + i;
        struct NativeKartMod *mod = &s_nativeKartMods[absoluteIndex];
        int hovered = 0, clicked = 0;
        int stride = MM_Mods_DrawWrappedRow(
            offsetX, y, mod->displayName,
            mod->enabled ? "[ON]" : "[OFF]",
            s_nativeKartRow == i, interactive,
            mouseX, mouseY, &hovered, &clicked);

        if (hovered)
            s_nativeKartRow = i;
        if (clicked)
        {
            MM_Mods_SelectKart(absoluteIndex);
            RECTMENU_ClearInput();
            MM_Mods_DrawPanelBackground(offsetX);
            return;
        }
        y += stride;
    }

    {
        const struct NativeKartMod *selected =
            &s_nativeKartMods[start + s_nativeKartRow];
        int id = selected->enabled ?
            VehiclePartRegistry_GetIDBySlug(
                VEHICLE_PART_KART, selected->slug) : -1;
        int colors = id >= 0 ?
            VehiclePartRegistry_GetColorCount(id) : 0;

        if (colors > 1)
            snprintf(footer, sizeof(footer), "%d COLORS | PAGE %d/%d",
                     colors, s_nativeKartPage + 1, pageCount);
        else
            snprintf(footer, sizeof(footer), "CROSS: TOGGLE | PAGE %d/%d",
                     s_nativeKartPage + 1, pageCount);
        MM_Mods_DrawCenteredFit(
            footer, NATIVE_MODS_PANEL_CENTER_X + offsetX,
            NATIVE_MODS_FOOTER_Y, FONT_SMALL, ORANGE,
            NATIVE_MODS_CONTENT_W - 8);
    }

    MM_Mods_DrawPanelBackground(offsetX);
}

static void MM_Mods_DrawWheels(int offsetX, int interactive)
{
    int pageCount =
        (s_nativeWheelModCount + NATIVE_MODS_WHEEL_ROWS - 1) /
        NATIVE_MODS_WHEEL_ROWS;
    int start;
    int visible;
    int mouseX = -1;
    int mouseY = -1;
    int y = NATIVE_MODS_LIST_Y;
    char footer[96];

    if (pageCount < 1) pageCount = 1;
    if (s_nativeWheelPage >= pageCount) s_nativeWheelPage = pageCount - 1;
    if (s_nativeWheelPage < 0) s_nativeWheelPage = 0;

    start = s_nativeWheelPage * NATIVE_MODS_WHEEL_ROWS;
    visible = s_nativeWheelModCount - start;
    if (visible > NATIVE_MODS_WHEEL_ROWS) visible = NATIVE_MODS_WHEEL_ROWS;
    if (visible < 0) visible = 0;
    if (s_nativeWheelRow >= visible)
        s_nativeWheelRow = visible > 0 ? visible - 1 : 0;

    MM_Mods_DrawPanelTitle(offsetX, "WHEELS");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    if (visible == 0)
    {
        MM_Mods_DrawCenteredFit("NO WHEEL MODS FOUND",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                92,
                                FONT_SMALL, WHITE,
                                NATIVE_MODS_CONTENT_W - 10);
        MM_Mods_DrawCenteredFit("ADD A WHEELS.INI PACK",
                                NATIVE_MODS_PANEL_CENTER_X + offsetX,
                                NATIVE_MODS_FOOTER_Y,
                                FONT_SMALL, ORANGE,
                                NATIVE_MODS_CONTENT_W - 8);
        MM_Mods_DrawPanelBackground(offsetX);
        return;
    }

    for (int i = 0; i < visible; i++)
    {
        int absoluteIndex = start + i;
        struct NativeWheelMod *mod = &s_nativeWheelMods[absoluteIndex];
        int hovered = 0;
        int clicked = 0;
        int stride;

        stride = MM_Mods_DrawWrappedRow(
            offsetX, y, mod->displayName,
            mod->enabled ? "[ON]" : "[OFF]",
            s_nativeWheelRow == i, interactive,
            mouseX, mouseY, &hovered, &clicked);

        if (hovered)
            s_nativeWheelRow = i;

        if (clicked)
        {
            MM_Mods_SelectWheel(absoluteIndex);
            RECTMENU_ClearInput();
            MM_Mods_DrawPanelBackground(offsetX);
            return;
        }

        y += stride;
    }

    snprintf(footer, sizeof(footer),
             "CROSS: TOGGLE   PAGE %d/%d",
             s_nativeWheelPage + 1, pageCount);
    MM_Mods_DrawCenteredFit(footer,
                            NATIVE_MODS_PANEL_CENTER_X + offsetX,
                            NATIVE_MODS_FOOTER_Y,
                            FONT_SMALL, ORANGE,
                            NATIVE_MODS_CONTENT_W - 8);

    MM_Mods_DrawPanelBackground(offsetX);
}

static void MM_Mods_DrawPending(int offsetX, const char *title, const char *kind)
{
    char line[96];

    MM_Mods_DrawPanelTitle(offsetX, title);
    snprintf(line, sizeof(line), "%s MOD LOADER", kind);
    MM_Mods_DrawCenteredFit(line,
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 76,
                            FONT_SMALL, WHITE,
                            NATIVE_MODS_CONTENT_W - 12);
    MM_Mods_DrawCenteredFit("NOT CONNECTED YET",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 104,
                            FONT_SMALL, ORANGE,
                            NATIVE_MODS_CONTENT_W - 12);
    MM_Mods_DrawCenteredFit("THE MENU HOOK IS READY",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 132,
                            FONT_SMALL, WHITE,
                            NATIVE_MODS_CONTENT_W - 12);
    MM_Mods_DrawPanelBackground(offsetX);
}

static void MM_Mods_RefreshCharacters(void)
{
    MM_Mods_ScanInstalled();
    MM_Mods_ApplyWheelSelection();
    VanillaRacerPatch_Shutdown();
    NativeGltf_PrepareRegistryRefresh();
    CharacterRegistry_Shutdown();
    CharacterRegistry_Init();
    s_nativeModsCharacterPage = 0;
    s_nativeModsCharacterRow = 0;
}

static void MM_Mods_DrawGeneral(int offsetX, int interactive)
{
    struct GameTracker *gGT = sdata->gGT;
    u32 *ot = gGT->backBuffer->otMem.uiOT;
    int mouseX = -1;
    int mouseY = -1;
    RECT refreshRect = {
        (s16)(NATIVE_MODS_CONTENT_X + offsetX),
        43,
        NATIVE_MODS_CONTENT_W,
        21
    };

    MM_Mods_DrawPanelTitle(offsetX, "GENERAL");
    RECTMENU_NativeTouchGet(&mouseX, &mouseY);

    if (interactive && MM_Mods_PointInRect(mouseX, mouseY, &refreshRect))
        s_nativeModsRow = 0;

    MM_Mods_DrawCenteredFit("REFRESH MODS / OFFSETS",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 48,
                            FONT_SMALL, WHITE,
                            NATIVE_MODS_CONTENT_W - 12);

    if (s_nativeModsRow == 0)
        CTR_Box_DrawClearBox(&refreshRect, &sdata->menuRowHighlight_Normal,
                             TRANS_50_DECAL, ot);

    MM_Mods_DrawCenteredFit("RELOADS CHARACTER / KART / WHEEL INI",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 88,
                            FONT_SMALL, ORANGE,
                            NATIVE_MODS_CONTENT_W - 12);
    MM_Mods_DrawCenteredFit("OFFSETS APPLY WITHOUT RESTART",
                            NATIVE_MODS_PANEL_CENTER_X + offsetX, 112,
                            FONT_SMALL, WHITE,
                            NATIVE_MODS_CONTENT_W - 12);

    if (interactive && RECTMENU_NativeTouchConsumeRect(&refreshRect))
    {
        MM_Mods_RefreshCharacters();
        OtherFX_Play(1, 1);
        RECTMENU_ClearInput();
    }

    MM_Mods_DrawPanelBackground(offsetX);
}

static void MM_Mods_DrawView(int view, int offsetX, int interactive)
{
    switch (view)
    {
        case NATIVE_MODS_VIEW_ROOT:
            MM_Mods_DrawRoot(offsetX, interactive);
            break;
        case NATIVE_MODS_VIEW_INSTALLED:
            MM_Mods_DrawInstalled(offsetX, interactive);
            break;
        case NATIVE_MODS_VIEW_CHARACTERS:
            MM_Mods_DrawCharacters(offsetX, interactive);
            break;
        case NATIVE_MODS_VIEW_KARTS:
            MM_Mods_DrawKarts(offsetX, interactive);
            break;
        case NATIVE_MODS_VIEW_WHEELS:
            MM_Mods_DrawWheels(offsetX, interactive);
            break;
        case NATIVE_MODS_VIEW_TRACKS:
            MM_Mods_DrawPending(offsetX, "TRACKS", "TRACK");
            break;
        case NATIVE_MODS_VIEW_GENERAL:
            MM_Mods_DrawGeneral(offsetX, interactive);
            break;
        default:
            MM_Mods_DrawRoot(offsetX, interactive);
            break;
    }
}
static void MM_Mods_Open(struct RectMenu *mainMenu)
{
    MM_Mods_ScanInstalled();
    MM_Mods_ApplyWheelSelection();
    CharacterRegistry_Init();

    s_nativeInstalledRow = 0;
    s_nativeInstalledPage = 0;
    s_nativeKartRow = 0;
    s_nativeKartPage = 0;
    s_nativeModsState = NATIVE_MODS_OPENING;
    s_nativeModsView = NATIVE_MODS_VIEW_ROOT;
    s_nativeModsNextView = NATIVE_MODS_VIEW_ROOT;
    s_nativeModsRow = 0;
    s_nativeModsCharacterRow = 0;
    s_nativeModsCharacterPage = 0;
    s_nativeModsReturnRow = s_nativeModsLauncherSelected ?
                            s_nativeModsLauncherReturnRow :
                            mainMenu->rowSelected;
    s_nativeModsLauncherSelected = 0;
    s_nativeModsSavedState = mainMenu->state;

    MM_Mods_HideTitleInstances();
    mainMenu->state |= ONLY_DRAW_TITLE | INVISIBLE;
    sdata->gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
    MM_Mods_BeginTransition(-1);
    RECTMENU_ClearInput();
}

static void MM_Mods_FinishClose(struct RectMenu *mainMenu)
{
    s_nativeModsState = NATIVE_MODS_CLOSED;
    MM_Mods_RestoreTitleInstances();
    mainMenu->state = s_nativeModsSavedState;
    mainMenu->rowSelected = (s16)s_nativeModsReturnRow;
    s_nativeModsLauncherSelected = 0;
    sdata->gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
    RECTMENU_ClearInput();
}

static void MM_Mods_GoBack(void)
{
    if (s_nativeModsView == NATIVE_MODS_VIEW_ROOT)
    {
        /* Opening and closing keep the CTR slide. Internal category changes
         * are immediate so the Mods browser feels like one persistent list. */
        s_nativeModsState = NATIVE_MODS_CLOSING;
        MM_Mods_BeginTransition(1);
    }
    else
    {
        s_nativeModsView = NATIVE_MODS_VIEW_ROOT;
        s_nativeModsRow = 0;
    }

    RECTMENU_ClearInput();
}
static void MM_Mods_ActivateRootRow(void)
{
    s_nativeModsView = NATIVE_MODS_VIEW_INSTALLED + s_nativeModsRow;
    s_nativeModsRow = 0;
    RECTMENU_ClearInput();
}

static void MM_Mods_HandleInstalledInput(u32 tap)
{
    int pageCount = (s_nativeInstalledModCount + NATIVE_MODS_INSTALLED_ROWS - 1) /
                    NATIVE_MODS_INSTALLED_ROWS;
    int start;
    int visible;

    if (pageCount < 1) pageCount = 1;
    if (s_nativeInstalledPage >= pageCount) s_nativeInstalledPage = pageCount - 1;
    if (s_nativeInstalledPage < 0) s_nativeInstalledPage = 0;

    start = s_nativeInstalledPage * NATIVE_MODS_INSTALLED_ROWS;
    visible = s_nativeInstalledModCount - start;
    if (visible > NATIVE_MODS_INSTALLED_ROWS) visible = NATIVE_MODS_INSTALLED_ROWS;
    if (visible < 0) visible = 0;

    if ((tap & BTN_UP) && visible > 0)
    {
        if (--s_nativeInstalledRow < 0) s_nativeInstalledRow = visible - 1;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_DOWN) && visible > 0)
    {
        if (++s_nativeInstalledRow >= visible) s_nativeInstalledRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & BTN_LEFT) && s_nativeInstalledPage > 0)
    {
        s_nativeInstalledPage--;
        s_nativeInstalledRow = 0;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_RIGHT) && s_nativeInstalledPage + 1 < pageCount)
    {
        s_nativeInstalledPage++;
        s_nativeInstalledRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) && visible > 0)
        MM_Mods_ToggleInstalled(start + s_nativeInstalledRow);
}

static void MM_Mods_HandleCharactersInput(u32 tap)
{
    int modCount = s_nativeInstalledModCount;
    int pageCount = (modCount + NATIVE_MODS_CHARACTER_ROWS - 1) / NATIVE_MODS_CHARACTER_ROWS;
    int start;
    int visible;

    if (pageCount < 1) pageCount = 1;
    if (s_nativeModsCharacterPage >= pageCount) s_nativeModsCharacterPage = pageCount - 1;
    if (s_nativeModsCharacterPage < 0) s_nativeModsCharacterPage = 0;

    start = s_nativeModsCharacterPage * NATIVE_MODS_CHARACTER_ROWS;
    visible = modCount - start;
    if (visible > NATIVE_MODS_CHARACTER_ROWS) visible = NATIVE_MODS_CHARACTER_ROWS;
    if (visible < 0) visible = 0;

    if ((tap & BTN_UP) && visible > 0)
    {
        if (--s_nativeModsCharacterRow < 0) s_nativeModsCharacterRow = visible - 1;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_DOWN) && visible > 0)
    {
        if (++s_nativeModsCharacterRow >= visible) s_nativeModsCharacterRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & BTN_LEFT) && s_nativeModsCharacterPage > 0)
    {
        s_nativeModsCharacterPage--;
        s_nativeModsCharacterRow = 0;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_RIGHT) && s_nativeModsCharacterPage + 1 < pageCount)
    {
        s_nativeModsCharacterPage++;
        s_nativeModsCharacterRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) && visible > 0)
        MM_Mods_ToggleInstalled(start + s_nativeModsCharacterRow);
}
static void MM_Mods_HandleKartsInput(u32 tap)
{
    int pageCount =
        (s_nativeKartModCount + NATIVE_MODS_KART_ROWS - 1) /
        NATIVE_MODS_KART_ROWS;
    int start, visible;

    if (pageCount < 1) pageCount = 1;
    if (s_nativeKartPage >= pageCount) s_nativeKartPage = pageCount - 1;
    if (s_nativeKartPage < 0) s_nativeKartPage = 0;

    start = s_nativeKartPage * NATIVE_MODS_KART_ROWS;
    visible = s_nativeKartModCount - start;
    if (visible > NATIVE_MODS_KART_ROWS) visible = NATIVE_MODS_KART_ROWS;
    if (visible < 0) visible = 0;

    if ((tap & BTN_UP) && visible > 0)
    {
        if (--s_nativeKartRow < 0) s_nativeKartRow = visible - 1;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_DOWN) && visible > 0)
    {
        if (++s_nativeKartRow >= visible) s_nativeKartRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & BTN_LEFT) && s_nativeKartPage > 0)
    {
        --s_nativeKartPage;
        s_nativeKartRow = 0;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_RIGHT) && s_nativeKartPage + 1 < pageCount)
    {
        ++s_nativeKartPage;
        s_nativeKartRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) && visible > 0)
        MM_Mods_SelectKart(start + s_nativeKartRow);
}

static void MM_Mods_HandleWheelsInput(u32 tap)
{
    int pageCount =
        (s_nativeWheelModCount + NATIVE_MODS_WHEEL_ROWS - 1) /
        NATIVE_MODS_WHEEL_ROWS;
    int start;
    int visible;

    if (pageCount < 1) pageCount = 1;
    if (s_nativeWheelPage >= pageCount) s_nativeWheelPage = pageCount - 1;
    if (s_nativeWheelPage < 0) s_nativeWheelPage = 0;

    start = s_nativeWheelPage * NATIVE_MODS_WHEEL_ROWS;
    visible = s_nativeWheelModCount - start;
    if (visible > NATIVE_MODS_WHEEL_ROWS) visible = NATIVE_MODS_WHEEL_ROWS;
    if (visible < 0) visible = 0;

    if ((tap & BTN_UP) && visible > 0)
    {
        if (--s_nativeWheelRow < 0) s_nativeWheelRow = visible - 1;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_DOWN) && visible > 0)
    {
        if (++s_nativeWheelRow >= visible) s_nativeWheelRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & BTN_LEFT) && s_nativeWheelPage > 0)
    {
        s_nativeWheelPage--;
        s_nativeWheelRow = 0;
        OtherFX_Play(0, 1);
    }
    else if ((tap & BTN_RIGHT) &&
             s_nativeWheelPage + 1 < pageCount)
    {
        s_nativeWheelPage++;
        s_nativeWheelRow = 0;
        OtherFX_Play(0, 1);
    }

    if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) && visible > 0)
        MM_Mods_SelectWheel(start + s_nativeWheelRow);
}

static void MM_Mods_HandleOpenInput(void)
{
    u32 tap = sdata->AnyPlayerTap;

    if (tap & (BTN_TRIANGLE | BTN_SQUARE_one))
    {
        MM_Mods_GoBack();
        return;
    }

    if (s_nativeModsView == NATIVE_MODS_VIEW_ROOT)
    {
        if (tap & BTN_UP)
        {
            if (--s_nativeModsRow < 0) s_nativeModsRow = NATIVE_MODS_ROOT_COUNT - 1;
            OtherFX_Play(0, 1);
        }
        else if (tap & BTN_DOWN)
        {
            if (++s_nativeModsRow >= NATIVE_MODS_ROOT_COUNT) s_nativeModsRow = 0;
            OtherFX_Play(0, 1);
        }

        if (tap & (BTN_CIRCLE | BTN_CROSS_one))
        {
            OtherFX_Play(1, 1);
            MM_Mods_ActivateRootRow();
        }
    }
    else if (s_nativeModsView == NATIVE_MODS_VIEW_INSTALLED)
    {
        MM_Mods_HandleInstalledInput(tap);
    }
    else if (s_nativeModsView == NATIVE_MODS_VIEW_CHARACTERS)
    {
        MM_Mods_HandleCharactersInput(tap);
    }
    else if (s_nativeModsView == NATIVE_MODS_VIEW_KARTS)
    {
        MM_Mods_HandleKartsInput(tap);
    }
    else if (s_nativeModsView == NATIVE_MODS_VIEW_WHEELS)
    {
        MM_Mods_HandleWheelsInput(tap);
    }
    else if (s_nativeModsView == NATIVE_MODS_VIEW_GENERAL)
    {
        if (tap & (BTN_CIRCLE | BTN_CROSS_one))
        {
            MM_Mods_RefreshCharacters();
            OtherFX_Play(1, 1);
        }
    }
}
int MM_Mods_UpdateMainMenu(struct RectMenu *mainMenu)
{
    struct GameTracker *gGT = sdata->gGT;

#if defined(CTR_INTERNAL)
    /* If TEST was manually aborted after measurement began, reaching the
     * title menu must end its ownership immediately. Otherwise the spectator
     * camera/PVS code would remain armed and could leak into a later retail
     * demo or normal race. Natural TEST completion already clears this. */
    if (NativePerf_IsStressTestActive() && NativePerf_IsStressTestMeasuring())
    {
        Platform_Log("[PERF TEST] returned to title menu; clearing stress state\n");
        NativePerf_SetStressTestActive(0);
    }
#endif

    if (s_nativeModsState != NATIVE_MODS_CLOSED)
    {
        int done;
        int outgoingX;
        int incomingX;

        gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;

        /* The retail title thread unhides Crash/logo/trophy every frame.
         * Keep those instances suppressed while the Mods overlay is alive;
         * the checkered flag remains because it is level geometry. */
        MM_Mods_HideTitleInstances();

        if (mainMenu->funcState != RECTMENU_FUNC_STATE_UPDATE)
        {
            RECTMENU_ClearInput();
            return 1;
        }

        if (s_nativeModsState == NATIVE_MODS_OPEN)
        {
            MM_Mods_DrawView(s_nativeModsView, 0, 1);
            MM_Mods_HandleOpenInput();
            RECTMENU_ClearInput();
            return 1;
        }

        done = MM_Mods_UpdateTransition();
        outgoingX = s_nativeModsTransitionMeta[0].currX;

        if (s_nativeModsState == NATIVE_MODS_OPENING)
        {
            incomingX = NATIVE_MODS_SLIDE + outgoingX;
            MM_Mods_DrawView(NATIVE_MODS_VIEW_ROOT, incomingX, 0);

            if (done)
            {
                s_nativeModsView = NATIVE_MODS_VIEW_ROOT;
                s_nativeModsState = NATIVE_MODS_OPEN;
            }
        }
        else if (s_nativeModsState == NATIVE_MODS_SWITCHING)
        {
            /* Legacy safety path: category switching is immediate now. */
            s_nativeModsView = s_nativeModsNextView;
            s_nativeModsState = NATIVE_MODS_OPEN;
            s_nativeModsRow = 0;
            MM_Mods_DrawView(s_nativeModsView, 0, 0);
        }
        else if (s_nativeModsState == NATIVE_MODS_CLOSING)
        {
            MM_Mods_DrawView(NATIVE_MODS_VIEW_ROOT, outgoingX, 0);

            if (done)
                MM_Mods_FinishClose(mainMenu);
        }

        RECTMENU_ClearInput();
        return 1;
    }

    {
        int launcherAvailable =
            (mainMenu->funcState == RECTMENU_FUNC_STATE_UPDATE) &&
            (D230.titleMenuState == TITLE_MENU_STATE_IN_MENU) &&
            ((mainMenu->state & DRAW_NEXT_MENU_IN_HIERARCHY) == 0) &&
            ((mainMenu->state & INVISIBLE) == 0);

        if (!launcherAvailable)
        {
            if (s_nativeModsLauncherSelected || s_nativeTestLauncherSelected)
            {
                MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                if (mainMenu->rowSelected < 0)
                    mainMenu->rowSelected = (s16)s_nativeModsLauncherReturnRow;
                s_nativeModsLauncherSelected = 0;
                s_nativeTestLauncherSelected = 0;
            }
            return 0;
        }

        if (s_nativeModsLauncherSelected || s_nativeTestLauncherSelected)
            MM_Mods_SetLauncherMenuLock(mainMenu, 1);
        else
            MM_Mods_SetLauncherMenuLock(mainMenu, 0);

        if (!s_nativeWheelSelectionApplied)
        {
            MM_Mods_ScanInstalled();
            MM_Mods_ApplyWheelSelection();
        }

#if defined(CTR_INTERNAL)
        /*
         * One-shot unattended benchmark hook. Creating debug/perf.autorun
         * before launch starts the existing Coco Park Stress Test as soon as
         * the title menu is genuinely ready, then consumes the marker so the
         * returning main menu cannot relaunch it.
         */
        {
            static int s_perfAutorunChecked = 0;
            if (!s_perfAutorunChecked)
            {
                FILE *autorun = fopen("debug/perf.autorun", "rb");
                s_perfAutorunChecked = 1;
                if (autorun != NULL)
                {
                    fclose(autorun);
                    remove("debug/perf.autorun");
                    Platform_Log("[PERF TEST] autorun marker consumed\n");
                    MM_Test_Start(mainMenu);
                    return 1;
                }
            }
        }
#endif

        {
            /* Use the same global tap source as the retail menu so keyboard,
             * P1 and the active controller all reach the side launchers. */
            u32 tap = sdata->AnyPlayerTap;
            int clickedMods = MM_Mods_DrawLauncher(s_nativeModsLauncherSelected);
            int clickedTest = MM_Test_DrawLauncher(s_nativeTestLauncherSelected);

            if (clickedTest)
            {
                MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                OtherFX_Play(1, 1);
                MM_Test_Start(mainMenu);
                return 1;
            }

            if (clickedMods)
            {
                MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                OtherFX_Play(1, 1);
                MM_Mods_Open(mainMenu);
                return 1;
            }

            if (s_nativeTestLauncherSelected)
            {
                gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;

                if (tap & BTN_RIGHT)
                {
                    s_nativeTestLauncherSelected = 0;
                    s_nativeModsLauncherSelected = 1;
                    OtherFX_Play(0, 1);
                    RECTMENU_ClearInput();
                    return 1;
                }

                if (tap & (BTN_UP | BTN_DOWN | BTN_TRIANGLE | BTN_SQUARE_one))
                {
                    s_nativeTestLauncherSelected = 0;
                    MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                    mainMenu->rowSelected = (s16)s_nativeModsLauncherReturnRow;
                    OtherFX_Play(0, 1);
                    RECTMENU_ClearInput();
                    return 1;
                }

                if (tap & (BTN_CIRCLE | BTN_CROSS_one))
                {
                    MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                    OtherFX_Play(1, 1);
                    MM_Test_Start(mainMenu);
                    return 1;
                }

                RECTMENU_ClearInput();
                return 0;
            }

            if (s_nativeModsLauncherSelected)
            {
                gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;

                if (tap & BTN_LEFT)
                {
                    s_nativeModsLauncherSelected = 0;
                    s_nativeTestLauncherSelected = 1;
                    OtherFX_Play(0, 1);
                    RECTMENU_ClearInput();
                    return 1;
                }

                if (tap & (BTN_UP | BTN_DOWN | BTN_TRIANGLE | BTN_SQUARE_one))
                {
                    s_nativeModsLauncherSelected = 0;
                    MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                    mainMenu->rowSelected = (s16)s_nativeModsLauncherReturnRow;
                    OtherFX_Play(0, 1);
                    RECTMENU_ClearInput();
                    return 1;
                }

                if (tap & (BTN_CIRCLE | BTN_CROSS_one))
                {
                    MM_Mods_SetLauncherMenuLock(mainMenu, 0);
                    OtherFX_Play(1, 1);
                    MM_Mods_Open(mainMenu);
                    return 1;
                }

                RECTMENU_ClearInput();
                return 0;
            }

            if ((tap & BTN_RIGHT) && (mainMenu->rowSelected >= 0))
            {
                s_nativeModsLauncherReturnRow = mainMenu->rowSelected;
                s_nativeModsLauncherSelected = 1;
                s_nativeTestLauncherSelected = 0;
                MM_Mods_SetLauncherMenuLock(mainMenu, 1);
                gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
                OtherFX_Play(0, 1);
                RECTMENU_ClearInput();
                return 1;
            }

            if ((tap & BTN_LEFT) && (mainMenu->rowSelected >= 0))
            {
                s_nativeModsLauncherReturnRow = mainMenu->rowSelected;
                s_nativeTestLauncherSelected = 1;
                s_nativeModsLauncherSelected = 0;
                MM_Mods_SetLauncherMenuLock(mainMenu, 1);
                gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
                OtherFX_Play(0, 1);
                RECTMENU_ClearInput();
                return 1;
            }
        }
    }

    return 0;
}

#else

int MM_Mods_UpdateMainMenu(struct RectMenu *mainMenu)
{
    (void)mainMenu;
    return 0;
}

#endif
