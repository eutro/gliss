(test
  reader-tests

  (assert-eq? (read (box '(#\a #\b #\c)))
              'abc)
  ;;
  )

(define (main)
  (reader-tests))
