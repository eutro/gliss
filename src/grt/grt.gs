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
    ((#\~) (iter-next! s) (list 'unquote (read s)))
    (else (read-token s))))

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
    `(call ~(+ -1 (count form)))
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
        `(br ~lbl-end)
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

;; write instructions generated by `compile' to bytecode
(define (bytecomp insns)
  nil)

(define (the-pipeline expr)
  (compile
   (empty-env)
   (expand expr)
   nil))
