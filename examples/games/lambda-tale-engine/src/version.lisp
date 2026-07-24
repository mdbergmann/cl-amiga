;;; Lambda's Tale — engine version.
;;;
;;; One place holds the engine's version; anything that wants to report
;;; it (a title screen, a save stamp, a bug report) derives it from here
;;; instead of spelling out a number of its own.  The shape follows the
;;; repo's own convention — MAJOR/MINOR/PATCH plus a DD.MM.YYYY date,
;;; as in CL_VERSION_* in src/core/types.h.
;;;
;;; A GAME built on the engine carries its OWN version, independent of
;;; this one: the engine only declares the slots (*GAME-NAME* and
;;; friends, all NIL here) and each game's own version.lisp fills them
;;; in — see ../../closure/src/version.lisp for the worked example.
;;;
;;; Loaded first after the package, so every later file may refer to it.

(in-package :tale)

;;; ---------------------------------------------------------------------
;;; The engine's version

(defconstant +engine-version-major+ 0)
(defconstant +engine-version-minor+ 1)
(defconstant +engine-version-patch+ 0)

(defparameter *engine-name* "Lambda's Tale"
  "Display name of the engine itself.")

(defparameter *engine-version-date* "24.07.2026"
  "Date of this engine version, DD.MM.YYYY.")

(defun engine-version ()
  "The engine version as three values: major, minor, patch."
  (values +engine-version-major+
          +engine-version-minor+
          +engine-version-patch+))

(defun engine-version-string ()
  "The engine version as \"MAJOR.MINOR.PATCH\"."
  (format nil "~D.~D.~D"
          +engine-version-major+
          +engine-version-minor+
          +engine-version-patch+))

;;; ---------------------------------------------------------------------
;;; The slots a game fills in
;;;
;;; NIL until a game sets them, so engine-only sessions (the engine test
;;; suite, the asset tools) are honest about having no game loaded.

(defparameter *game-name* nil
  "Display name of the game running on the engine, or NIL if none —
set by the game's own version.lisp.")

(defparameter *game-version* nil
  "Version of the game running on the engine, a string like \"0.1\",
or NIL if none.  Versioned by the game, not by the engine: the two
numbers move independently.")

(defparameter *game-version-date* nil
  "Date of the game's version, DD.MM.YYYY, or NIL if none.")
