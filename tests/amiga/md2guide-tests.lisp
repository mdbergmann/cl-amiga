; md2guide-tests.lisp -- the AmigaGuide documentation converter on the
; target (tools/docs/md2guide.lisp, specs/amigaguide-docs.md).
;
; Loaded from run-tests.lisp (nested LOAD: the converter defines its own
; package, which cannot exist when this file is read).  Converts the
; golden fixture set (tests/md2guide/fixture.md + other.md + sub/child.md)
; into T: -- the converter creates T:sub/ itself -- and compares the
; result byte for byte with the committed fixture.guide / other.guide /
; sub/child.guide -- the same comparison tests/test_md2guide.sh makes on
; the host.  It proves the converter is portable Lisp: UTF-8 is decoded by
; the converter itself from a Latin-1 stream, so the output is identical
; on the byte-string m68k build and on wide-string builds.  The fixture,
; not the shipped docs, to keep suite time flat.

(load "tools/docs/md2guide.lisp")

(defun md2guide-test-file-bytes (path)
  "PATH as a string of its bytes (Latin-1 stream: one character per byte)."
  (with-open-file (in path :direction :input :external-format :latin-1)
    (let ((out (make-string-output-stream)))
      (loop for c = (read-char in nil nil)
            while c
            do (write-char c out))
      (get-output-stream-string out))))

(defun md2guide-test-first-difference (a b)
  "Index of the first differing character, or NIL when equal."
  (or (mismatch a b)
      nil))

(check "md2guide converts the fixture set to T:"
       3
       (md2guide:convert-set '(("tests/md2guide/fixture.md" . "fixture.guide")
                               ("tests/md2guide/other.md" . "other.guide")
                               ("tests/md2guide/sub/child.md" . "sub/child.guide"))
                             "T:" :version "0.0.0" :date "01.01.2000"))

(let ((expected (md2guide-test-file-bytes "tests/md2guide/fixture.guide"))
      (actual (md2guide-test-file-bytes "T:fixture.guide")))
  (check "md2guide fixture.guide byte-identical to the golden file"
         nil
         (md2guide-test-first-difference expected actual))
  (check "md2guide fixture.guide is not empty" t (> (length actual) 1000)))

(let ((expected (md2guide-test-file-bytes "tests/md2guide/other.guide"))
      (actual (md2guide-test-file-bytes "T:other.guide")))
  (check "md2guide other.guide byte-identical to the golden file"
         nil
         (md2guide-test-first-difference expected actual)))

; The guide one directory down: written into a directory the converter
; created, its links up spelled as AmigaDOS parent paths (/fixture.guide).
(check "md2guide created T:sub/ for the nested guide"
       t
       (and (probe-file "T:sub/child.guide") t))
(let ((expected (md2guide-test-file-bytes "tests/md2guide/sub/child.guide"))
      (actual (md2guide-test-file-bytes "T:sub/child.guide")))
  (check "md2guide sub/child.guide byte-identical to the golden file"
         nil
         (md2guide-test-first-difference expected actual))
  (check "md2guide nested guide links up with an AmigaDOS parent path"
         t
         (and (search "LINK \"/fixture.guide/tables\"" actual) t)))

; An unsupported construct is a located error, and nothing is written.
(check "md2guide error fixture signals file:line diagnostic"
       t
       (handler-case
           (progn (md2guide:convert-set '(("tests/md2guide/errors/setext.md" . "setext.guide"))
                                        "T:")
                  nil)
         (md2guide:md2guide-error (e)
           (let ((msg (format nil "~A" e)))
             (and (search "tests/md2guide/errors/setext.md:4:" msg)
                  (search "setext heading" msg)
                  t)))))

(check "md2guide dangling link is an error"
       t
       (handler-case
           (progn (md2guide:convert-set '(("tests/md2guide/errors/dangling-link.md" . "dl.guide"))
                                        "T:")
                  nil)
         (md2guide:md2guide-error (e)
           (and (search "dangling link: #nowhere" (format nil "~A" e)) t))))

(check "md2guide error wrote no output file"
       nil
       (probe-file "T:setext.guide"))

(delete-file "T:fixture.guide")
(delete-file "T:other.guide")
(delete-file "T:sub/child.guide")
