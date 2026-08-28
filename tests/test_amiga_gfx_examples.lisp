;;; test_amiga_gfx_examples.lisp — host-side checks of the
;;; examples/amiga/gfx/ programs, driven by tests/test_amiga_gfx_examples.sh.
;;;
;;; The host has no AmigaOS: what can be checked here is that every
;;; example compiles completely and bows out with its "not available"
;;; line, that the unattended-run knob AMIGA.INTUITION:*EVENT-LOOP-TIMEOUT*
;;; defaults to interactive, that the ports' RUN functions refuse with
;;; the C program's message (their teardown runs over nothing allocated),
;;; and the pure arithmetic the ports carry (RASSIZE, the sprite colour
;;; register rule, the sprite image).  tests/amiga/test-gfx-examples.lisp
;;; runs the programs for real in FS-UAE.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(require "amiga/intuition")

(check "host-event-loop-timeout-default-nil" nil amiga.intuition:*event-loop-timeout*)

;; Every example loads and says it is not available (bouncing-lines
;; checks the curated base, the ports their raw bases).
(defun load-quietly (path)
  (let ((out (with-output-to-string (*standard-output*)
               (load path))))
    (and (search "not available" out) t)))

(check "host-bouncing-lines-loads-and-bows-out" t
  (load-quietly "examples/amiga/gfx/bouncing-lines.lisp"))

(check "host-doublebuffer-loads-and-bows-out" t
  (load-quietly "examples/amiga/gfx/doublebuffer.lisp"))

(check "host-sprite-loads-and-bows-out" t
  (load-quietly "examples/amiga/gfx/sprite.lisp"))

;; The packages exist now; reach in through FIND-SYMBOL so this file
;; reads even if a load above failed.
(defun example-fn (package name)
  (let* ((pkg (find-package package))
         (sym (and pkg (find-symbol name pkg))))
    (and sym (fboundp sym) (symbol-function sym))))

(check "host-doublebuffer-run-refuses-without-v39" t
  (handler-case (progn (funcall (example-fn "DOUBLEBUFFER" "RUN")) nil)
    (error (e) (and (search "Gfx V39" (format nil "~A" e)) t))))

(check "host-doublebuffer-not-available" nil
  (funcall (example-fn "DOUBLEBUFFER" "AVAILABLE-P")))

(check "host-doublebuffer-rassize" '(960 32 2 4)
  (let ((rassize (example-fn "DOUBLEBUFFER" "RASSIZE")))
    ;; RASSIZE(w,h) = h * bytes per word-aligned row: 120 px = 16 bytes,
    ;; 1 px still one word, 17 px two words.
    (list (funcall rassize 120 60) (funcall rassize 1 16)
          (funcall rassize 16 1) (funcall rassize 17 1))))

(check "host-doublebuffer-cleanup-over-nothing" t
  ;; CLEANUP on a fresh DEMO must tolerate every slot being empty.
  (progn (funcall (example-fn "DOUBLEBUFFER" "CLEANUP")
                  (funcall (example-fn "DOUBLEBUFFER" "MAKE-DEMO")))
         t))

(check "host-sprite-run-refuses" t
  (handler-case (progn (funcall (example-fn "SIMPLE-SPRITE" "RUN")) nil)
    (error () t)))

(check "host-sprite-not-available" nil
  (funcall (example-fn "SIMPLE-SPRITE" "AVAILABLE-P")))

(check "host-sprite-image-words" '(22 #xffff #x0000 #xffff #xffff)
  ;; 11 word pairs; line 1 is colour 1 (01), line 8 colour 3 (11).
  (let ((data (symbol-value (find-symbol "*SPRITE-DATA*" "SIMPLE-SPRITE"))))
    (list (length data) (nth 2 data) (nth 3 data) (nth 16 data) (nth 17 data))))

(check "host-sprite-dma-is-a-noop-off-amiga" nil
  (funcall (example-fn "SIMPLE-SPRITE" "SPRITE-DMA") t))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL GFX EXAMPLES HOST CHECKS PASSED~%")
    (format t "SOME GFX EXAMPLES HOST CHECKS FAILED~%"))
