;; literals: 'nil 'true 'false strings numbers

;; primitives:
;; symbol-set-value!
;; quote lambda if let
;; car cdr list? symbol?
;; eq? gensym box unbox
;; + begin box-set!

(symbol-set-value! 'list (lambda list (& args) args))

(symbol-set-macro!
 (symbol-set-value!
  'define
  (lambda define (header & body)
    (if (list? header)
        (let ((hd (car header))
              (args (cdr header)))
          (list
           'define hd
           (if (symbol? hd)
               (cons 'lambda (cons hd (cons args body)))
               (cons 'lambda (cons args body)))))
        (cons
         'symbol-set-value!
         (cons
          (list 'quote header)
          body)))))
 true)

(define (nil? x) (eq? nil x))

(define (map-0 f xs)
  (if (nil? xs)
      nil
      (cons
       (f (car xs))
       (map-0 f (cdr xs)))))

(define (some f l)
  (if (nil? l)
      false
      (let ((v (f (car l))))
        (if v v (some f (cdr l))))))

(define (map f & ls)
  (if (some nil? ls)
      nil
      (cons (apply f (map-0 car ls))
            (apply map f (map-0 cdr ls)))))

(define (foldr f start seq)
  (if seq
      (f (car seq) (foldr f start (cdr seq)))
      start))

(define (foldl f start seq)
  (if seq
      (foldl f (f start (car seq)) (cdr seq))
      start))

(define (concat-1 xs ys)
  (if xs
      (cons
       (car xs)
       (concat-1 (cdr xs) ys))
      ys))

(define (concat & ls)
  (foldr concat-1 nil ls))

(define (comp-1 f g)
  (lambda (& args) (f (apply g args))))

(define (comp f & gs)
  (foldl comp-1 f gs))

(define (partial f & pargs)
  (lambda (& args) (apply f (concat pargs args))))

(define (caar c) (car (car c)))
(define (cadr c) (car (cdr c)))
(define (cdar c) (cdr (car c)))
(define (cddr c) (cdr (cdr c)))

(define (caaar c) (car (caar c)))
(define (caadr c) (car (cadr c)))
(define (cadar c) (car (cdar c)))
(define (caddr c) (car (cddr c)))
(define (cdaar c) (cdr (caar c)))
(define (cdadr c) (cdr (cadr c)))
(define (cddar c) (cdr (cdar c)))
(define (cdddr c) (cdr (cddr c)))

(define (caaaar c) (car (caaar c)))
(define (caaadr c) (car (caadr c)))
(define (caadar c) (car (cadar c)))
(define (caaddr c) (car (caddr c)))
(define (cadaar c) (car (cdaar c)))
(define (cadadr c) (car (cdadr c)))
(define (caddar c) (car (cddar c)))
(define (cadddr c) (car (cdddr c)))
(define (cdaaar c) (cdr (caaar c)))
(define (cdaadr c) (cdr (caadr c)))
(define (cdadar c) (cdr (cadar c)))
(define (cdaddr c) (cdr (caddr c)))
(define (cddaar c) (cdr (cdaar c)))
(define (cddadr c) (cdr (cdadr c)))
(define (cdddar c) (cdr (cddar c)))
(define (cddddr c) (cdr (cdddr c)))

(symbol-set-macro!
 (define (defmacro header & body)
   (let ((get-name
          (lambda get-name (x)
            (if (list? x)
                (get-name (car x))
                x)))
         (name (get-name header)))
     (list
      'begin
      (cons 'define (cons header body))
      (list 'symbol-set-macro! (list 'quote name) true))))
 true)

(defmacro `form
  (if (if form (list? form) nil)
      (if (eq? 'unquote (car form))
          (car (cdr form))
          (foldr
           (lambda (form rem)
             (if (if form (list? form) nil)
                 (if (eq? 'unquote (car form))
                     (list 'cons
                           (car (cdr form))
                           rem)
                     (if (eq? 'unquote-splicing (car form))
                         (list 'concat
                               (car (cdr form))
                               rem)
                         (list 'cons
                               (list 'quasiquote form)
                               rem)))
                 (list 'cons
                       (list 'quasiquote form)
                       rem)))
           nil
           form))
      (list 'quote form)))

(defmacro (cond & clauses)
  (if (nil? clauses)
      nil
      `(if ~(caar clauses)
           (begin ~@(cdar clauses))
           (cond ~@(cdr clauses)))))

(defmacro (and & exprs)
  (if exprs
      (if (cdr exprs)
          `(if ~(car exprs)
               (and ~@(cdr exprs))
               false)
          (car exprs))
      false))

(defmacro (or & exprs)
  (if exprs
      (if (cdr exprs)
          (let ((res (gensym "or-result")))
            `(let ((~res ~(car exprs)))
               (if ~res ~res (or ~@(cdr exprs)))))
          (car exprs))
      true))

(defmacro (when pred & body)
  `(if ~pred (begin ~@body) nil))

(defmacro (if-let spec thn els)
  `(let (~spec)
     (if ~(car spec)
         ~thn
         ~els)))

(defmacro (when-let spec & body)
  `(if-let ~spec (begin ~@body) nil))

(define (not x) (if x false true))
(define (any? x) true)
(define (boolean? x) (or (eq? true x) (eq? false x)))
(define else true)

(defmacro (case arg & cases)
  (let ((nm (gensym)))
    `(let ((~nm ~arg))
       (cond
         ~@(map
            (lambda (cs)
              (let ((lhs (car cs))
                    (body (cdr cs)))
                (cond
                  ((list? lhs)
                   `((or
                      ~@(map
                         (lambda (cnst)
                           `(eq? ~nm '~cnst))
                         lhs))
                     ~@body))
                  (else cs))))
            cases)
         [else nil]))))

(defmacro (-> x & forms)
  ((lambda recur (x forms)
     (if forms
         (let ((form (car forms))
               (threaded
                (if (list? form)
                    `(~(car form) ~x ~@(cdr form))
                    (list form x))))
           (recur threaded (cdr forms)))
         x))
   x forms))

(define (list* & args)
  (cond
    ((nil? args) nil)
    ((nil? (cdr args)) (car args))
    (else (cons (car args)
                (apply list* (cdr args))))))

(define (enumerate-from i xs)
  (if xs
      (cons
       (list i (car xs))
       (enumerate-from
        (+ i 1)
        (cdr xs)))
      nil))

(define (enumerate xs)
  (enumerate-from 0 xs))

(define (count xs)
  ((lambda recur (n xs)
     (if xs
         (recur (+ n 1) (cdr xs))
         n))
   0 xs))

;; TODO implement maps

(define empty-map nil)

(define (find assoc key)
  (cond
    ((not assoc) nil)
    ((eq? (caar assoc) key) (car assoc))
    (else (find (cdr assoc) key))))

(define (get assoc key)
  (if-let (kv (find assoc key))
    (cadr kv)
    nil))

(define (assoc assoc key val)
  (cons (list key val) assoc))

(define (update map key f & args)
  (assoc
   map key
   (apply f (get map key) args)))
