;; racket -nyt ./gliss.rkt -f ./gstd.gs -f ./grt.gs -f ./link.gs -- ./link.gi ./gstd.gs ./grt.gs ./link.gs

(define src-exprs
  (apply
   concat
   (map
    (comp read-all open-file)
    (cdr (program-args)))))

(define iw (new-image-writer))
(define start (bytecomp! iw (the-pipeline (cons 'begin src-exprs))))
(image-writer-set-start! iw start)
(define bv (new-bytevector 0))
(image-writer->bytes iw bv)

(write-file (car (program-args)) (bytevector->bytestring bv))
