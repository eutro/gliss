(test
 scope-tests

 (assert-eq? 'a (call-in-new-scope (lambda () (list 1 2 3) 'a)))
 (assert-fn string=? "abc" (call-in-new-scope (lambda () (list->string (list #\a #\b #\c)))))

 (assert-eq? 5 (call-in-new-scope + 1 2 2))

 (call-in-new-scope
  (lambda ()
    (let ((bx (box nil)))
      (dbg-dump-obj bx)
      (call-in-new-scope
       run!
       (lambda (x)
         (box-swap! bx rcons x)
         (dbg-dump-obj bx))
       (list 1 2 3))

      (assert-fn (comp (partial all identity)
                       (partial map eq?))
                 '(3 2 1)
                 (unbox bx)))))

 ;;
 )

(define (main)
  (call-in-new-scope scope-tests))
