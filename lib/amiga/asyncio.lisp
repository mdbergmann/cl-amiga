;;; amiga/asyncio.lisp — double-buffered asynchronous file I/O
;;;
;;; Loaded via (require "amiga/asyncio").
;;;
;;; A Common Lisp port of the NDK 3.1 AsynchIO package (Martin
;;; Taillefer's asyncio.c, Examples1/AsynchIO on the Amiga Developer CD):
;;; file I/O that overlaps computation with disk transfers by talking to
;;; the filesystem handler directly at the DOS *packet* level.  Two
;;; buffers alternate: while the program consumes (or fills) one, an
;;; ACTION_READ / ACTION_WRITE packet for the other is in flight at the
;;; handler — sent with exec PutMsg, collected with WaitPort — so the
;;; filesystem reads ahead (or writes behind) while Lisp code runs.
;;;
;;; What a C program builds by hand is built here with FFI pokes into one
;;; AllocVec block: an embedded reply MsgPort using the classic
;;; PA_IGNORE / SIGB_SINGLE trick (no signal bit is allocated; the port
;;; is switched to PA_SIGNAL on the task's one-shot signal only while
;;; actually waiting), a hand-initialised struct StandardPacket whose
;;; dp_Arg1 is the file handle's fh_Arg1, and the two DMA-friendly
;;; 16-byte-aligned buffers, rounded up to the device's block size.
;;;
;;;   OPEN-ASYNC       Open + Lock/ParentOfFH + Info (block size), first
;;;                    read packet already in flight before it returns
;;;   READ-ASYNC / WRITE-ASYNC    bulk transfer to/from a foreign pointer,
;;;                    an integer address, or a Lisp byte vector
;;;   READ-BYTE-ASYNC / READ-CHAR-ASYNC / READ-LINE-ASYNC
;;;   WRITE-BYTE-ASYNC / WRITE-CHAR-ASYNC / WRITE-STRING-ASYNC / WRITE-LINE-ASYNC
;;;   SEEK-ASYNC       :START / :CURRENT / :END, returns the previous position
;;;   CLOSE-ASYNC      flushes the write buffer, frees everything
;;;   WITH-ASYNC-FILE  OPEN-ASYNC + UNWIND-PROTECTed CLOSE-ASYNC
;;;
;;; Deviations from the C original, on purpose:
;;;   - I/O errors signal a Lisp ERROR (with the DOS error code) instead
;;;     of returning -1; the C original's ErrorReport() retry requester
;;;     is omitted — a requester would hang unattended runs.
;;;   - SeekAsync's buffer-window reuse (the part the NDK 3.1 README
;;;     warns about; rewritten in the NDK 3.5 version) is replaced by a
;;;     simpler discard-and-restart: every seek re-aims the read-ahead at
;;;     the block-aligned target.  Correct for every case, merely slower
;;;     for short back-seeks inside the buffered window.
;;;
;;; Constraints inherited from the technique:
;;;   - The reply port's SigTask is the opening task: an async file must
;;;     be used from the thread that opened it.
;;;   - The file name cannot be an interactive stream (CON:, RAW:, *);
;;;     NIL: works (writes are swallowed, reads return EOF), matching
;;;     the C original.
;;;   - Always CLOSE-ASYNC (or use WITH-ASYNC-FILE): the buffers live in
;;;     AllocVec memory the GC knows nothing about.
;;;
;;; See tests/amiga/test-asyncio.lisp for the executable specification
;;; and examples/amiga/asyncio/copyfile.lisp for a demo.

;; compile-time too: COMPILE-FILE (the host builds the lib/amiga FASLs) must see
;; these packages at read time, not only LOAD.
(eval-when (:compile-toplevel :load-toplevel :execute)
  (require "amiga/ffi")
  (require "amiga/raw/exec")
  (require "amiga/raw/dos"))

