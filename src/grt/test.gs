(define src-exprs (read-all (open-file "gstd.gs")))
(define iw (new-image-writer))
(define start (bytecomp! iw (the-pipeline (cons 'begin src-exprs))))
(image-writer-set-start! iw start)
(define bv (new-bytevector 0))
(image-writer->bytes iw bv)

(define (write)
  (write-file "gstd.gi" (bytevector->bytestring bv)))
