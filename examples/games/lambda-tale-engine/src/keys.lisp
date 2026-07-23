;;; Lambda's Tale — keyboard input normalization.
;;;
;;; The Amiga UI receives keys as IDCMP_VANILLAKEY Code+Qualifier
;;; pairs.  The keymap has already been applied to Code, so Caps Lock
;;; upcases letters exactly like Shift — but the game's controls are
;;; case-sensitive ('s' steps back, Shift-S opens the save picker), so
;;; a stuck or host-desynced Caps Lock (a classic emulator condition)
;;; would silently turn every back-step into the save screen.  The fix:
;;; for letters, derive the case from the Shift qualifier alone.
;;;
;;; Pure function, loaded on both platforms so the host suite tests it;
;;; the qualifier bits are IEQUALIFIER_LSHIFT/RSHIFT from
;;; devices/inputevent.h (mirrored in AMIGA.INTUITION, which only
;;; exists on the Amiga build).

(in-package :tale)

(defconstant +key-shift-mask+ #x0003
  "IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT.")

(defun vanilla-key-char (code qualifier)
  "Map an IDCMP_VANILLAKEY CODE+QUALIFIER to the key ACT expects:
:ESC for the Escape code, otherwise a character whose case — for
letters — reflects the Shift qualifier, not Caps Lock."
  (if (= code 27)
      :esc
      (let ((ch (code-char code)))
        (cond ((not (both-case-p ch)) ch)
              ((logtest qualifier +key-shift-mask+) (char-upcase ch))
              (t (char-downcase ch))))))