(defpackage "AMIGA.ASYNCIO"
  (:use "CL" "FFI" "AMIGA.FFI")
  (:local-nicknames ("EXEC" "AMIGA.RAW.EXEC")
                    ("DOS"  "AMIGA.RAW.DOS"))
  (:export
   "AVAILABLE-P"
   "ASYNC-FILE" "ASYNC-FILE-P"
   "OPEN-ASYNC" "CLOSE-ASYNC" "WITH-ASYNC-FILE"
   "READ-ASYNC" "WRITE-ASYNC" "SEEK-ASYNC"
   "READ-BYTE-ASYNC" "READ-CHAR-ASYNC" "READ-LINE-ASYNC"
   "WRITE-BYTE-ASYNC" "WRITE-CHAR-ASYNC"
   "WRITE-STRING-ASYNC" "WRITE-LINE-ASYNC"))

(in-package "AMIGA.ASYNCIO")

(defun available-p ()
  "True when DOS packets can be spoken on this system: an AmigaOS or
MorphOS runtime with dos.library open.  NIL on other hosts, where the
module still loads for compilation and the portable checks."
  (and (member :amigaos *features*) dos:*dos-base* t))

;;; ================================================================
;;; The AllocVec block — offsets of everything the OS looks at
;;; ================================================================

;;; One MEMF_PUBLIC block per open file, laid out like the C AsyncFile
;;; (exec/ports.i and dos/dosextens.i offsets, cross-checked against the
;;; generated MSG-PORT / MESSAGE / STANDARD-PACKET struct rows in
;;; lib/amiga/raw/):
;;;
;;;     +0   struct MsgPort   reply port (embedded, no CreateMsgPort)
;;;     +36  struct StandardPacket
;;;            +36 sp_Msg    +44 ln_Type  +46 ln_Name  +54 mn_Length
;;;            +56 sp_Pkt    +60 dp_Port  +64 dp_Type  +68 dp_Res1
;;;                          +72 dp_Res2  +76 dp_Arg1  +80 dp_Arg2  +84 dp_Arg3
;;;     +104 4-byte scratch for the byte/char functions
;;;     +112 the two buffers, first one aligned up to 16 bytes

(defconstant +mp-ln-type+   8)          ; MsgPort.mp_Node.ln_Type
(defconstant +mp-flags+    14)          ; MsgPort.mp_Flags
(defconstant +mp-sigbit+   15)          ; MsgPort.mp_SigBit
(defconstant +mp-sigtask+  16)          ; MsgPort.mp_SigTask
(defconstant +mp-msglist+  20)          ; MsgPort.mp_MsgList (struct List)
(defconstant +sp-msg+      36)          ; StandardPacket.sp_Msg
(defconstant +sp-ln-type+  44)          ;   .ln_Type
(defconstant +sp-ln-name+  46)          ;   .ln_Name -> &sp_Pkt
(defconstant +sp-mn-length+ 54)         ;   .mn_Length (UWORD)
(defconstant +sp-pkt+      56)          ; StandardPacket.sp_Pkt
(defconstant +dp-port+     60)          ;   .dp_Port -> the reply port
(defconstant +dp-type+     64)          ;   .dp_Type ACTION_READ / ACTION_WRITE
(defconstant +dp-res1+     68)          ;   .dp_Res1 byte count / error flag
(defconstant +dp-res2+     72)          ;   .dp_Res2 DOS error code
(defconstant +dp-arg1+     76)          ;   .dp_Arg1 fh_Arg1
(defconstant +dp-arg2+     80)          ;   .dp_Arg2 buffer address
(defconstant +dp-arg3+     84)          ;   .dp_Arg3 buffer size
(defconstant +scratch+    104)          ; one longword for byte/char I/O
(defconstant +header-size+ 112)         ; buffers start here (16-aligned)
(defconstant +standard-packet-size+ 68) ; sizeof(struct StandardPacket)

;;; ================================================================
;;; The ASYNC-FILE handle
;;; ================================================================

