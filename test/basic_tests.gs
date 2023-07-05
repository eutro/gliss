(test
 simple-tests

 (assert-eq? 'a 'a)
 (assert-eq? (+ 1 1) 2)
 (assert-eq? (- 1) -1)
 (assert-eq? ((comp - - + - + -) 1) 1)
 (assert-eq? (- 5 2) 3)

 (assert-eq? (let ((f
                    (comp
                     (lambda (x) (* x 2))
                     (lambda (x) (+ x 2)))))
               (f 1))
             (* (+ 1 2) 2))

 (assert-eq? (modulo 10 3) 1)
 (assert-eq? (modulo -10 3) 2)

 (assert-eq? (modulo 10 -3) -2)
 (assert-eq? (modulo -10 -3) -1)

 (assert-eq? (- (modulo 41 -4)) 3)
 (assert-eq? (modulo 12 -4) 0)

 (assert-eq? (dec 4) 3)
 (assert-eq? (dec 0) -1)

 (assert-eq? 65 (char->integer #\A))
 (assert-eq? 10 (char->integer #\newline))
 (assert-eq? 32 (char->integer #\space))

 (assert-fn string=? "foo" "foo")
 (assert-fn string=? (list->string '(#\f #\o #\o)) "foo")

 (assert-eq? nil (run! (lambda (x) x) '(1 2 3 4)))

 (assert-fn (comp (partial all identity)
                  (partial map eq?))
            '(1 2 3)
            (list 1 2 3))

 (assert-eq? 1 (count '(5)))
 (assert-eq? 0 (count '()))

 (assert-eq? 4 (arithmetic-shift 1 2))
 (assert-eq? (bitwise-and 4 255) 4)

 ;;
 )

(test
 bytevector-tests

 (let ((bvec (new-bytevector 0)))
   (assert-eq? (bytevector-length bvec) 0)
   (bytevector-push! bvec 1)
   (assert-eq? (bytevector-length bvec) 1)
   (bytevector-push! bvec 2 3 4 5)
   (assert-eq? (bytevector-length bvec) 5)

   (let ((bs (bytevector->bytestring bvec)))
     (assert-eq? 1 (bytestring-ref bs 0))
     (assert-eq? 3 (bytestring-ref bs 2))
     (assert-eq? 5 (bytestring-ref bs 4)))))

(define (main)
  (simple-tests)
  (bytevector-tests))
