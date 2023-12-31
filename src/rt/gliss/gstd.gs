;;; -*- mode: scheme; -*-
;;;
;;; Copyright (C) 2023 eutro
;;;
;;; This program is free software: you can redistribute it and/or modify
;;; it under the terms of the GNU General Public License as published by
;;; the Free Software Foundation, either version 3 of the License, or
;;; (at your option) any later version.
;;;
;;; This program is distributed in the hope that it will be useful,
;;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;;; GNU General Public License for more details.
;;;
;;; You should have received a copy of the GNU General Public License
;;; along with this program.  If not, see <https://www.gnu.org/licenses/>.

;; literals: 'nil 'true 'false strings numbers

;; primitives: see those provided in gliss.rkt

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

(define (identity x) x)

(define (all f l)
  (if (nil? l)
      true
      (let ((v (f (car l))))
        (if v (all f (cdr l)) false))))

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

(define (run! f seq)
  (foldl (lambda (acc x) (f x) acc) nil seq))

(define (concat-1 xs ys)
  (if ys
      ((lambda loop (xs ys)
         (if xs
             (cons
              (car xs)
              (loop (cdr xs) ys))
             ys))
       xs ys)
      xs))

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

(define (rcons pair x) (cons x pair))

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

(defmacro (static-cond & clauses)
  (if (nil? clauses)
      nil
      (if (vm-check (caar clauses))
          `(begin ~@(cdar clauses))
          `(static-cond ~@(cdr clauses)))))

(defmacro (static-when pred & body)
  `(static-cond (~pred ~@body)))

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
  (let ((nm (gensym "nm")))
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
         (else nil)))))

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
  (if (list? xs)
      ((lambda recur (n xs)
         (if xs
             (recur (inc n) (cdr xs))
             n))
       0 xs)
      (raise (list "Not a list" xs))))

(define (reverse xs)
  ((lambda recur (xs tl)
     (if xs
         (recur (cdr xs) (cons (car xs) tl))
         tl))
   xs nil))

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

;; arithmetic

(define (min-0 a b) (if (<= a b) a b))
(define (min x & xs) (foldl min-0 x xs))
(define (max-0 a b) (if (>= a b) a b))
(define (max x & xs) (foldl max-0 x xs))

(define (char<? & cs) (apply < (map char->integer cs)))
(define (char>? & cs) (apply > (map char->integer cs)))
(define (char<=? & cs) (apply <= (map char->integer cs)))
(define (char>=? & cs) (apply >= (map char->integer cs)))

(define (char-digit? c)
  (char<=? #\0 c #\9))

(define (inc x) (+ x 1))
(define (dec x) (- x 1))

;; boxes

(define (box-swap! bx f & args)
  (let ((new-v (apply f (unbox bx) args)))
    (box-set! bx new-v)
    new-v))

;; bytestrings

(define (bytestring-slice bytes pos len)
  (let ((bs (new-bytestring len)))
    (bytestring-copy! bs 0 bytes pos len)
    bs))

(define (bytestring-resize bytes new-size)
  (let ((old-size (bytestring-length bytes)))
    (cond
      ((< new-size old-size)
       (bytestring-slice bytes 0 new-size))
      ((> new-size old-size)
       (let ((bs (new-bytestring new-size)))
         (bytestring-copy! bs 0 bytes 0 old-size)
         bs))
      (else bytes))))

(define (list->bytestring ls)
  (let ((cnt (count ls))
        (_ (when (eq? 0 cnt)
             (dbg-suspend ls)
             (count ls)))
        (bs (new-bytestring cnt)))
    ((lambda recur (i ls)
       (when ls
         (bytestring-set! bs i (car ls))
         (recur (inc i) (cdr ls))))
     0 ls)
    bs))

;; bytevectors

(define (new-bytevector initial-cap)
  (list
   (box 0)
   (box (new-bytestring (or initial-cap 16)))))

(define (bytevector-length bv)
  (unbox (car bv)))

(define (bytevector-set-len! bv len)
  (box-set! (car bv) len))

(define (bytevector-buf bv)
  (unbox (cadr bv)))

(define (bytevector-resize-buf! bv new-cap)
  (box-swap! (cadr bv) bytestring-resize new-cap))

(define (bytevector->bytestring bv)
  (bytestring-resize (bytevector-buf bv) (bytevector-length bv)))

(define (double-until n expected)
  (if (>= n expected) n
      (double-until
       (if (= 0 n) 4 (* 2 n))
       expected)))

(define (bytevector-ensure! bv space)
  (let ((buf (bytevector-buf bv))
        (cap (bytestring-length buf))
        (len (bytevector-length bv))
        (space-rem (- cap len)))
    (when (< space-rem space)
      (let ((new-cap (double-until cap (+ len space))))
        (bytevector-resize-buf! bv new-cap)))))

(define (bytevector-append! bv bs)
  (let ((n (bytestring-length bs)))
    (bytevector-ensure! bv n)
    (let ((buf (bytevector-buf bv))
          (i (bytevector-length bv)))
      (bytestring-copy! buf i bs 0 n)
      (bytevector-set-len! bv (+ i n))))
  bv)

(define (bytevector-push! bv & bs)
  (bytevector-append! bv (list->bytestring bs)))

;; boxlists/iterators

(define (iter-next! bl)
  (let ((l (unbox bl)))
    (if (nil? l)
        false
        (begin
          (box-set! bl (cdr l))
          (car l)))))

(define (iter-peek bl)
  (let ((l (unbox bl)))
    (if (nil? l)
        false
        (car l))))

(define (open-string str)
  (box (string->list str)))

;; debugging

;; primitive: (debugger-suspend tag)
