#ifndef CL_BINDTAB_H
#define CL_BINDTAB_H

/*
 * Demand-interned binding tables (specs/raw-bindings-footprint.md, Phase 2).
 *
 * A generated OS binding module (lib/amiga/raw/) defines ~2000 names;
 * a program touches a few dozen.  Instead of materialising every one at
 * load (symbol + value/FFI stub + export cons, ~110 bytes each), the
 * module ships ONE packed byte vector — the binding table — and attaches
 * it to its package (CL_Package.bindings).  The package lookup funnel in
 * package.c consults the table when a name is not present and builds the
 * symbol on first reference; everything the program never names costs
 * the table bytes only.  Once built, a symbol is an ordinary symbol of an
 * ordinary package: EQ, FIND-SYMBOL, SYMBOL-PACKAGE, #', FUNCALL, SETF,
 * TRACE all behave per CLHS.  Enumerating or mutating the symbol set
 * (DO-SYMBOLS, APROPOS, UNINTERN, IMPORT, ...) flips the package eager —
 * every entry is built and the table dropped — so no laziness is ever
 * observable except as memory.
 *
 * Blob layout (big-endian throughout, so the FASL byte-vector literal is
 * byte-identical on the LE host and the BE m68k target):
 *
 *   header  16 bytes:  u32 magic "CLBT", u8 version, u8 flags,
 *                      u16 reserved, u32 n_entries, u32 names_len
 *   entries n x 16:    u32 name_off, u16 name_len, u8 kind, u8 flags,
 *                      u32 a, i16 b, u8 c, u8 d        — sorted by name
 *   name arena:        names_len bytes (ASCII, no terminators), plus the
 *                      8-byte payloads of CL_BT_F_WIDE values
 *
 * Per kind:
 *   CL_BT_CONST / CL_BT_VAR   value: a as u32, or i32 when CL_BT_F_SIGNED,
 *                             or the i64 at arena[a] when CL_BT_F_WIDE.
 *                             CONST -> CL_SYM_CONSTANT, VAR -> CL_SYM_SPECIAL
 *                             (a DEFCSTRUCT's *NAME-SIZE*).
 *   CL_BT_LIBCALL             a regspec (nibbles | result kind << 28),
 *                             b LVO, c nparams, d minimum library version
 *                             (0 = none); flags CL_BT_F_(NOT_)MORPHOS.
 *                             -> CL_STUB_LIBCALL stub on the package's
 *                             base-variable symbol.
 *   CL_BT_FIELD               d the reader stub kind (CL_STUB_PEEK /
 *                             PEEK_IDX / FIELD_PTR), c ctype, a offset
 *                             (| count << 16 for the indexed kinds), b
 *                             element size.  The writer %SET-<NAME>
 *                             (POKE / POKE_IDX) is derived — built with
 *                             the reader, registered as its DEFSETF.
 *   CL_BT_NAME                export only: the definition comes from an
 *                             ordinary form that follows the table (the
 *                             >7-register DEFCFUNs over CALL-LIBRARY).
 *
 * Guards are evaluated at PROBE time against *FEATURES* and the module's
 * version variable; a name whose every variant fails still materialises
 * (exported, unbound) — exactly what the eager `(when guard (defcfun ..))`
 * forms produced, so FBOUNDP-style feature probes keep working.
 */

#include "types.h"

#define CL_BT_MAGIC        0x434C4254u   /* "CLBT" */
#define CL_BT_VERSION      1
#define CL_BT_HEADER_SIZE  16
#define CL_BT_ENTRY_SIZE   16

/* header flags */
#define CL_BT_HF_LIBCALLS  0x01   /* at least one CL_BT_LIBCALL entry */

/* entry kinds */
#define CL_BT_CONST    0
#define CL_BT_VAR      1
#define CL_BT_LIBCALL  2
#define CL_BT_FIELD    3
#define CL_BT_NAME     4
#define CL_BT_KIND_MAX 4

