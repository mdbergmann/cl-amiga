;;; image-trunc.lisp -- write the first 200 bytes of the image-save leg's
;;; image to build/amiga/clamiga-trunc.img: a header that stages, a payload
;;; that is not there.  call-on-ustartup then starts clamiga from it (and
;;; from an arena that cannot be allocated) with CLAMIGA_MEM_DIAG=1: both
;;; starts must fail with their message and hand every byte back -- AmigaOS
;;; does not reclaim what a failed start forgets (a Clamacs start whose heap
;;; did not fit lost 2.5 MB per attempt).
(with-open-file (in "CLAmiga:build/amiga/clamiga-test.img"
                    :element-type '(unsigned-byte 8))
  (let ((buf (make-array 200 :element-type '(unsigned-byte 8))))
    (read-sequence buf in)
    (with-open-file (out "CLAmiga:build/amiga/clamiga-trunc.img"
                         :direction :output :if-exists :supersede
                         :element-type '(unsigned-byte 8))
      (write-sequence buf out))))
(format t "IMAGE-TRUNC-WRITTEN~%")
