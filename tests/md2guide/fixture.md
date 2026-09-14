# Fixture — the `md2guide` mapping

The golden fixture of `tools/docs/md2guide.lisp`: one instance of every
Markdown construct the converter supports, converted by `make test` and
compared byte-for-byte with `fixture.guide` (see `specs/amigaguide-docs.md`).
Everything before the first level-2 heading is the body of the `main` node.

> **Note:** a block quote is indented four columns, and the bold lead
> keeps its bold.  A link inside it works: [Tables](#tables).

- **Package:** `FIXTURE` (uses CL)
- A bullet item long enough to be re-flowed onto a second line, with a hanging
  indent, `inline code`, *italic* and **bold** text, and a link to
  [Other](other.md).
  - A nested item, also long enough to wrap onto a second line with its own
    hanging indent two columns deeper.
  - Another nested item.
- Back at the top level.

1. An ordered item.
2. Another, with a link to a [section of the other file](other.md#second-section).

## Inline markup

Bold with **two stars**, italic with *one star*, nested *italic with
**bold** inside*, `code with *stars* and [brackets]`, a literal 5 * 3, a
literal [bracketed] word, ``double backticks with a ` inside``, and
escapes: \*not italic\*, \`not code\`, a\_b.  A link whose text is its
target: [other.md](other.md); links that stay text: a
[URL](https://example.org/path), a [file that is not converted](../foo.md), a
[directory](examples/), and the [title of this file](#fixture--the-md2guide-mapping).
AmigaGuide specials in text: an @ sign, a \ backslash, an @{ brace, and a
"double-quoted" phrase in a link label: [see "Tables"](#tables).

An image on its own: ![The alt text of an image](docs/scrshts/x.png), and a
badge (an image inside a link, dropped): [![CI](https://example.org/badge.svg)](https://example.org/ci).

## Characters

Mapped: em—dash, en–dash, ellipsis…, arrows → ← ↔ ⇄, comparisons ≥ ≤ ≡ ≠,
quotes “double” and ‘single’, a non-breaking space, and a 🙂 face.
Latin-1 passes through: § × ä ö ü ß é.

## Tables

| Signature | Kind | Description |
|-----------|------|-------------|
| `(socket-listen port &optional loopback)` | function | Open a listening server socket on a port (port `0` picks a free one; `loopback` non-`nil` binds 127.0.0.1 only) |
| `*verbose*` | variable | A literal pipe: `a \| b`; **bold** and a [link](#code) in a cell |
| `(quiet)` | macro | |

| Option | Default | Description |
|--------|---------|-------------|
| `--heap SIZE` | `4M` | Heap size |
| `--no-image` | | Boot from the FASLs |

| Package | Prefix | Doc |
|---------|--------|-----|
| `EXT` | `ext:` | [ext.md](other.md) |

## Code

```lisp
(defun hello ()
  (format t "Hello @ Amiga~%"))   ; an @ and a \ backslash

  a line with leading blanks and a trailing blank line
```

```
no language tag, and a line that is a good deal longer than seventy-six columns to see clipping
```

### A level-3 section

Text of the level-3 section.  Level-2 nodes end with a Sections list.

#### A level-4 heading is a bold line

Text under it stays in the level-3 node.

### Duplicate

First of two headings with the same text.

### Duplicate

Second: its node name gets a `-1` suffix, and this links to it:
[second Duplicate](#duplicate-1), [first Duplicate](#duplicate).

---

## After a rule

A thematic break emits nothing.  The title of this document contains code
markers and an em dash; its anchor is `#fixture--the-md2guide-mapping`.
