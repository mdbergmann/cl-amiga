;;; Lambda's Tale engine — loader.  Self-locating: a game loads this
;;; file from wherever the engine lives — e.g. from a sibling game
;;; directory (load "../lambda-tale-engine/src/load.lisp") — and the
;;; engine finds its own sources and default tile packs through
;;; *LOAD-TRUENAME*, never through the working directory.  The working
;;; directory belongs to the GAME: maps, campaigns, zone packs and
;;; saves all resolve there (or relative to their map file).

;; The engine root (the directory holding src/, data/, tools/), as an
;; absolute path ending in a separator.  Computed before the TALE
;; package exists; profiles.lisp moves it into TALE:*ENGINE-DIR*.
(defparameter cl-user::*lambda-tale-engine-root*
  (let ((src (directory-namestring *load-truename*)))
    ;; strip the trailing "src/" component
    (subseq src 0 (- (length src) 4))))

(let ((src (concatenate 'string cl-user::*lambda-tale-engine-root* "src/")))
  (flet ((ld (file) (load (concatenate 'string src file))))
    (ld "package.lisp")
    (ld "profiles.lisp")
    (ld "dice.lisp")
    (ld "ilbm.lisp")
    (ld "map.lisp")
    (ld "knowledge.lisp")
    (ld "view.lisp")
    (ld "game.lisp")
    (ld "events.lisp")
    (ld "party.lisp")
    (ld "time.lisp")
    (ld "items.lisp")
    (ld "combat.lisp")
    (ld "specials.lisp")
    (ld "locations.lisp")
    (ld "spells.lisp")
    (ld "songs.lisp")
    (ld "save.lisp")
    (ld "save-menu.lisp")
    (ld "render.lisp")
    (ld "render-fp.lisp")
    #+amigaos (ld "amiga-ui.lisp")
    #-amigaos (ld "host-ui.lisp")))
