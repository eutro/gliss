;; VM primitives:
;; symbol-macro-value
;; raise

;; gstd linked before this

;; passes:
;; read -> expand -> compile -> bytecomp

;; read a datum from a stream
(define (read s)
  (case (skip-ws! s)
    ((#false) eof)
    ((#\() (iter-next! s) (read-list-tail s))
    ((#\") (iter-next! s) (read-string-tail s))
    ((#\') (iter-next! s) (list 'quote (read s)))
    ((#\`) (iter-next! s) (list 'quasiquote (read s)))
    ((#\~)
     (iter-next! s)
     (list (if (eq? #\@ (iter-peek s))
               (begin (iter-next! s) 'unquote-splicing)
               'unquote)
           (read s)))
    (else (read-token s))))

(define (read-all s)
  (let ((nxt (read s)))
    (if (eq? eof nxt)
        nil
        (cons nxt (read-all s)))))

(define (skip-ws! s)
  (let ((c (iter-peek s)))
    (cond
      ((not c) false)
      ((char-whitespace? c)
       (iter-next! s)
       (skip-ws! s))
      ((eq? c #\;)
       (line-comment s)
       (skip-ws! s))
      (else c))))

(define (line-comment s)
  (let ((c (iter-next! s)))
    (when (and c (not (eq? c #\newline)))
      (line-comment s))))

(define (read-token s)
  (let ((tok-chars (read-token-0 s))
        (tok (list->string tok-chars)))
    (cond
      ((or (string=? tok "true")
           (string=? tok "#true")) true)
      ((or (string=? tok "false")
           (string=? tok "#false"))
       false)
      ((string=? tok "nil") nil)
      ((string-prefix? tok "#\\")
       (let ((char-str (substring tok 2)))
         (cond
           ((= 1 (string-length char-str)) (string-ref char-str 0))
           ((string=? char-str "newline") #\newline)
           (else (raise (list "Unrecognised character" char-str))))))
      ((let ((c (string-ref tok 0)))
         (or (char-digit? c)
             (and
              (< 1 (string-length tok))
              (or (eq? #\- c)
                  (eq? #\+ c))
              (char-digit? (string-ref tok 1)))))
       (string->number tok))
      (else (intern tok)))))

(define (read-token-0 s)
  (let ((c (iter-peek s)))
    (if (or (not c)
            (char-whitespace? c)
            (eq? c #\))
            (eq? c #\()
            (eq? c #\"))
        nil
        (cons
         (iter-next! s)
         (if (eq? c #\\)
             (cons
              (or
               (iter-next! s)
               (raise "Unexpected EOF reading token"))
              (read-token-0 s))
             (read-token-0 s))))))

(define (read-list-tail s)
  (case (skip-ws! s)
    ((#false) (raise "Unclosed list"))
    ((#\)) (iter-next! s) nil)
    (else (cons (read s) (read-list-tail s)))))

(define (read-string-tail s)
  (let ((string-chars (read-string-tail-0 s)))
    (list->string string-chars)))

(define (read-string-tail-0 s)
  (let ((c (iter-next! s)))
    (case c
      ((#false) (raise "Unclosed string"))
      ((#\") nil)
      ((#\\)
       (let ((e (iter-next! s)))
         (cons e (read-string-tail-0 s))))
      (else (cons c (read-string-tail-0 s))))))

;; expand a datum
(define (expand form)
  (if (and form (list? form))
    (expand-list form)
    form))

(define (expand-list list-form)
  (let ((hd (car list-form))
        (is-sym (symbol? hd))
        (macro
            (and
             is-sym
             (case hd
               ((if)
                (lambda ex-if (prd thn & els)
                  (list
                   'if
                   (expand prd)
                   (expand thn)
                   (or (and els (expand (car els)))
                       nil))))
               ((let)
                (lambda ex-let (spec & body)
                  (list*
                   'let
                   (map
                    (lambda ex-binding (binding)
                      (let ((name (car binding))
                            (value (cdr binding)))
                        (when (not (symbol? name))
                          (raise "Let binding must be a symbol"))
                        (cons name (map expand value))))
                    spec)
                   (map expand body))))
               ((lambda)
                (lambda ex-lambda (name-or-spec & body)
                  (let ((check-spec!
                         (lambda (x)
                           (when (not (symbol? x))
                             (raise "Lambda parameter must be a symbol")))))
                    (cond
                      ((symbol? name-or-spec)
                       (let ((name name-or-spec)
                             (spec (car body))
                             (body (cdr body)))
                         (run! check-spec! spec)
                         (list* 'lambda name spec (map expand body))))
                      ((list? name-or-spec)
                       (run! check-spec! name-or-spec)
                       (list* 'lambda name-or-spec (map expand body)))
                      (else (raise "Lambda without parameter list"))))))
               ((begin) false) ;; expands normally
               ((quote) (lambda ex-quote (quoted) (list 'quote quoted)))
               (else
                (when-let (macro (symbol-macro-value hd))
                          (comp expand macro)))))))
    (if macro
        (apply macro (cdr list-form))
        (map-0 expand list-form))))

(define (expand-0 expr)
  (apply (symbol-macro-value (car expr)) (cdr expr)))

(define (new-env parent captures-box)
  (-> empty-map
      (assoc 'parent parent)
      (assoc 'captures captures-box)
      (assoc 'varc 0)))

(define (empty-env) (new-env nil (box nil)))

;; compile an expanded form
;; note that expansion already reports
;; syntax errors for built-in forms
;;
;; into serial form, a list of instructions
;; like (br-if-not label)
;;   or (call 3)
;;   or (const (foo bar baz))
;; etc.
;;
;; these will then be compiled to bytecode in a
;; second pass
(define (compile &env form &tail)
  (cond
    ((literal? form)
     (cons `(const ~form) &tail))
    ((list? form)
     (case (car form)
       ((if) (compile-if &env form &tail))
       ((lambda) (compile-lambda &env form &tail))
       ((let) (compile-let &env form &tail))
       ((begin) (compile-begin &env (cdr form) &tail))
       ((quote) (cons `(const ~(cadr form)) &tail))
       (else (compile-call &env form &tail))))
    ((symbol? form) (load-sym &env form &tail))
    (else (raise (list "Expression cannot be compiled" form)))))

(define (literal? form)
  (or (nil? form)
      (boolean? form)
      (number? form)
      (string? form)
      (char? form)))

(define (compile-call &env form &tail)
  (foldr
   (lambda (f &tail)
     (compile &env f &tail))
   (cons
    `(call ~(dec (count form)))
    &tail)
   form))

(define (compile-begin &env forms &tail)
  (cond
    ((not forms) (cons '(const nil) &tail))
    ((not (cdr forms)) (compile &env (car forms) &tail))
    (else
     (compile
      &env
      (car forms)
      (list*
       '(drop)
       (compile-begin
        &env
        (cdr forms)
        &tail))))))

(define (compile-if &env form &tail)
  (let ((lbl-then (gensym "then"))
        (lbl-end (gensym "end"))
        (pred (cadr form))
        (thn (caddr form))
        (els (cadddr form)))
    (compile
     &env
     pred
     (list*
      `(br-if-not ~lbl-then)
      (compile
       &env
       thn
       (list*
        `(br ~lbl-end 1)
        `(label ~lbl-then)
        (compile
         &env
         els
         (list*
          `(label ~lbl-end)
          &tail))))))))

(define (compile-lambda &env form &tail)
  (let ((captures (box empty-map))
        (env* (new-env &env captures))
        (name+spec+body
         (if (symbol? (cadr form))
             (cdr form)
             (cons nil (cdr form))))
        (env*
         (if (car name+spec+body)
             (update
              env* 'vars
              assoc (car name+spec+body)
              '(this))
             env*))
        (env*
         (update
          env* 'vars
          (lambda (vars)
            ((lambda recur (vars i arg-names)
               (cond
                 ((not arg-names) vars)
                 ((eq? '& (car arg-names))
                  (assoc
                   vars
                   (cadr arg-names)
                   `(rest-arg ~i)))
                 (else
                  (recur
                   (assoc
                    vars
                    (car arg-names)
                    `(arg ~i))
                   (+ i 1)
                   (cdr arg-names)))))
             vars
             0
             (cadr name+spec+body)))))
        (lambda-insns
         (compile-begin
          env*
          (cddr name+spec+body)
          nil))
        (captures (unbox captures))
        (capture-count (count captures)))
    (foldr
     (lambda (capture &tail)
       (load-var
        &env (cadadr capture)
        &tail))
     (cons
      `(lambda ~capture-count ~lambda-insns)
      &tail)
     captures)))

(define (compile-let &env form &tail)
  (compile-let* &env (cadr form) (cddr form) &tail))
(define (compile-let* &env bindings body &tail)
  (if bindings
      (let ((binding (car bindings))
            (binding-name (car binding))
            (binding-body (cdr binding))
            (var-idx (get &env 'varc))
            (the-var `(var ~var-idx))
            (env* (-> &env
                      (update 'vars assoc binding-name the-var)
                      (update 'varc + 1))))
        (compile-begin
         &env
         binding-body
         (list*
          `(set! ~the-var)
          (compile-let*
           env*
           (cdr bindings)
           body
           &tail))))
      (compile-begin &env body &tail)))

(define (load-sym &env sym &tail)
  (if-let (var (-> &env (get 'vars) (get sym)))
    (load-var &env var &tail)
    (load-captured-sym &env sym &tail)))

(define (load-captured-sym &env sym &tail)
  (if-let (var (find-captured-var &env sym))
    (load-var &env var &tail)
    (load-var &env `(top ~sym) &tail)))

(define (find-captured-var &env sym)
  (when &env
    (let ((captures-box (get &env 'captures))
          (existing-captures (unbox captures-box))
          (already-captured (get existing-captures sym)))
      (if already-captured
          (car already-captured)
          (let ((parent-env (get &env 'parent))
                (var-in-parent
                 (or
                  (-> parent-env (get 'vars) (get sym))
                  (find-captured-var parent-env sym))))
            (when var-in-parent
              (let ((var-here `(closed ~(count existing-captures))))
                (box-set!
                 captures-box
                 (assoc
                  existing-captures
                  sym (list var-here var-in-parent)))
                var-here)))))))

(define (load-var &env var &tail)
  (cons
   (list 'load var)
   &tail))

(define (the-pipeline expr)
  (compile
   (empty-env)
   (expand expr)
   nil))

;; bytecode writing

(define (write-zeros bv n)
  (when (> n 0)
    (bytevector-push! bv 0)
    (write-zeros bv (dec n))))
(define (write-padding-off! bv off)
  (let ((dist-to-four (- (modulo (+ off (bytevector-length bv)) -4))))
    (bytevector-ensure! bv dist-to-four)
    (write-zeros bv dist-to-four)))
(define (write-padding! bv)
  (write-padding-off! bv 0))

(define (write-u16le! bv int)
  (bytevector-push!
   bv
   (bitwise-and 255 int)
   (bitwise-and 255 (arithmetic-shift int -8))))
(define (write-u32le! bv int)
  (bytevector-push!
   bv
   (bitwise-and 255 int)
   (bitwise-and 255 (arithmetic-shift int -8))
   (bitwise-and 255 (arithmetic-shift int -16))
   (bitwise-and 255 (arithmetic-shift int -24))))
(define (write-u32le-at! bv pos int)
  (let ((buf (bytevector-buf bv)))
    (bytestring-set! buf pos
                     (bitwise-and 255 int))
    (bytestring-set! buf (+ pos 1)
                     (bitwise-and 255 (arithmetic-shift int -8)))
    (bytestring-set! buf (+ pos 2)
                     (bitwise-and 255 (arithmetic-shift int -16)))
    (bytestring-set! buf (+ pos 3)
                     (bitwise-and 255 (arithmetic-shift int -24)))))
(define (write-u64le! bv int)
  (bytevector-push!
   bv
   (bitwise-and 255 int)
   (bitwise-and 255 (arithmetic-shift int -8))
   (bitwise-and 255 (arithmetic-shift int -16))
   (bitwise-and 255 (arithmetic-shift int -24))
   (bitwise-and 255 (arithmetic-shift int -32))
   (bitwise-and 255 (arithmetic-shift int -40))
   (bitwise-and 255 (arithmetic-shift int -48))
   (bitwise-and 255 (arithmetic-shift int -56))))

(define (new-constant-writer)
  (list
   (box nil) ;; constants reverse list
   (box 0) ;; length
   ))

(define (constant-writer-add0! cw cnst-bytes)
  (box-swap! (car cw) rcons cnst-bytes)
  (dec (box-swap! (cadr cw) inc)))

(define const-list 1)
(define const-direct 2)
(define const-symbol 3)
(define const-string 4)

(define (constant-writer-add! cw cnst)
  (let ((bv (new-bytevector 0)))
    (cond
      ((or (nil? cnst)
           (boolean? cnst))
       (write-u32le! bv const-direct)
       (write-u64le!
        bv
        (case cnst
          ((()) 6) ;; nil
          ((#true) 10)
          ((#false) 14))))
      ((number? cnst)
       (write-u32le! bv const-direct)
       ;; avoid overflow, tags are 2 bits
       (write-u32le! bv (arithmetic-shift cnst 2))
       (write-u32le! bv (arithmetic-shift cnst -30)))
      ((char? cnst)
       (write-u32le! bv const-direct)
       (write-u32le! bv 3) ;; tag
       (write-u32le! bv (char->integer cnst)))
      ((symbol? cnst)
       (write-u32le! bv const-symbol)
       (let ((bs (symbol->bytestring cnst)))
         (write-u32le! bv (bytestring-length bs))
         (bytevector-append! bv bs))
       (write-padding! bv))
      ((string? cnst)
       (write-u32le! bv const-string)
       (let ((bs (string->bytestring cnst)))
         (write-u32le! bv (bytestring-length bs))
         (bytevector-append! bv bs))
       (write-padding! bv))
      ((list? cnst)
       (write-u32le! bv const-list)
       (write-u32le! bv (count cnst))
       (run!
        (lambda (elt)
          (write-u32le! bv (constant-writer-add! cw elt)))
        cnst))
      (else (raise (list "Could not encode constant" cnst))))
    (constant-writer-add0! cw (bytevector->bytestring bv))))
(define (constant-writer->bytes cw bv)
  (when (unbox (car cw))
    (write-u32le! bv sec-constants)
    (write-u32le! bv (unbox (cadr cw)))
    (run!
     (lambda (cnst)
       (bytevector-append! bv cnst))
     (reverse (unbox (car cw))))))

(define (new-codes-writer)
  (list
   (box nil) ;; codes reverse list
   (box 0) ;; length
   ))
(define (codes-writer-add! cw code)
  (box-swap! (car cw) rcons code)
  (dec (box-swap! (cadr cw) inc)))
(define (codes-writer->bytes cw bv)
  (when (unbox (car cw))
    (write-u32le! bv sec-codes)
    (write-u32le! bv (unbox (cadr cw)))
    (run!
     (lambda (code)
       (bytevector-append! bv code))
     (reverse (unbox (car cw))))))

(define (new-code-body-writer)
  (list
   (new-bytevector 0) ;; code
   (box 0) ;; max stack
   (box 0) ;; max locals
   (box empty-map) ;; labels
   (box 0) ;; current stack
   (new-bytevector 0) ;; stack map
   ))
(define (code-body-writer-labels* cw) (cadddr cw))
(define (code-body-writer-stackmap cw) (cadr (cddddr cw)))
(define (code-body-writer->code cw)
  (let ((insns-bv (car cw))
        (bv (new-bytevector (+ 12 (bytevector-length insns-bv)))))
    (write-u32le! bv (bytevector-length insns-bv))
    (write-u32le! bv (unbox (cadr cw)))
    (write-u32le! bv (unbox (caddr cw)))
    (write-u32le! bv (count (unbox (cadddr cw))))
    (bytevector-append! bv (bytevector->bytestring insns-bv))
    (write-padding! bv)
    (bytevector-append! bv (bytevector->bytestring (code-body-writer-stackmap cw)))
    ;; no padding
    (bytevector->bytestring bv)))
(define (cbw-ensure-local! cw locals)
  (box-swap! (caddr cw) max (inc locals)))
(define (cbw-push! cw pushed)
  (let ((new-stack (box-swap! (car (cddddr cw)) + pushed)))
    (box-swap! (cadr cw) max new-stack)
    new-stack))

(define opc-drop 1)
(define opc-ret 2)
(define opc-br 3)
(define opc-br-if-not 4)
(define opc-ldc 5)
(define opc-sym-deref 6)
(define opc-lambda 7)
(define opc-call 8)
(define opc-local-ref 18)
(define opc-local-set 19)
(define opc-arg-ref 20)
(define opc-restarg-ref 21)
(define opc-this-ref 22)
(define opc-closure-ref 23)

(define (cbw-emit-ldc! cw iw what)
  (let ((bv (car cw)))
    (bytevector-push! bv opc-ldc)
    (write-u32le!
     bv
     (constant-writer-add!
      (image-writer-constants iw)
      what))))
(define (code-body-writer-emit! cw iw insn)
  (let ((bv (car cw)))
    (case (car insn)
      ((load)
       (cbw-push! cw 1)
       (let ((what (cadr insn)))
         (case (car what)
           ((top)
            (cbw-emit-ldc! cw iw (cadr what))
            (bytevector-push! bv opc-sym-deref))
           ((arg rest-arg var closed)
            (bytevector-push!
             bv
             (case (car what)
               ((arg) opc-arg-ref)
               ((rest-arg) opc-restarg-ref)
               ((var)
                (cbw-ensure-local! cw (cadr what))
                opc-local-ref)
               ((closed) opc-closure-ref)))
            (bytevector-push! bv (cadr what)))
           ((this)
            (bytevector-push! bv opc-this-ref))
           (else (raise (list "Uncompilable load" what))))))
      ((set!)
       (cbw-push! cw -1)
       (let ((what (cadr insn)))
         (case (car what)
           ((var)
            (cbw-ensure-local! cw (cadr what))
            (bytevector-push! bv opc-local-set)
            (bytevector-push! bv (cadr what)))
           (else (raise (list "Uncompilable set!" what))))))
      ((const) (cbw-push! cw 1) (cbw-emit-ldc! cw iw (cadr insn)))
      ((drop) (cbw-push! cw -1) (bytevector-push! bv opc-drop))
      ((call)
       (let ((argc (cadr insn))
             (retc 1))
         (bytevector-push! bv opc-call)
         (bytevector-push! bv argc)
         (bytevector-push! bv retc)
         (cbw-push!
          cw
          (- retc ;; pushes returns
             (inc ;; pops function
              argc) ;; pops args
             ))))
      ((br-if-not br)
       (let ((lbl (cadr insn))
             (lbls* (code-body-writer-labels* cw)))
         (bytevector-push!
          bv
          (case (car insn)
            ((br)
             (cbw-push! cw (- (caddr insn)))
             opc-br)
            ((br-if-not)
             (cbw-push! cw -1)
             opc-br-if-not)))
         (let ((ip (bytevector-length bv))
               (found* (or (get (unbox lbls*) lbl) (box nil)))
               (found (unbox found*))
               (to-write
                (cond
                  ((number? found) found)
                  (else
                   (box-swap! found* rcons ip)
                   (when (nil? found)
                     (box-swap! lbls* assoc lbl found*))
                   0))))
           (write-u32le! bv (- (or found ip) ip)))))
      ((label)
       (let ((lbl (cadr insn))
             (lbls* (code-body-writer-labels* cw))
             (ip (bytevector-length bv)))
         (box-swap!
          lbls*
          (lambda (lbls)
            (let ((found* (get lbls lbl)))
              (cond
                (found*
                 (run!
                  (lambda (write-target)
                    (write-u32le-at!
                     bv
                     write-target
                     (- ip (+ write-target 4))))
                  (unbox found*))
                 (box-set! found* ip)
                 (let ((sm (code-body-writer-stackmap cw)))
                   (write-u32le! sm ip)
                   (write-u32le! sm (cbw-push! cw 0)))
                 lbls)
                (else (assoc lbls lbl (box ip)))))))))
      ((lambda)
       (let ((argc (cadr insn))
             (body (caddr insn))
             (body-idx (bytecomp! iw body)))
         (cbw-push! cw (- (dec argc)))
         (bytevector-push! bv opc-lambda)
         (write-u32le! bv body-idx)
         (write-u16le! bv argc)))
      (else (raise (list "Uncompilable insn" insn))))))
(define (code-body-writer-flush! cw iw)
  (let ((height (cbw-push! cw 0))
        (bv (car cw)))
    (bytevector-push! bv opc-ret)
    (bytevector-push! bv height)))

(define (new-image-writer)
  (list
   (new-constant-writer) ;; constants
   (new-codes-writer) ;; codes
   (box nil) ;; bindings
   (box nil) ;; start
   ))
(define (image-writer-constants iw) (car iw))
(define (image-writer-codes iw) (cadr iw))
(define (image-writer-bindings iw) (unbox (caddr iw)))
(define (image-writer-add-binding! iw key val)
  (box-swap! (caddr iw) rcons (list key val)))
(define (image-writer-start iw) (unbox (cadddr iw)))
(define (image-writer-set-start! iw s) (box-set! (cadddr iw) s))

(define magic-u32le 7564391)
(define sec-constants 1)
(define sec-codes 2)
(define sec-bindings 3)
(define sec-start 4)

(define (image-writer->bytes iw bv)
  (write-u32le! bv magic-u32le)
  (write-u32le! bv 1) ;; version
  (constant-writer->bytes (image-writer-constants iw) bv)
  (codes-writer->bytes (image-writer-codes iw) bv)
  (when (image-writer-bindings iw)
    (write-u32le! bv sec-bindings)
    (run!
     (lambda (bd)
       (write-u32le! bv (car bd)) ;; symbol
       (write-u32le! bv (cadr bd)) ;; value
       )
     (reverse (image-writer-bindings iw))))
  (when-let (start (image-writer-start iw))
    (write-u32le! bv sec-start)
    (write-u32le! bv start)))

;; write instructions generated by `compile' to bytecode
;; returns the code index
(define (bytecomp! iw insns)
  (let ((cw (new-code-body-writer)))
    (run!
     (lambda (insn)
       (code-body-writer-emit! cw iw insn))
     insns)
    (code-body-writer-flush! cw iw)
    (codes-writer-add!
     (image-writer-codes iw)
     (code-body-writer->code cw))))
