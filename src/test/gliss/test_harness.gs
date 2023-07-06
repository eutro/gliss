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
