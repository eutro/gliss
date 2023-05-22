;; VM primitives:
;; symbol-macro-value
;; raise

;; gstd linked before this

(define (expand-list list-form)
  (let ((hd (car list-form))
        (macro (and (symbol? hd)
                    (symbol-macro-value hd))))
    (if macro
        (expand (apply macro (cdr list-form)))
        (map-0 expand list-form))))

(define (expand form)
  (if (list? form)
    (expand-list form)
    form))

(define (new-env parent captures-box)
  (-> empty-map
      (assoc 'parent parent)
      (assoc 'captures captures-box)))

(define (empty-env) (new-env nil (box nil)))

;; compile an expanded form
;;
;; into serial form, a list of instructions
;; like (br-if label)
;;   or (call 3)
;;   or (const (foo bar baz))
;; etc.
;;
;; these will then be compiled to bytecode in a
;; second pass
(define (compile &env form &tail)
  (cond
    ((or (nil? form)
         (boolean? form)
         (integer? form)
         (string? form))
     (cons `(const ~form) &tail))
    ((list? form)
     (case (car form)
       ((if) (compile-if &env form &tail))
       ((lambda) (compile-lambda &env form &tail))
       ((let) (compile-let &env form &tail))
       ((begin) (compile-begin &env (cdr form) &tail))
       ((quote) (cons `(const ~(cadr form)) &tail))
       (else
        (foldr
         (lambda (f &tail)
           (compile &env f &tail))
         (cons
          `(call ~(+ -1 (count form)))
          &tail)
         form))))
    ((symbol? form) (load-sym &env form &tail))))
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
      `(br-if ~lbl-then)
      (compile
       &env
       els
       (list*
        `(br ~lbl-end)
        `(label ~lbl-then)
        (compile
         &env
         thn
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
          (caddr name+spec+body)
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
            (env* (update
                   &env 'vars
                   assoc binding-name `(var ~binding-name))))
        (compile-begin
         &env
         binding-body
         (list*
          `(bind-var ~binding-name)
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
              (let ((var-here `(capture ~(count existing-captures))))
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