/* entry flags */
#define CL_BT_F_NOT_MORPHOS 0x01   /* only when :MORPHOS is absent */
#define CL_BT_F_MORPHOS     0x02   /* only when :MORPHOS is present */
#define CL_BT_F_SIGNED      0x04   /* CONST/VAR: a is an int32 */
#define CL_BT_F_WIDE        0x08   /* CONST/VAR: a = arena offset of an i64 */

/* CL_Package.bindings vector slots */
#define CL_BT_SLOT_BLOB     0
#define CL_BT_SLOT_BASE     1   /* base-variable symbol (LIBCALL stubs) or NIL */
#define CL_BT_SLOT_VERSION  2   /* version-variable symbol (guards) or NIL */
#define CL_BT_SLOTS         3

/* Pack a list of rows into a blob (byte vector).  Row syntax (what
 * AMIGA.FFI:DEFINE-BINDING-TABLE takes; names are strings, used
 * literally like DEFPACKAGE's :EXPORT strings):
 *   (:const "NAME" integer)
 *   (:var   "NAME" integer)
 *   (:fn    "NAME" lvo (reg-keyword...) result-keyword [:not-morphos|:morphos] [min-version])
 *   (:field "NAME" type offset)        type = ctype keyword | (:struct n) | (:array ctype n)
 *   (:struct "NAME" size ("FIELD" type offset)...)   -> (:var "*NAME-SIZE*") + (:field "NAME-FIELD" ..)
 *   (:name  "NAME")
 * Signals a descriptive error naming the row on any malformed input. */
CL_Obj cl_bindtab_pack(CL_Obj rows);

/* Attach BLOB to PACKAGE (replacing any previous table), then fill every
 * symbol already present in PACKAGE whose name the table defines — the
 * DEFPACKAGE :SHADOW names and anything interned before the module
 * loaded.  Validates the blob first (rejects a corrupt one with a clear
 * message).  BASE_SYM / VERSION_SYM: the module's library-base and
 * version variables (symbols or NIL). */
void cl_bindtab_register(CL_Obj package, CL_Obj blob, CL_Obj base_sym,
                         CL_Obj version_sym);

/* Non-allocating lookup of NAME in PACKAGE's table; cl_package_rwlock must
 * be held.  Returns 1 on a hit with *name_idx = first entry of that name,
 * *def_idx = the first variant whose guard passes (-1 when none does) and
 * *want_setter = 0.  When ALLOW_SETTER and NAME is "%SET-<field>" for a
 * writable field entry, hits with *want_setter = 1 and both indices at
 * the field entry.  0 when the table does not define NAME. */
int cl_bindtab_probe_nolock(CL_Obj package, const char *name, uint32_t len,
                            int allow_setter, int *name_idx, int *def_idx,
                            int *want_setter);

/* Build the symbol for a probe hit and return it (the setter symbol when
 * WANT_SETTER).  Allocates outside cl_package_rwlock and links under it
 * with a re-check, so a concurrent materialisation of the same name
 * yields the same symbol.  If the name is already present, the present
 * symbol is returned; with FORCE (the registration pass) its definition
 * is (re)installed from the table, otherwise only a missing definition is
 * filled in.  PACKAGE's table may have been dropped by a concurrent flip;
 * then the symbol named QUERY (the spelling the caller looked up, QLEN
 * bytes; may be NULL) is returned — it is present by then. */
CL_Obj cl_bindtab_materialize(CL_Obj package, const char *query, uint32_t qlen,
                              int name_idx, int def_idx, int want_setter,
                              int force);

/* Flip PACKAGE eager: materialise every entry not yet present and drop the
 * table.  No-op for an ordinary package. */
void cl_bindtab_materialize_all(CL_Obj package);

/* CLAMIGA builtins: %MAKE-BINDING-TABLE, %REGISTER-BINDING-TABLE,
 * %BINDING-TABLE-ENTRIES, %BINDING-TABLE-INFO, %BINDING-TABLE-MATERIALIZE-ALL */
void cl_builtins_bindtab_init(void);

#endif /* CL_BINDTAB_H */
