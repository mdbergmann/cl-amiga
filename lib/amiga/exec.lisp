;;; amiga/exec.lisp — exec.library abstractions for CL-Amiga
;;;
;;; Loaded via (require "amiga/exec").
;;; Memory introspection (AvailMem) and chip-RAM helpers on top of the
;;; AMIGA package's ALLOC-CHIP/FREE-CHIP builtins.

;; compile-time too: COMPILE-FILE (the host builds the lib/amiga FASLs) must see
;; these packages at read time, not only LOAD.
(eval-when (:compile-toplevel :load-toplevel :execute)
  (require "amiga/ffi"))

(defpackage "AMIGA.EXEC"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:export
   ;; Library
   "*EXEC-BASE*"
   ;; Memory introspection
   "AVAIL-MEM"
   ;; MEMF_* requirement/option flags (exec/memory.h)
   "+MEMF-ANY+" "+MEMF-PUBLIC+" "+MEMF-CHIP+" "+MEMF-FAST+"
   "+MEMF-CLEAR+" "+MEMF-LARGEST+" "+MEMF-TOTAL+"
   ;; Chip RAM helpers
   "ALLOC-CHIP-BYTES"))

(in-package "AMIGA.EXEC")

;;; ================================================================
;;; Exec library base — ExecBase lives at absolute address 4, no
;;; OpenLibrary needed (exec is what OpenLibrary itself lives in).
;;; NIL on the host build, where this module is loaded for COMPILE-FILE
;;; (the host builds the lib/amiga FASLs): address 4 is not readable
;;; there -- same convention as the generated amiga/raw modules.
;;; ================================================================

(defvar *exec-base*
  (when (member :amigaos *features*)
    (ffi:make-foreign-pointer (ffi:peek-u32 (ffi:make-foreign-pointer 4)))))

;;; ================================================================
;;; LVO offsets (from amiga/exec_lib.fd)
;;; ================================================================

(defconstant +lvo-avail-mem+ -216)

;;; ================================================================
;;; MEMF_* flags (exec/memory.h) — AvailMem/AllocMem requirements.
;;; ================================================================

(defconstant +memf-any+     #x00000000)
(defconstant +memf-public+  #x00000001)
(defconstant +memf-chip+    #x00000002)
(defconstant +memf-fast+    #x00000004)
(defconstant +memf-clear+   #x00010000)
(defconstant +memf-largest+ #x00020000)
(defconstant +memf-total+   #x00080000)

;;; ================================================================
;;; AvailMem
;;; ================================================================

(defun avail-mem (&optional (requirements +memf-any+))
  "Free system memory in bytes -- exec.library AvailMem(REQUIREMENTS).
REQUIREMENTS is a MEMF_* mask: +MEMF-ANY+ (the default) counts all
free memory, +MEMF-CHIP+/+MEMF-FAST+ one pool, +MEMF-LARGEST+ ORed in
asks for the largest single free block instead of the sum, and
+MEMF-TOTAL+ for the pool's total size.  A snapshot, not a
reservation: other tasks allocate concurrently."
  (amiga:call-library *exec-base* +lvo-avail-mem+
                      (list :d1 requirements)))

;;; ================================================================
;;; Chip RAM helpers
;;; ================================================================

(defun alloc-chip-bytes (bytes)
  "Copy the (unsigned-byte 8) vector BYTES into a fresh chip-RAM
allocation -- the standard way to hand data to hardware that requires
chip memory (blitter masks, sprite images, audio samples).  Returns
the chip foreign pointer; the caller frees it with AMIGA:FREE-CHIP."
  (let ((chip (amiga:alloc-chip (length bytes))))
    (ffi:poke-bytes chip bytes)
    chip))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/exec")
