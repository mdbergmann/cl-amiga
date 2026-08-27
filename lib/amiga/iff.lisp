;;; amiga/iff.lisp — IFF file parsing and writing over iffparse.library
;;;
;;; Loaded via (require "amiga/iff").
;;;
;;; A Common Lisp port of the NDK 3.1 sift example (sift.c by Stuart
;;; Ferguson and Leo Schwab, Examples2/IFF/other on the Amiga Developer
;;; CD), grown into a small module: SIFT prints the IFFCheck-like
;;; listing of any IFF file or of the clipboard, and the machinery it
;;; needs — ParseIFF stepping, chunk inspection, and the matching write
;;; side — is exported for programs that read or write IFF themselves
;;; (ILBM images, 8SVX sounds, FTXT clips, own formats).
;;;
;;;   OPEN-IFF / CLOSE-IFF / WITH-IFF   a DOS file or :CLIPBOARD, for
;;;                    :READ or :WRITE (AllocIFF + InitIFFasDOS /
;;;                    OpenClipboard + InitIFFasClip + OpenIFF)
;;;   PARSE-STEP       one ParseIFF step, IFFPARSE_RAWSTEP by default:
;;;                    :CHUNK entered a context, :END-OF-CHUNK about to
;;;                    leave one, :EOF done; real errors signal
;;;   CURRENT-CHUNK    id, type, size, scan of the current context node
;;;   IFF-DEPTH        chunk nesting depth (iff_Depth)
;;;   READ-CHUNK-BYTES / WRITE-CHUNK-BYTES   chunk data to and from
;;;                    Lisp byte vectors and strings
;;;   PUSH-CHUNK / POP-CHUNK   write-side chunk brackets; PopChunk
;;;                    back-patches an IFFSIZE_UNKNOWN size
;;;   MAP-CHUNKS       call a function with (id type size depth) for
;;;                    every chunk of a file or the clipboard
;;;   SIFT             the sift.c program: print the chunk structure
;;;   ID-STRING / STRING-ID / ERROR-STRING   pure-Lisp helpers, also
;;;                    available off-Amiga
;;;
;;; Deviations from the C original, on purpose:
;;;   - A parse error signals a Lisp ERROR carrying the IFFERR text
;;;     (ERROR-STRING) instead of printing "File scan aborted" and
;;;     exiting; SIFT's callers choose how to handle it.
;;;   - IDs print via the pure-Lisp ID-STRING instead of IDtoStr — same
;;;     four characters, but it works on any host.
;;;
;;; See tests/amiga/test-iff.lisp for the executable specification and
;;; examples/amiga/iff/sift.lisp for the demo program.

;; compile-time too: COMPILE-FILE (the host builds the lib/amiga FASLs) must see
;; these packages at read time, not only LOAD.
(eval-when (:compile-toplevel :load-toplevel :execute)
  (require "amiga/ffi")
  (require "amiga/raw/iffparse")
  (require "amiga/raw/dos"))

(defpackage "AMIGA.IFF"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:local-nicknames ("IP"  "AMIGA.RAW.IFFPARSE")
                    ("DOS" "AMIGA.RAW.DOS"))
  (:export
   "AVAILABLE-P"
   "IFF-FILE" "IFF-FILE-P"
   "OPEN-IFF" "CLOSE-IFF" "WITH-IFF"
   "PARSE-STEP" "CURRENT-CHUNK" "IFF-DEPTH"
   "READ-CHUNK-BYTES" "WRITE-CHUNK-BYTES"
   "PUSH-CHUNK" "POP-CHUNK"
   "MAP-CHUNKS" "SIFT"
   "ID-STRING" "STRING-ID" "ERROR-STRING"))

(in-package "AMIGA.IFF")

(defun available-p ()
  "True when iffparse.library is open: an AmigaOS or MorphOS runtime.
NIL on other hosts, where the module still loads for compilation and
the pure-Lisp helpers."
  (and (member :amigaos *features*) ip:*iffparse-base* t))

;;; ================================================================
;;; Pure-Lisp helpers — usable on any host
;;; ================================================================

