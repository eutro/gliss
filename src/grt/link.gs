;; racket -nyt ./gliss.rkt -f ./gstd.gs -f ./grt.gs -f ./link.gs -e "(main)" -- ./link.gi ./gstd.gs ./grt.gs ./link.gs

(define (main)
  (let ((src-exprs
         (apply
          concat
          (map
           (comp read-all open-file)
           (cdr (program-args)))))
        (iw (new-image-writer))
        (start (bytecomp! iw (apply the-pipeline src-exprs)))
        (_ (image-writer-set-start! iw start))
        (bv (new-bytevector 0)))
    (image-writer->bytes iw bv)
    (write-file (car (program-args)) (bytevector->bytestring bv))))