;;; The C original keeps its counters inside the AllocVec block; here the
;;; OS-visible parts stay foreign and the bookkeeping lives in a Lisp
;;; struct.  OFFSET, BUF0 and BUF1 are absolute addresses (fixnums) so
;;; they can be handed to CopyMem directly; MEM is the one foreign
;;; pointer, all pokes are MEM-relative.

(defstruct (async-file (:constructor %make-async-file)
                       (:copier nil))
  name            ; the file name, for error messages
  fh              ; BPTR from Open(), an integer
  handler         ; fh_Type: the filesystem's MsgPort, or NIL for NIL:
  mem             ; foreign pointer to the AllocVec block, NIL once closed
  base            ; its address
  buf0 buf1       ; buffer addresses (16-byte aligned)
  half            ; bytes per buffer (af_BufferSize in C)
  block-size      ; device block size, for aligned seeks
  offset          ; read/write cursor, an absolute address
  bytes-left      ; bytes left in the current buffer
  current         ; 0/1: reading, the buffer in flight; writing, the one filling
  seek-offset     ; bytes to skip in the next arriving buffer (after a seek)
  pending-p       ; a packet is out at the handler
  read-mode-p)

(defun %check-open (af op)
  (unless (async-file-p af)
    (error "AMIGA.ASYNCIO: ~A: not an ASYNC-FILE: ~S" op af))
  (unless (async-file-mem af)
    (error "AMIGA.ASYNCIO: ~A: ~S is already closed" op (async-file-name af))))

(defun %io-error (af op)
  "Signal the DOS error the last packet (dp_Res2) reported."
  (error "AMIGA.ASYNCIO: ~A on ~S failed (DOS error ~D)"
         op (async-file-name af)
         (ffi:peek-i32 (async-file-mem af) +dp-res2+)))

(defun %sync-error (af op)
  "Signal the DOS error a synchronous call (IoErr) reported."
  (error "AMIGA.ASYNCIO: ~A on ~S failed (DOS error ~D)"
         op (async-file-name af) (dos:io-err)))

(defun %buf (af n)
  (if (zerop n) (async-file-buf0 af) (async-file-buf1 af)))

;;; ================================================================
;;; The packet engine — SendPacket / WaitPacket of the C original
;;; ================================================================

(defun %send-packet (af buffer-addr)
  "Send the file's ACTION packet to the handler, aimed at BUFFER-ADDR.
Asynchronous: PutMsg returns immediately, the filesystem works while
Lisp code runs."
  (let ((mem (async-file-mem af)))
    (ffi:poke-u32 mem (async-file-base af) +dp-port+)   ; reply to our port
    (ffi:poke-u32 mem buffer-addr +dp-arg2+)
    (exec:put-msg (async-file-handler af) (ffi:pointer+ mem +sp-msg+))
    (setf (async-file-pending-p af) t)))

(defun %wait-packet (af)
  "Wait for the pending packet and return its dp_Res1 (bytes moved, 0 at
EOF, negative on error — dp_Res2 has the DOS code).  With no packet
pending, return the previous packet's result, like the C original: an
error sticks to the file.  The PA_SIGNAL/PA_IGNORE dance around WaitPort
is the SIGB_SINGLE trick — the port only signals while we really wait."
  (let ((mem (async-file-mem af)))
    (cond ((async-file-pending-p af)
           (ffi:poke-u8 mem exec:+pa-signal+ +mp-flags+)
           ;; Ours is the only packet that can arrive at this port, so
           ;; Remove(WaitPort()) is safe, as in the C original.
           (exec:remove (exec:wait-port mem))
           (ffi:poke-u8 mem exec:+pa-ignore+ +mp-flags+)
           (setf (async-file-pending-p af) nil)
           (ffi:peek-i32 mem +dp-res1+))
          (t
           (dos:set-io-err (ffi:peek-i32 mem +dp-res2+))
           (ffi:peek-i32 mem +dp-res1+)))))