(defun id-string (id)
  "The four-character string of the IFF identifier ID (an integer),
e.g. #x464F524D -> \"FORM\".  The inverse of STRING-ID."
  (check-type id integer)
  (let ((u (logand id #xFFFFFFFF)))
    (map 'string #'code-char
         (list (ldb (byte 8 24) u) (ldb (byte 8 16) u)
               (ldb (byte 8 8) u) (ldb (byte 8 0) u)))))

(defun string-id (string)
  "The integer IFF identifier of the four-character STRING,
e.g. \"FORM\" -> #x464F524D.  The inverse of ID-STRING."
  (unless (and (stringp string) (= (length string) 4)
               (every (lambda (c) (< (char-code c) 256)) string))
    (error "AMIGA.IFF: STRING-ID: ~S is not a string of four 8-bit characters"
           string))
  (logior (ash (char-code (char string 0)) 24)
          (ash (char-code (char string 1)) 16)
          (ash (char-code (char string 2)) 8)
          (char-code (char string 3))))

;; The IFFERR_#? texts of the C original, indexed by (- code) - 1.
(defparameter *error-strings*
  #("End of file (not an error)."
    "End of context (not an error)."
    "No lexical scope."
    "Insufficient memory."
    "Stream read error."
    "Stream write error."
    "Stream seek error."
    "File is corrupt."
    "IFF syntax error."
    "Not an IFF file."
    "Required call-back hook missing."
    "Return to client.  You should never see this."))

(defun error-string (code)
  "The text for the iffparse IFFERR_#? CODE (a non-positive integer),
matching the C sift's message table."
  (let ((idx (- (- code) 1)))
    (if (and (integerp code) (<= 0 idx) (< idx (length *error-strings*)))
        (aref *error-strings* idx)
        (format nil "Unknown IFF error ~S." code))))

(defun %id (designator op)
  "An ID argument: a four-character string, an integer, or NIL for the
null type of an untyped chunk."
  (cond ((null designator) 0)
        ((integerp designator) (logand designator #xFFFFFFFF))
        ((stringp designator) (string-id designator))
        (t (error "AMIGA.IFF: ~A: ID must be a 4-character string, an integer or NIL, got ~S"
                  op designator))))

;;; ================================================================
;;; Open / close
;;; ================================================================

;;; The OS-visible state is the foreign IFFHandle; what CloseIFF cannot
;;; do itself — closing the stream underneath, as in the C's 'bye:' —
;;; is remembered here.

(defstruct (iff-file (:constructor %make-iff-file)
                     (:copier nil))
  name            ; the file name or "clipboard unit N", for messages
  handle          ; foreign IFFHandle pointer, NIL once closed
  stream          ; BPTR integer (:DOS) or ClipboardHandle pointer (:CLIPBOARD)
  kind            ; :DOS or :CLIPBOARD
  read-p)         ; opened with IFFF_READ

(defun %check-open (file op)
  (unless (iff-file-p file)
    (error "AMIGA.IFF: ~A: not an IFF-FILE: ~S" op file))
  (unless (iff-file-handle file)
    (error "AMIGA.IFF: ~A: ~A is already closed" op (iff-file-name file))))

(defun %close-stream (kind stream)
  (ecase kind
    (:dos (dos:close stream))
    (:clipboard (ip:close-clipboard stream))))

(defun open-iff (source &key (direction :read) (clipboard-unit 0))
  "Open SOURCE for IFF parsing or writing and return an IFF-FILE.
SOURCE is a file name, or :CLIPBOARD for clipboard unit CLIPBOARD-UNIT
(the C sift's -c).  DIRECTION is :READ or :WRITE (:WRITE on a file
creates it, replacing any existing one).  Signals an error if the
source cannot be opened."
  (unless (available-p)
    (error "AMIGA.IFF: only available on AmigaOS/MorphOS (iffparse.library)"))
  (check-type direction (member :read :write))
  (unless (or (eq source :clipboard) (stringp source))
    (error "AMIGA.IFF: OPEN-IFF: source must be a file name or :CLIPBOARD, got ~S"
           source))
  (let ((handle (ip:alloc-iff)))
    (unless handle
      (error "AMIGA.IFF: OPEN-IFF: AllocIFF failed"))
    (let (stream kind name)
      ;; The stream under the IFFHandle: iff_Stream is the Open() BPTR
      ;; for a DOS file, the ClipboardHandle address for the clipboard.
      (if (eq source :clipboard)
          (let ((ch (ip:open-clipboard clipboard-unit)))
            (unless ch
              (ip:free-iff handle)
              (error "AMIGA.IFF: OPEN-IFF: cannot open clipboard unit ~D"
                     clipboard-unit))
            (setf stream ch kind :clipboard
                  name (format nil "clipboard unit ~D" clipboard-unit))
            (ffi:poke-u32 handle (ffi:foreign-pointer-address ch) 0)
            (ip:init-if-fas-clip handle))
          (let ((fh (ffi:with-foreign-string (fname source)
                      (dos:open fname (if (eq direction :read)
                                          dos:+mode-oldfile+
                                          dos:+mode-newfile+)))))
            (when (zerop fh)
              (let ((err (dos:io-err)))
                (ip:free-iff handle)
                (error "AMIGA.IFF: OPEN-IFF: cannot open ~S for ~S (DOS error ~D)"
                       source direction err)))
            (setf stream fh kind :dos name source)
            (ffi:poke-u32 handle fh 0)
            (ip:init-if-fas-dos handle)))
      (let ((err (ip:open-iff handle (if (eq direction :read)
                                         ip:+ifff-read+
                                         ip:+ifff-write+))))
        (unless (zerop err)
          (%close-stream kind stream)
          (ip:free-iff handle)
          (error "AMIGA.IFF: OPEN-IFF: OpenIFF on ~A failed: ~A"
                 name (error-string err))))
      (%make-iff-file :name name :handle handle :stream stream :kind kind
                      :read-p (eq direction :read)))))

(defun close-iff (file)
  "Close FILE: CloseIFF (completing the write side's outer chunk, if
any), close the stream underneath, FreeIFF.  Returns T.  Closing an
already-closed file is a no-op."
  (unless (iff-file-p file)
    (error "AMIGA.IFF: CLOSE-IFF: not an IFF-FILE: ~S" file))
  (when (iff-file-handle file)
    (ip:close-iff (iff-file-handle file))
    (%close-stream (iff-file-kind file) (iff-file-stream file))
    (ip:free-iff (iff-file-handle file))
    (setf (iff-file-handle file) nil))
  t)

(defmacro with-iff ((var source &rest open-args) &body body)
  "Bind VAR to (OPEN-IFF SOURCE . OPEN-ARGS) around BODY, closing on
the way out however BODY exits."
  `(let ((,var (open-iff ,source ,@open-args)))
     (unwind-protect (progn ,@body)
       (close-iff ,var))))

;;; ================================================================
;;; Parsing — the read side
;;; ================================================================

(defun parse-step (file &optional (mode :rawstep))
  "One ParseIFF step on FILE.  MODE is :RAWSTEP (the default — every
context boundary is reported, what SIFT needs), :STEP (like :RAWSTEP
but with iffparse's automatic prop/stop handling) or :SCAN (run to the
next declared stop chunk).  Returns :CHUNK on entering a context (or
reaching a stop chunk under :SCAN), :END-OF-CHUNK when about to leave
one, :EOF at the end.  Anything else signals an error carrying the
ERROR-STRING text."
  (%check-open file "PARSE-STEP")
  (let ((err (ip:parse-iff (iff-file-handle file)
                           (ecase mode
                             (:rawstep ip:+iffparse-rawstep+)
                             (:step ip:+iffparse-step+)
                             (:scan ip:+iffparse-scan+)))))
    (cond ((zerop err) :chunk)
          ((= err ip:+ifferr-eoc+) :end-of-chunk)
          ((= err ip:+ifferr-eof+) :eof)
          (t (error "AMIGA.IFF: PARSE-STEP on ~A failed, error ~D: ~A"
                    (iff-file-name file) err (error-string err))))))

(defun current-chunk (file)
  "The current context node of FILE, as (VALUES ID TYPE SIZE SCAN) —
cn_ID, cn_Type, cn_Size and cn_Scan, integers (ID-STRING makes them
readable).  NIL when there is no current chunk."
  (%check-open file "CURRENT-CHUNK")
  (let ((node (ip:current-chunk (iff-file-handle file))))
    (when node
      (values (ip:context-node-id node)
              (ip:context-node-type node)
              (ip:context-node-size node)
              (ip:context-node-scan node)))))

(defun iff-depth (file)
  "FILE's current chunk nesting depth (iff_Depth): 1 at the top-level
FORM (the default outer context counts), 2 inside it, and so on — the
C sift's indentation count."
  (%check-open file "IFF-DEPTH")
  (ip:iff-handle-depth (iff-file-handle file)))

(defun read-chunk-bytes (file vector &optional num-bytes)
  "Read up to NUM-BYTES bytes (default: VECTOR's length) of the current
chunk's data into VECTOR, a vector of (UNSIGNED-BYTE 8).  Returns the
number of bytes read — iffparse clips at the chunk boundary, so 0 means
the chunk is exhausted."
  (%check-open file "READ-CHUNK-BYTES")
  (let ((count (or num-bytes (length vector))))
    (check-type count (integer 0 *))
    (when (> count (length vector))
      (error "AMIGA.IFF: READ-CHUNK-BYTES: NUM-BYTES ~D exceeds the ~D-element vector"
             count (length vector)))
    (if (zerop count)
        0
        (ffi:with-foreign-alloc (buf count)
          (let ((result (ip:read-chunk-bytes (iff-file-handle file) buf count)))
            (when (minusp result)
              (error "AMIGA.IFF: READ-CHUNK-BYTES on ~A failed, error ~D: ~A"
                     (iff-file-name file) result (error-string result)))
            (ffi:peek-bytes buf vector 0 0 result)
            result)))))

;;; ================================================================
;;; Writing
;;; ================================================================

(defun push-chunk (file type id &optional (size ip:+iffsize-unknown+))
  "Open a chunk for writing on FILE (PushChunk).  TYPE and ID are
four-character strings or integers; TYPE is the form type for \"FORM\" /
\"LIST\" / \"CAT \" chunks and NIL (null) for plain data chunks.  With
the default SIZE (IFFSIZE_UNKNOWN, -1), POP-CHUNK seeks back and patches
the real size.  Returns T."
  (%check-open file "PUSH-CHUNK")
  (let ((err (ip:push-chunk (iff-file-handle file)
                            (%id type "PUSH-CHUNK") (%id id "PUSH-CHUNK")
                            size)))
    (unless (zerop err)
      (error "AMIGA.IFF: PUSH-CHUNK on ~A failed, error ~D: ~A"
             (iff-file-name file) err (error-string err))))
  t)

(defun pop-chunk (file)
  "Close the chunk PUSH-CHUNK opened last (PopChunk): write the pad
byte if the size is odd and back-patch an IFFSIZE_UNKNOWN size.
Returns T."
  (%check-open file "POP-CHUNK")
  (let ((err (ip:pop-chunk (iff-file-handle file))))
    (unless (zerop err)
      (error "AMIGA.IFF: POP-CHUNK on ~A failed, error ~D: ~A"
             (iff-file-name file) err (error-string err))))
  t)

(defun write-chunk-bytes (file data &optional num-bytes)
  "Write NUM-BYTES bytes (default: all) of DATA — a vector of
(UNSIGNED-BYTE 8) or a string of 8-bit characters — into the current
chunk (WriteChunkBytes).  Returns the number of bytes written."
  (%check-open file "WRITE-CHUNK-BYTES")
  (let ((count (or num-bytes (length data))))
    (check-type count (integer 0 *))
    (when (> count (length data))
      (error "AMIGA.IFF: WRITE-CHUNK-BYTES: NUM-BYTES ~D exceeds the ~D-element data"
             count (length data)))
    (if (zerop count)
        0
        (ffi:with-foreign-alloc (buf count)
          (ffi:poke-bytes buf data 0 0 count)
          (let ((result (ip:write-chunk-bytes (iff-file-handle file) buf count)))
            (when (minusp result)
              (error "AMIGA.IFF: WRITE-CHUNK-BYTES on ~A failed, error ~D: ~A"
                     (iff-file-name file) result (error-string result)))
            result)))))

;;; ================================================================
;;; MAP-CHUNKS and SIFT — the sift.c program
;;; ================================================================

(defun map-chunks (function source &key (clipboard-unit 0))
  "Parse SOURCE (a file name or :CLIPBOARD) with IFFPARSE_RAWSTEP and
call FUNCTION with (ID TYPE SIZE DEPTH) for every chunk entered — the
loop at the heart of sift.c.  Returns the number of chunks.  A parse
error signals, carrying the ERROR-STRING text."
  (with-iff (file source :direction :read :clipboard-unit clipboard-unit)
    (loop with count = 0
          for step = (parse-step file)
          until (eq step :eof)
          do (when (eq step :chunk)
               (multiple-value-bind (id type size) (current-chunk file)
                 (when id
                   (funcall function id type size (iff-depth file))
                   (incf count))))
          finally (return count))))

(defun sift (source &key (stream *standard-output*) (clipboard-unit 0))
  "Print an IFFCheck-like listing of SOURCE (a file name, or :CLIPBOARD
for the C sift's -c) to STREAM — the NDK sift program:

    . FORM 64 ILBM
    . . BMHD 20 ILBM
    . . BODY 10 ILBM
    File scan complete.

Nested chunks are indented one \". \" per depth level.  Returns the
number of chunks.  A parse error signals instead of printing the C's
\"File scan aborted\" line."
  (let ((count (map-chunks
                (lambda (id type size depth)
                  (dotimes (i depth) (write-string ". " stream))
                  (format stream "~A ~D ~A~%"
                          (id-string id) size (id-string type)))
                source :clipboard-unit clipboard-unit)))
    (format stream "File scan complete.~%")
    count))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/iff")
