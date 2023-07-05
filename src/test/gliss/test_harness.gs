(defmacro (test name & body)
  `(define (~name)
     ~@body))

(defmacro (assert-fn fn lhs rhs)
  `(let ((lhsv ~lhs)
         (rhsv ~rhs))
     (when (not (~fn lhsv rhsv))
       (raise
        (list "Assertion failed"
              '(~fn ~lhs ~rhs)
              (list '~fn lhsv rhsv))))))

(defmacro (assert-eq? lhs rhs)
  `(assert-fn eq? ~lhs ~rhs))
