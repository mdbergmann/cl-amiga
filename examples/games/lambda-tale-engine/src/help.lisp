;;; Lambda's Tale — the help screen: the key mappings as text lines.
;;;
;;; The status line is gone (the layout gives its space to the party
;;; roster), so the key reference lives on its own page under the H
;;; (or ?) key.  Pure text — both front-ends draw HELP-LINES verbatim
;;; (the SHOP-LINES pattern) and the host test suite checks it.

(in-package :tale)

(defun help-lines ()
  "The key-mapping reference as a list of text lines."
  (list "*** Keys ***"
        ""
        "W forward    S step back"
        "A turn left  D turn right"
        "M map view   H/? this help"
        "C cast spell"
        "U use item"
        "P play a song"
        "1-7 character sheet"
        "Shift-S save  Shift-L load"
        "Q or Esc quit"
        ""
        "Combat: A attack  D defend"
        "        C cast  P play  F flee"
        "Shop/menus: 1-9 pick  Esc back"
        "Map view: F full map (debug)"
        ""
        "[H]/[Esc] back"))
