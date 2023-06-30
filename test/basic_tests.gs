
(defmacro (test name & body)
  `(define (~name)
     ~@body))

(defmacro (assert-eq? lhs rhs)
  `(let ((lhsv ~lhs)
         (rhsv ~rhs))
     (when (not (eq? lhsv rhsv))
       (raise
        (list "Not equal"
              (list 'LHS: '~lhs '= lhsv)
              (list 'RHS: '~rhs '= rhsv))))))

(test
 simple

 (assert-eq? 'a 'a)
 (assert-eq? (+ 1 1) 2))

(define (main)
  (simple))
