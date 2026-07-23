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

;; Each file's load is timed; the breakdown lands in the engine debug
;; log once loading is over (a no-op unless TALE_DEBUG_LOG enabled it).
;; With the runtime's internal-time epoch anchored at process start,
;; the "launch ->" figures are true ms-since-clamiga-launch, so the log
;; also shows what the boot (C init + lib FASLs) cost before us.
(let ((src (concatenate 'string cl-user::*lambda-tale-engine-root* "src/"))
      (t0 (get-internal-real-time))
      (times '()))
  (flet ((ms (delta) (round (* 1000 delta) internal-time-units-per-second))
         (ld (file)
           (let ((start (get-internal-real-time)))
             (load (concatenate 'string src file))
             (push (cons file (- (get-internal-real-time) start)) times))))
    (ld "package.lisp")
    (ld "debug-log.lisp")
    (ld "profiles.lisp")
    (ld "palette.lisp")
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
    (ld "keys.lisp")
    (ld "help.lisp")
    (ld "microfont.lisp")
    (ld "render.lisp")
    (ld "render-fp.lisp")
    #+amigaos (ld "amiga-ui.lisp")
    #-amigaos (ld "host-ui.lisp")
    ;; TALE::%DLOG is looked up at runtime: this form is READ before
    ;; the TALE package exists, so its symbols cannot appear literally.
    (let ((dlog (let ((s (find-symbol "%DLOG" "TALE")))
                  (and s (fboundp s) (symbol-function s)))))
      (when dlog
        (dolist (entry (reverse times))
          (funcall dlog "  load ~A [~D ms]" (car entry) (ms (cdr entry))))
        (funcall dlog "engine sources loaded: ~D files in ~D ms (launch -> loader ~D ms, launch -> loaded ~D ms)"
                 (length times)
                 (ms (- (get-internal-real-time) t0))
                 (ms t0)
                 (ms (get-internal-real-time)))))))
