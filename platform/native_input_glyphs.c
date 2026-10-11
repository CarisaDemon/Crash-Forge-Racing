#include <common.h>
#include <platform/native_input_glyphs.h>
#include <platform/native_input.h>
#include <platform/native_assets.h>
#include <platform/native_renderer.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_GLYPH_ATLAS_W 256
#define INPUT_GLYPH_ATLAS_H 224
#define INPUT_GLYPH_CELL_W 64
#define INPUT_GLYPH_CELL_H 32

static TextureID s_inputGlyphTexture;
static int s_inputGlyphLoadAttempted;

static int InputGlyphs_Load(void)
{
    char path[1024];
    SDL_Surface *source;
    SDL_Surface *rgba;
    unsigned char *pixels;

    if (s_inputGlyphTexture != 0)
        return 1;
    if (s_inputGlyphLoadAttempted)
        return 0;

    s_inputGlyphLoadAttempted = 1;
    if (!NativeAssets_ResolvePath(
            "UI_NATIVE/control_glyphs.png", path, sizeof(path)))
        return 0;

    source = SDL_LoadPNG(path);
    if (source == NULL)
        return 0;

    rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (rgba == NULL)
        return 0;

    if (rgba->w != INPUT_GLYPH_ATLAS_W ||
        rgba->h != INPUT_GLYPH_ATLAS_H)
    {
        SDL_DestroySurface(rgba);
        return 0;
    }

    pixels = (unsigned char *)malloc(
        (size_t)rgba->w * (size_t)rgba->h * 4u);
    if (pixels == NULL)
    {
        SDL_DestroySurface(rgba);
        return 0;
    }

    for (int y = 0; y < rgba->h; ++y)
    {
        memcpy(pixels + (size_t)y * rgba->w * 4u,
               (const unsigned char *)rgba->pixels +
                   (size_t)y * rgba->pitch,
               (size_t)rgba->w * 4u);
    }

    s_inputGlyphTexture = NativeRenderer_CreateImageTexture(
        pixels, rgba->w, rgba->h);
    free(pixels);
    SDL_DestroySurface(rgba);
    return s_inputGlyphTexture != 0;
}

static int InputGlyphs_DrawCell(
    int column, int row,
    int x, int y, int w, int h,
    struct PrimMem *primMem,
    uint32_t *ot)
{
    DR_PSYX_TEX_FT4 *packet;
    POLY_FT4 *poly;
    const size_t bytes = sizeof(DR_PSYX_TEX_FT4);
    int u0;
    int v0;
    int u1;
    int v1;

    if (column < 0 || column >= 4 || row < 0 || row >= 7 ||
        w <= 0 || h <= 0 || primMem == NULL || ot == NULL ||
        !InputGlyphs_Load())
        return 0;

    if ((char *)primMem->cursor + bytes >= (char *)primMem->guardEnd)
        return 0;

    packet = (DR_PSYX_TEX_FT4 *)primMem->cursor;
    memset(packet, 0, bytes);
    setlen(packet, (int)(bytes / sizeof(uint32_t)) - 1);
    packet->code[0] = 0xb4000000u |
                      ((uint32_t)s_inputGlyphTexture & 0x00ffffffu);
    packet->code[1] = INPUT_GLYPH_ATLAS_W |
                      (INPUT_GLYPH_ATLAS_H << 16);

    poly = &packet->poly;
    setPolyFT4(poly);
    setRGB0(poly, 128, 128, 128);

    poly->x0 = (s16)x;       poly->y0 = (s16)y;
    poly->x1 = (s16)(x + w); poly->y1 = (s16)y;
    poly->x2 = (s16)x;       poly->y2 = (s16)(y + h);
    poly->x3 = (s16)(x + w); poly->y3 = (s16)(y + h);

    u0 = column * INPUT_GLYPH_CELL_W;
    v0 = row * INPUT_GLYPH_CELL_H;
    u1 = u0 + INPUT_GLYPH_CELL_W - 1;
    v1 = v0 + INPUT_GLYPH_CELL_H - 1;

    poly->u0 = (u8)u0; poly->v0 = (u8)v0;
    poly->u1 = (u8)u1; poly->v1 = (u8)v0;
    poly->u2 = (u8)u0; poly->v2 = (u8)v1;
    poly->u3 = (u8)u1; poly->v3 = (u8)v1;
    poly->tpage = 0;
    poly->clut = 0;

    addPrim(ot, packet);
    primMem->cursor = (char *)packet + bytes;
    return 1;
}

