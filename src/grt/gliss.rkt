#lang racket/base

;; REPL with:
;; racket -nyit ./gliss.rkt -f ./gstd.gs

(require (for-syntax syntax/parse
                     racket/base))

(provide symbol-set-value!
         symbol-set-macro!
         symbol-macro-value
         quote eq? gensym
         box unbox + begin
         cons car cdr symbol?
         list? true false
         apply integer? string?
         raise dbg
         (rename-out
          [let* let]
          [gl-if if]
          [gl-lambda lambda]
          [set-box! box-set!]
          [null nil]
          [gl-app #%app])

         define-syntax
         #%top-interaction
         #%datum
         #%top
         (for-syntax #%datum))

(define true #t)
(define false #f)

(define gliss-readtable
  (make-readtable
   #f
   #\~ #\, #f
   #\, #\space #f))

(current-readtable gliss-readtable)

(define (dbg x)
  (writeln x)
  x)

(define (symbol-set-value! sym value)
  (namespace-set-variable-value! sym value)
  sym)

(define (symbol-macro-name sym)
  (string->symbol (string-append (symbol->string sym) "/macro")))

(define (symbol-set-macro! sym macro?)
  (define value (namespace-variable-value sym))
  (when macro?
    (define (stx-transformer stx)
      (datum->syntax stx (apply value (cdr (syntax->datum stx)))))
    (eval `(define-syntax ,sym ,stx-transformer))
    (namespace-set-variable-value! (symbol-macro-name sym) value)))

(define (symbol-macro-value sym)
  (namespace-variable-value (symbol-macro-name sym) #t (lambda () null)))

(define-syntax-rule (gl-if prd thn els)
  (let ([k prd])
    (if (or (null? k)
            (eq? #f k))
        els
        thn)))

(begin-for-syntax
  (define-syntax-class gliss-params
    #:attributes (as-rkt)
    #:datum-literals (&)
    (pattern (& name:id) #:attr as-rkt #'name)
    (pattern (param-name:id tl ...)
             #:with tl-params:gliss-params #'(tl ...)
             #:attr as-rkt #'(param-name . tl-params.as-rkt))
    (pattern () #:attr as-rkt #'())))

(define-syntax (gl-lambda stx)
  (syntax-parse stx
    [(_ {~optional name:id} params:gliss-params
        body ...)
     (syntax/loc stx
       (letrec (({~? name fn}
                 (lambda params.as-rkt
                   body ...)))
         {~? name fn}))]))

(define-syntax (gl-app stx)
  (syntax-parse stx
    [(_) (syntax/loc stx '())]
    [(_ . tl) (syntax/loc stx (#%app . tl))]))
