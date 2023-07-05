(define (main)
  (let ((prog-args (program-args))
        (_ (when (or (nil? prog-args))
             (raise "Usage: glissi [in-files ...]")))
        (src-exprs
         (call-in-new-scope
          apply
          concat
          (map
           (comp read-all open-file)
           prog-args))))
    (apply compile-program src-exprs)))
