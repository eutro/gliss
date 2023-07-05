(define (main)
  (let ((prog-args (program-args))
        (_ (when (or (nil? prog-args) (nil? (cdr prog-args)))
             (raise "Usage: glissc <out-file> [in-files ...]")))
        (src-exprs
         (call-in-new-scope
          apply
          concat
          (map
           (comp read-all open-file)
           (cdr prog-args))))
        (iw (new-image-writer))
        (start (bytecomp! iw (call-in-new-scope apply compile-program src-exprs)))
        (_ (image-writer-set-start! iw start))
        (bv (new-bytevector 0)))
    (image-writer->bytes iw bv)
    (write-file (car prog-args) (bytevector->bytestring bv))))