;;; ================================================================
;;; Open / close
;;; ================================================================

(defun open-async (file-name mode &key (buffer-size 8192))
  "Open FILE-NAME for asynchronous I/O and return an ASYNC-FILE.
MODE is :READ (existing file, read-ahead starts before this returns),
:WRITE (create, replacing any existing file) or :APPEND (write at the
end, creating if needed).  BUFFER-SIZE is the total for both buffers,
rounded up to twice the device's block size; 8192 is a good default.
Signals an error (with the DOS error code) if the file cannot be opened."
  (unless (available-p)
    (error "AMIGA.ASYNCIO: only available on AmigaOS/MorphOS (dos.library packets)"))
  (check-type file-name string)
  (check-type buffer-size (integer 128 *))
  (let ((handle 0) (lock 0))
    (ffi:with-foreign-string (fname file-name)
      (ecase mode
        (:read
         (setf handle (dos:open fname dos:+mode-oldfile+))
         (unless (zerop handle)
           (setf lock (dos:lock fname dos:+shared-lock+))))
        (:write
         (setf handle (dos:open fname dos:+mode-newfile+)))
        (:append
         (setf handle (dos:open fname dos:+mode-readwrite+))
         (when (and (not (zerop handle))
                    (minusp (dos:seek handle 0 dos:+offset-end+)))
           (dos:close handle)
           (setf handle 0))))
      ;; For the write modes a lock on the same device comes from the
      ;; file's parent (DupLockFromFH does not work on write handles).
      (when (and (not (eq mode :read)) (not (zerop handle)))
        (setf lock (dos:parent-of-fh handle))))
    (when (zerop handle)
      (error "AMIGA.ASYNCIO: cannot open ~S for ~S (DOS error ~D)"
             file-name mode (dos:io-err)))
    ;; Round the buffers up to a multiple of the device's block size:
    ;; block-aligned transfers maximise DMA efficiency.
    (let ((block-size 512))
      (unless (zerop lock)
        (ffi:with-foreign-alloc (info dos:*info-data-size*)
          (unless (zerop (dos:info lock info))
            (let ((bpb (dos:info-data-bytes-per-block info)))
              (when (plusp bpb)
                (setf block-size bpb
                      buffer-size (* (ceiling buffer-size (* 2 bpb))
                                     2 bpb))))))
        (dos:un-lock lock))
      (let* ((half (floor buffer-size 2))
             (mem (exec:alloc-vec (+ +header-size+ buffer-size 15)
                                  (logior exec:+memf-public+ exec:+memf-clear+))))
        (unless mem
          (dos:close handle)
          (error "AMIGA.ASYNCIO: cannot allocate ~D bytes for ~S"
                 (+ +header-size+ buffer-size 15) file-name))
        (let* ((base (ffi:foreign-pointer-address mem))
               (buf0 (logand (+ base +header-size+ 15) #xFFFFFFF0))
               (fh-struct (ffi:make-foreign-pointer (ash handle 2)))  ; BADDR
               (af (%make-async-file
                    :name file-name :fh handle
                    :handler (dos:file-handle-type fh-struct)
                    :mem mem :base base
                    :buf0 buf0 :buf1 (+ buf0 half) :half half
                    :block-size block-size
                    :offset buf0 :bytes-left 0 :current 0 :seek-offset 0
                    :pending-p nil :read-mode-p (eq mode :read))))
          ;; The reply port: type NT_MSGPORT, PA_IGNORE until we wait,
          ;; SIGB_SINGLE (no allocated signal bit), SigTask = this task,
          ;; and a NewList'ed message list.
          (ffi:poke-u8 mem exec:+nt-msgport+ +mp-ln-type+)
          (ffi:poke-u8 mem exec:+pa-ignore+ +mp-flags+)
          (ffi:poke-u8 mem exec:+sigb-single+ +mp-sigbit+)
          (ffi:poke-u32 mem (ffi:foreign-pointer-address (exec:find-task 0))
                        +mp-sigtask+)
          (ffi:poke-u32 mem (+ base +mp-msglist+ 4) +mp-msglist+)      ; lh_Head
          (ffi:poke-u32 mem 0 (+ +mp-msglist+ 4))                      ; lh_Tail
          (ffi:poke-u32 mem (+ base +mp-msglist+) (+ +mp-msglist+ 8))  ; lh_TailPred
          ;; The packet: message and DosPacket cross-linked as
          ;; dos.library's SendPkt would (ln_Name -> pkt, dp_Link -> msg).
          (ffi:poke-u8 mem exec:+nt-message+ +sp-ln-type+)
          (ffi:poke-u32 mem (+ base +sp-pkt+) +sp-ln-name+)
          (ffi:poke-u16 mem +standard-packet-size+ +sp-mn-length+)
          (ffi:poke-u32 mem (+ base +sp-msg+) +sp-pkt+)                ; dp_Link
          (ffi:poke-i32 mem (dos:file-handle-args fh-struct) +dp-arg1+)
          (ffi:poke-i32 mem half +dp-arg3+)
          (cond ((async-file-read-mode-p af)
                 ;; Read mode: the first read packet goes out NOW — by the
                 ;; time the caller asks for data, it is already in buf0.
                 (ffi:poke-i32 mem dos:+action-read+ +dp-type+)
                 (when (async-file-handler af)
                   (%send-packet af buf0)))
                (t
                 (ffi:poke-i32 mem dos:+action-write+ +dp-type+)
                 (setf (async-file-bytes-left af) half)))
          af)))))

(defun close-async (af)
  "Close AF, flushing any buffered write data, and free its OS memory.
Returns T.  Closing an already-closed file is a no-op.  A flush error is
signalled only after the file handle and memory have been released."
  (unless (async-file-p af)
    (error "AMIGA.ASYNCIO: CLOSE-ASYNC: not an ASYNC-FILE: ~S" af))
  (when (async-file-mem af)
    (let ((result (%wait-packet af))
          (err nil))
      (cond ((minusp result)
             (setf err (ffi:peek-i32 (async-file-mem af) +dp-res2+)))
            ((and (not (async-file-read-mode-p af))
                  (> (async-file-half af) (async-file-bytes-left af)))
             ;; Flush the partially filled buffer with a plain Write.
             (when (minusp (dos:write (async-file-fh af)
                                      (%buf af (async-file-current af))
                                      (- (async-file-half af)
                                         (async-file-bytes-left af))))
               (setf err (dos:io-err)))))
      (dos:close (async-file-fh af))
      (exec:free-vec (async-file-mem af))
      (setf (async-file-mem af) nil)
      (when err
        (error "AMIGA.ASYNCIO: CLOSE-ASYNC: flushing ~S failed (DOS error ~D)"
               (async-file-name af) err))))
  t)

(defmacro with-async-file ((var file-name mode &rest open-args) &body body)
  "Bind VAR to (OPEN-ASYNC FILE-NAME MODE . OPEN-ARGS) around BODY,
closing on the way out however BODY exits."
  `(let ((,var (open-async ,file-name ,mode ,@open-args)))
     (unwind-protect (progn ,@body)
       (close-async ,var))))

;;; ================================================================
;;; Bulk transfer
;;; ================================================================

;;; The destination/source of READ-ASYNC / WRITE-ASYNC is a foreign
;;; pointer, an integer address (CopyMem moves the bytes, as in C) or a
;;; Lisp vector of (UNSIGNED-BYTE 8) (PEEK-BYTES / POKE-BYTES move them
;;; straight between the vector and the double buffers).

(defun %xfer-out (af dest vector-p pos count)
  "Copy COUNT bytes from AF's cursor to DEST at POS."
  (when (plusp count)
    (if vector-p
        (ffi:peek-bytes (async-file-mem af) dest
                        (- (async-file-offset af) (async-file-base af))
                        pos (+ pos count))
        (exec:copy-mem (async-file-offset af) (+ dest pos) count))))

(defun %xfer-in (af src vector-p pos count)
  "Copy COUNT bytes from SRC at POS to AF's cursor."
  (when (plusp count)
    (if vector-p
        (ffi:poke-bytes (async-file-mem af) src
                        (- (async-file-offset af) (async-file-base af))
                        pos (+ pos count))
        (exec:copy-mem (+ src pos) (async-file-offset af) count))))

(defun %buffer-arg (dest op)
  "Normalise DEST: (values dest-or-address vector-p)."
  (cond ((ffi:foreign-pointer-p dest)
         (values (ffi:foreign-pointer-address dest) nil))
        ((integerp dest) (values dest nil))
        ((vectorp dest) (values dest t))
        (t (error "AMIGA.ASYNCIO: ~A: buffer must be a foreign pointer, an address or a byte vector, got ~S"
                  op dest))))

(defun read-async (af dest &optional num-bytes)
  "Read up to NUM-BYTES bytes from AF into DEST (a foreign pointer, an
integer address, or a vector of (UNSIGNED-BYTE 8), which also defaults
NUM-BYTES to its length).  Returns the number of bytes read — less than
NUM-BYTES only at end of file, 0 at EOF proper.  While the caller works
on these bytes, the next buffer load is already on its way."
  (%check-open af "READ-ASYNC")
  (unless (async-file-read-mode-p af)
    (error "AMIGA.ASYNCIO: READ-ASYNC: ~S is open for writing" (async-file-name af)))
  (when num-bytes (check-type num-bytes (integer 0 *)))
  (multiple-value-bind (dest vector-p) (%buffer-arg dest "READ-ASYNC")
    (let ((count (or num-bytes (if vector-p (length dest) nil)))
          (pos 0)
          (total 0))
      (unless count
        (error "AMIGA.ASYNCIO: READ-ASYNC: NUM-BYTES is required for a foreign destination"))
      (when (and vector-p (> count (length dest)))
        (error "AMIGA.ASYNCIO: READ-ASYNC: NUM-BYTES ~D exceeds the ~D-element vector"
               count (length dest)))
      ;; More wanted than the current buffer holds: drain it, collect the
      ;; arriving buffer, immediately re-arm the other one.
      (loop while (> count (async-file-bytes-left af)) do
        (let ((chunk (async-file-bytes-left af)))
          (%xfer-out af dest vector-p pos chunk)
          (decf count chunk) (incf pos chunk) (incf total chunk)
          (setf (async-file-bytes-left af) 0))
        (let ((arrived (%wait-packet af)))
          (when (minusp arrived) (%io-error af "READ-ASYNC"))
          (when (zerop arrived) (return-from read-async total))
          (when (async-file-handler af)
            (%send-packet af (%buf af (- 1 (async-file-current af)))))
          ;; A short file can leave a post-seek skip past the data.
          (when (> (async-file-seek-offset af) arrived)
            (setf (async-file-seek-offset af) arrived))
          (setf (async-file-offset af) (+ (%buf af (async-file-current af))
                                          (async-file-seek-offset af))
                (async-file-bytes-left af) (- arrived (async-file-seek-offset af))
                (async-file-current af) (- 1 (async-file-current af))
                (async-file-seek-offset af) 0)))
      (%xfer-out af dest vector-p pos count)
      (decf (async-file-bytes-left af) count)
      (incf (async-file-offset af) count)
      (+ total count))))

(defun write-async (af src &optional num-bytes)
  "Write NUM-BYTES bytes from SRC (a foreign pointer, an integer address,
or a vector of (UNSIGNED-BYTE 8), which also defaults NUM-BYTES to its
length) to AF.  Returns the number of bytes written.  A full buffer goes
to the filesystem asynchronously while the other buffer keeps filling."
  (%check-open af "WRITE-ASYNC")
  (when (async-file-read-mode-p af)
    (error "AMIGA.ASYNCIO: WRITE-ASYNC: ~S is open for reading" (async-file-name af)))
  (when num-bytes (check-type num-bytes (integer 0 *)))
  (multiple-value-bind (src vector-p) (%buffer-arg src "WRITE-ASYNC")
    (let ((count (or num-bytes (if vector-p (length src) nil)))
          (pos 0)
          (total 0))
      (unless count
        (error "AMIGA.ASYNCIO: WRITE-ASYNC: NUM-BYTES is required for a foreign source"))
      (when (and vector-p (> count (length src)))
        (error "AMIGA.ASYNCIO: WRITE-ASYNC: NUM-BYTES ~D exceeds the ~D-element vector"
               count (length src)))
      (loop while (> count (async-file-bytes-left af)) do
        ;; NIL: has no handler process: swallow everything, as C does.
        (unless (async-file-handler af)
          (setf (async-file-offset af) (async-file-buf0 af)
                (async-file-bytes-left af) (async-file-half af))
          (return-from write-async (+ total count)))
        (let ((chunk (async-file-bytes-left af)))
          (%xfer-in af src vector-p pos chunk)
          (decf count chunk) (incf pos chunk) (incf total chunk))
        (when (minusp (%wait-packet af)) (%io-error af "WRITE-ASYNC"))
        (%send-packet af (%buf af (async-file-current af)))
        (setf (async-file-current af) (- 1 (async-file-current af))
              (async-file-offset af) (%buf af (async-file-current af))
              (async-file-bytes-left af) (async-file-half af)))
      (%xfer-in af src vector-p pos count)
      (decf (async-file-bytes-left af) count)
      (incf (async-file-offset af) count)
      (+ total count))))

;;; ================================================================
;;; Seek
;;; ================================================================

(defun %file-size (af)
  (ffi:with-foreign-alloc (fib dos:*file-info-block-size*)
    (unless (dos:examine-fh (async-file-fh af) fib)
      (%sync-error af "SEEK-ASYNC"))
    (dos:file-info-block-size fib)))

(defun seek-async (af position whence)
  "Move AF's logical position: WHENCE is :START, :CURRENT or :END, as
DOS Seek's OFFSET_BEGINNING / OFFSET_CURRENT / OFFSET_END.  Returns the
previous logical position, like Seek.  (SEEK-ASYNC AF 0 :CURRENT) is a
position probe.  In read mode the read-ahead restarts at the target's
block boundary; in write mode the buffer is flushed first."
  (%check-open af "SEEK-ASYNC")
  (let ((arrived (%wait-packet af)))
    (when (minusp arrived) (%io-error af "SEEK-ASYNC"))
    (if (async-file-read-mode-p af)
        (let ((file-pos (dos:seek (async-file-fh af) 0 dos:+offset-current+)))
          (when (minusp file-pos) (%sync-error af "SEEK-ASYNC"))
          ;; The handler's position is past everything buffered and in
          ;; flight; subtract that (and add a pending post-seek skip) to
          ;; get the caller's logical position.
          (let* ((current (+ (- file-pos (+ (async-file-bytes-left af) arrived))
                             (async-file-seek-offset af)))
                 (target (ecase whence
                           (:start position)
                           (:current (+ current position))
                           (:end (+ (%file-size af) position))))
                 ;; Restart block-aligned: aligned reads are much faster,
                 ;; the odd head bytes are skipped via SEEK-OFFSET.
                 (aligned (* (floor target (async-file-block-size af))
                             (async-file-block-size af))))
            (when (minusp (dos:seek (async-file-fh af) (- aligned file-pos)
                                    dos:+offset-current+))
              (%sync-error af "SEEK-ASYNC"))
            (setf (async-file-seek-offset af) (- target aligned)
                  (async-file-bytes-left af) 0
                  (async-file-current af) 0
                  (async-file-offset af) (async-file-buf0 af))
            (when (async-file-handler af)
              (%send-packet af (async-file-buf0 af)))
            current))
        ;; Write mode: flush, seek synchronously, start a fresh buffer.
        (progn
          (when (> (async-file-half af) (async-file-bytes-left af))
            (when (minusp (dos:write (async-file-fh af)
                                     (%buf af (async-file-current af))
                                     (- (async-file-half af)
                                        (async-file-bytes-left af))))
              (%sync-error af "SEEK-ASYNC")))
          (let ((previous (dos:seek (async-file-fh af) position
                                    (ecase whence
                                      (:start dos:+offset-beginning+)
                                      (:current dos:+offset-current+)
                                      (:end dos:+offset-end+)))))
            (when (minusp previous) (%sync-error af "SEEK-ASYNC"))
            (setf (async-file-bytes-left af) (async-file-half af)
                  (async-file-current af) 0
                  (async-file-offset af) (async-file-buf0 af))
            previous)))))

;;; ================================================================
;;; Byte / character convenience
;;; ================================================================

(defun read-byte-async (af)
  "Return the next byte of AF as an integer, or NIL at end of file.
The fast path is a single PEEK-U8 from the current buffer."
  (%check-open af "READ-BYTE-ASYNC")
  (cond ((plusp (async-file-bytes-left af))
         (let ((b (ffi:peek-u8 (async-file-mem af)
                               (- (async-file-offset af) (async-file-base af)))))
           (decf (async-file-bytes-left af))
           (incf (async-file-offset af))
           b))
        ((plusp (read-async af (+ (async-file-base af) +scratch+) 1))
         (ffi:peek-u8 (async-file-mem af) +scratch+))
        (t nil)))

(defun read-char-async (af)
  "Return the next byte of AF as a CHARACTER, or NIL at end of file."
  (let ((b (read-byte-async af)))
    (and b (code-char b))))

(defun read-line-async (af)
  "Return the next line of AF (without the newline) as a string, or NIL
at end of file."
  (let ((b (read-byte-async af)))
    (when b
      (let ((line (make-array 64 :element-type 'character
                                 :adjustable t :fill-pointer 0)))
        (loop while (and b (/= b 10)) do
          (vector-push-extend (code-char b) line)
          (setf b (read-byte-async af)))
        (coerce line 'simple-string)))))

(defun write-byte-async (af byte)
  "Write BYTE (an integer 0..255) to AF.  Returns 1."
  (%check-open af "WRITE-BYTE-ASYNC")
  (check-type byte (integer 0 255))
  (cond ((plusp (async-file-bytes-left af))
         (ffi:poke-u8 (async-file-mem af) byte
                      (- (async-file-offset af) (async-file-base af)))
         (decf (async-file-bytes-left af))
         (incf (async-file-offset af))
         1)
        (t
         (ffi:poke-u8 (async-file-mem af) byte +scratch+)
         (write-async af (+ (async-file-base af) +scratch+) 1))))

(defun write-char-async (af char)
  "Write CHAR (a character with a code below 256) to AF.  Returns 1."
  (let ((code (char-code char)))
    (unless (< code 256)
      (error "AMIGA.ASYNCIO: WRITE-CHAR-ASYNC: ~S is not an 8-bit character" char))
    (write-byte-async af code)))

(defun write-string-async (af string)
  "Write STRING's characters (all with codes below 256) to AF.
Returns the number of bytes written."
  (loop for c across string do (write-char-async af c))
  (length string))

(defun write-line-async (af string)
  "WRITE-STRING-ASYNC plus a newline.  Returns the number of bytes written."
  (+ (write-string-async af string)
     (write-byte-async af 10)))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/asyncio")