int Platform_InputGlyphsDrawBinding(
    int playerIndex,
    int binding,
    int x, int y, int w, int h,
    struct PrimMem *primMem,
    uint32_t *ot)
{
    int style;
    int raw;
    int column = -1;
    int row = -1;

    if (binding < 0)
        return 0;

    style = Platform_InputResolvePlayerGlyphStyle(playerIndex);
    if (style == PLATFORM_INPUT_GLYPH_KEYBOARD)
        return 0;

    raw = binding &
        ~(PLATFORM_INPUT_BIND_AXIS | PLATFORM_INPUT_BIND_INVERSE);

    if ((binding & PLATFORM_INPUT_BIND_AXIS) != 0)
    {
        switch ((SDL_GamepadAxis)raw)
        {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            row = 5;
            column = (style == PLATFORM_INPUT_GLYPH_XBOX ||
                      style == PLATFORM_INPUT_GLYPH_NINTENDO) ? 2 : 0;
            break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            row = 5;
            column = (style == PLATFORM_INPUT_GLYPH_XBOX ||
                      style == PLATFORM_INPUT_GLYPH_NINTENDO) ? 3 : 1;
            break;
        default:
            return 0;
        }
    }
    else
    {
        switch ((SDL_GamepadButton)raw)
        {
        case SDL_GAMEPAD_BUTTON_NORTH: column = 0; break;
        case SDL_GAMEPAD_BUTTON_WEST:  column = 1; break;
        case SDL_GAMEPAD_BUTTON_EAST:  column = 2; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: column = 3; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            row = 4;
            column = (style == PLATFORM_INPUT_GLYPH_XBOX ||
                      style == PLATFORM_INPUT_GLYPH_NINTENDO) ? 2 : 0;
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            row = 4;
            column = (style == PLATFORM_INPUT_GLYPH_XBOX ||
                      style == PLATFORM_INPUT_GLYPH_NINTENDO) ? 3 : 1;
            break;
        case SDL_GAMEPAD_BUTTON_BACK:
            row = 6;
            column = 0;
            break;
        case SDL_GAMEPAD_BUTTON_START:
            row = 6;
            column = 1;
            break;
        default:
            return 0;
        }

        if (row < 0)
        {
            if (style == PLATFORM_INPUT_GLYPH_PLAYSTATION)
                row = 0;
            else if (style == PLATFORM_INPUT_GLYPH_XBOX)
                row = 1;
            else if (style == PLATFORM_INPUT_GLYPH_NINTENDO)
                row = 2;
            else
                row = 3;
        }
    }

    return InputGlyphs_DrawCell(
        column, row, x, y, w, h, primMem, ot);
}

int Platform_InputGlyphsDrawState(
    int waiting,
    int x, int y, int w, int h,
    struct PrimMem *primMem,
    uint32_t *ot)
{
    /* finalbuttons uses row 6 for Select/Start; the supplied
       blankbuttons art occupies columns 2/3 of that same atlas row. */
    return InputGlyphs_DrawCell(
        waiting ? 2 : 3, 6,
        x, y, w, h, primMem, ot);
}

void Platform_InputGlyphsShutdown(void)
{
    if (s_inputGlyphTexture != 0)
    {
        NativeRenderer_ReleaseImageTexture(s_inputGlyphTexture);
        s_inputGlyphTexture = 0;
    }
    s_inputGlyphLoadAttempted = 0;
}
