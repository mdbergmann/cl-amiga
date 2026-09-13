#!/usr/bin/env python3
"""Generate the Workbench icons of the binary release (icons/*.info).

Each icon is a classic (OS 1.3/2.x-style) DiskObject project icon whose
default tool is C:IconX, so a double-click on Workbench or Ambient runs the
launcher script of the same name next to it -- `icons/CLAmiga` starts
bin/aos3/clamiga in a console window, `icons/CLAmiga-FPU` the hard-float
build, `icons/Clamacs` the editor.  The script, not the icon, picks bin/mos
on MorphOS, so one icon serves both systems.

Why classic icons: every Workbench from 1.3 on renders a 2-bitplane
DiskObject with the standard 4-colour palette (0 grey, 1 black, 2 white,
3 blue), no icon.library, NewIcons or OS3.5 colour-icon support needed --
and the file is ~500 bytes.  The image is the same 48x24 lambda card for
all three; a badge tells the FPU build and the editor apart.

    python3 scripts/make-icons.py [OUTDIR]        # default: icons/

Layout written (big-endian, as icon.library reads it):

    struct DiskObject   78 bytes (magic 0xE310, version 1, embedded Gadget)
    struct Image        20 bytes (GadgetRender), then the bitplanes
    default tool        LONG length + NUL-terminated string
    tool types          LONG (n+1)*4, then per entry LONG length + string

Pointer fields in the file are flags only (non-zero = "follows").  Checked
in as binaries because a Python is not part of the release build's
requirements; re-run this after changing the art and commit the .info files.
"""
import os
import struct
import sys

# --- art ------------------------------------------------------------------

W, H = 48, 24

# Colour indices of the standard Workbench 2.x+ 4-colour icon palette.
GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3

LAMBDA = [
    "###...........",
    ".###..........",
    "..###.........",
    "...###........",
    "....###.......",
    ".....###......",
    "......###.....",
    "......####....",
    ".....##.###...",
    "....##...###..",
    "...##.....###.",
    "..##.......###",
    ".##.........##",
    "##...........#",
]

# 3x5 pixel capitals for the badge text.
FONT = {
    "A": ["###", "#.#", "###", "#.#", "#.#"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "F": ["###", "#..", "##.", "#..", "#.."],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"],
}


def blank():
    return [[GREY] * W for _ in range(H)]


def card(px):
    """White card with a black border and a one-pixel shadow, inset 1px."""
    left, top, right, bottom = 1, 1, W - 3, H - 3
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            px[y][x] = WHITE
    for x in range(left, right + 1):
        px[top][x] = BLACK
        px[bottom][x] = BLACK
    for y in range(top, bottom + 1):
        px[y][left] = BLACK
        px[y][right] = BLACK
    # shadow along the right and bottom edges
    for y in range(top + 1, bottom + 2):
        px[y][right + 1] = BLACK
    for x in range(left + 1, right + 2):
        px[bottom + 1][x] = BLACK


def blit(px, sprite, x0, y0, colour):
    for dy, row in enumerate(sprite):
        for dx, ch in enumerate(row):
            if ch == "#":
                px[y0 + dy][x0 + dx] = colour


def text(px, s, x0, y0, colour):
    for i, ch in enumerate(s):
        blit(px, FONT[ch], x0 + i * 4, y0, colour)


def badge(px, s):
    """Blue tab in the lower right corner of the card with white text."""
    tw = len(s) * 4 - 1
    x1, y1 = W - 4, H - 4          # inside the border, above the shadow
    x0, y0 = x1 - tw - 3, y1 - 7
    for y in range(y0, y1):
        for x in range(x0, x1):
            px[y][x] = BLUE
    text(px, s, x0 + 2, y0 + 1, WHITE)


def art(kind):
    px = blank()
    card(px)
    if kind == "clamiga":
        blit(px, LAMBDA, 17, 5, BLACK)
    elif kind == "clamiga-fpu":
        blit(px, LAMBDA, 12, 5, BLACK)
        badge(px, "FPU")
    elif kind == "clamacs":
        blit(px, LAMBDA, 8, 5, BLACK)
        # a text cursor and two "lines" to the right of the lambda
        for y in range(6, 18):
            px[y][27] = BLACK
        for x in range(30, 42):
            px[7][x] = BLACK
            px[10][x] = BLACK
        badge(px, "IDE")
    else:
        raise ValueError(kind)
    return px


# --- DiskObject writer ----------------------------------------------------

WBTOOL, WBPROJECT = 3, 4
GFLG_GADGIMAGE = 0x0004
GACT_RELVERIFY, GACT_IMMEDIATE = 0x0001, 0x0002
GTYP_BOOLGADGET = 0x0001


def planes(px, depth=2):
    """Bitplane data: per plane, H rows of ceil(W/16) words, MSB first."""
    row_words = (W + 15) // 16
    out = bytearray()
    for plane in range(depth):
        for y in range(H):
            bits = 0
            for x in range(W):
                if (px[y][x] >> plane) & 1:
                    bits |= 1 << (row_words * 16 - 1 - x)
            out += bits.to_bytes(row_words * 2, "big")
    return bytes(out)


def cstring(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def diskobject(px, default_tool, tooltypes, stack):
    gadget = struct.pack(
        ">IhhhhHHHIIIIIHI",
        0,                      # NextGadget
        0, 0, W, H,             # LeftEdge, TopEdge, Width, Height
        GFLG_GADGIMAGE,         # Flags: image gadget, complement highlight
        GACT_RELVERIFY | GACT_IMMEDIATE,
        GTYP_BOOLGADGET,
        1,                      # GadgetRender (follows)
        0,                      # SelectRender
        0,                      # GadgetText
        0,                      # MutualExclude
        0,                      # SpecialInfo
        0,                      # GadgetID
        1,                      # UserData: 1 = OS 2.x icon revision
    )
    assert len(gadget) == 44
    NO_ICON_POSITION = 0x80000000   # Workbench places the icon itself
    head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIIIIIi",
        WBPROJECT, 0,           # do_Type, pad
        1,                      # do_DefaultTool (follows)
        1 if tooltypes else 0,  # do_ToolTypes (follows)
        NO_ICON_POSITION,       # do_CurrentX
        NO_ICON_POSITION,       # do_CurrentY
        0,                      # do_DrawerData
        0,                      # do_ToolWindow
        stack,                  # do_StackSize
    )
    assert len(head) == 78
    image = struct.pack(">hhhhhIBBI", 0, 0, W, H, 2, 1, 0x03, 0x00, 0)
    assert len(image) == 20
    body = head + image + planes(px) + cstring(default_tool)
    if tooltypes:
        body += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            body += cstring(t)
    return body


ICONS = {
    # name: (art kind, console window of the IconX run)
    "CLAmiga":     ("clamiga",     "CON:0/20/640/236/CLAmiga/CLOSE"),
    "CLAmiga-FPU": ("clamiga-fpu", "CON:0/20/640/236/CLAmiga-FPU/CLOSE"),
    "Clamacs":     ("clamacs",     "NIL:"),
}


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "icons")
    os.makedirs(outdir, exist_ok=True)
    for name, (kind, window) in ICONS.items():
        data = diskobject(art(kind), "C:IconX", ["WINDOW=" + window], 131072)
        path = os.path.join(outdir, name + ".info")
        with open(path, "wb") as f:
            f.write(data)
        print("%s: %d bytes" % (path, len(data)))


if __name__ == "__main__":
    main()
