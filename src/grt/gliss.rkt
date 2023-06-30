#lang racket/base

;; REPL with:
;; racket -nyit ./gliss.rkt -f ./gstd.gs

(require (for-syntax syntax/parse
                     racket/base
                     syntax/transformer)
         racket/pretty
         racket/string)

(provide symbol-set-value!
         symbol-set-macro!
         symbol-macro-value
         vm-check
         eq? gensym
         box unbox begin
         cons car cdr symbol?
         list? true false
         apply number? string? char?
         eof string->number

         char-whitespace?
         char->integer

         < <= > >= =
         + - / * modulo
         bitwise-and bitwise-ior
         arithmetic-shift

         open-file write-file

         list->string string->list string=?
         string-prefix? string-ref
         substring string-length
         symbol->bytestring

         program-args

         raise dbg nil
         (rename-out
          [let* let]
          [gl-if if]
          [gl-lambda lambda]
          [gl-quote quote]
          [gl-eval eval]
          [string->symbol intern]
          [set-box! box-set!]
          [make-bytes new-bytestring]
          [bytes-length bytestring-length]
          [bytes-copy! bytestring-copy!]
          [bytes-set! bytestring-set!]
          [string->bytes/utf-8 string->bytestring]))

;; implementation details
(provide (rename-out
          [gl-app #%app])
         define-syntax
         #%top-interaction
         #%datum
         #%top
         (for-syntax #%datum))

(define-syntax true (make-variable-like-transformer #'#t))
(define-syntax false (make-variable-like-transformer #'#f))
(define-syntax nil (make-variable-like-transformer #''()))

(define gliss-readtable
  (make-readtable
   #f
   #\~ #\, #f
   #\, #\space #f))

(current-readtable gliss-readtable)
(current-print pretty-print-handler)

(define (dbg x)
  (writeln x)
  x)

(define (symbol-set-value! sym value)
  (namespace-set-variable-value! sym value)
  sym)

(define (symbol-macro-name sym)
  (string->symbol (string-append (symbol->string sym) "/macro")))

(define (gl-eval expr)
  (eval expr))

(define (symbol-set-macro! sym macro?)
  (define sentinel (box nil))
  (define value (namespace-variable-value sym #t (lambda () sentinel)))
  (unless (eq? sentinel value)
    (when macro?
      (define (stx-transformer stx)
        (datum->syntax stx (apply value (cdr (syntax->datum stx))) stx))
      (eval `(define-syntax ,sym ,stx-transformer))
      (namespace-set-variable-value! (symbol-macro-name sym) value))))

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
     #:with lambda-expr (syntax/loc stx
                          (lambda params.as-rkt
                            body ...))
     (syntax/loc stx
       (letrec (({~? name fn} lambda-expr))
         {~? name fn}))]))

(define-syntax (gl-app stx)
  (syntax-parse stx
    [(_) (syntax/loc stx '())]
    [(_ . tl) (syntax/loc stx (#%app . tl))]))

(define-syntax (gl-quote stx)
  (syntax-parse stx
    #:literals (true false nil)
    [(_ true) (syntax/loc stx '#true)]
    [(_ false) (syntax/loc stx '#false)]
    [(_ nil) (syntax/loc stx '())]
    [(_ x) (syntax/loc stx 'x)]))

(define (vm-check sym)
  (eq? sym 'racket))

(define (symbol->bytestring sym)
  (string->bytes/utf-8 (symbol->string sym)))

(define (program-args)
  (vector->list (current-command-line-arguments)))

(define (open-file file-name)
   (local-require racket/base racket/port)
   (box (call-with-input-file* file-name (λ (f) (port->list read-char f)))))

 (define (write-file file-name data)
   (local-require racket/base racket/port)
   (call-with-output-file*
     file-name
     (λ (f) (write-bytes data f))
     #:exists 'replace))
