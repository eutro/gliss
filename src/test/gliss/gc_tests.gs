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

(test
 scope-tests

 (assert-eq? 'a (call-in-new-scope (lambda () (list 1 2 3) 'a)))
 (assert-fn string=? "abc" (call-in-new-scope (lambda () (list->string (list #\a #\b #\c)))))

 (assert-eq? 5 (call-in-new-scope + 1 2 2))

 (call-in-new-scope
  (lambda ()
    (let ((bx (box nil)))
      (dbg-dump-obj bx)
      (call-in-new-scope
       run!
       (lambda (x)
         (box-swap! bx rcons x)
         (dbg-dump-obj bx))
       (list 1 2 3))

      (assert-fn (comp (partial all identity)
                       (partial map eq?))
                 '(3 2 1)
                 (unbox bx)))))

 ;;
 )

(define (main)
  (call-in-new-scope scope-tests))
