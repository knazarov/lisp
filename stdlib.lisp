;; Standard library

(define list (lambda x x))
(define cadr (lambda (x) (car (cdr x))))

(defmacro defun params
  (list 'define
        (car params)
        (cons 'lambda (cons
                       (car (cdr params))
                       (cdr (cdr params))
                       )
              )
        )
  )


(defun map (fun list)
  (if list
      (cons (fun (car list)) (map fun (cdr list)))
    )
  )

(defmacro let params
  (cons (cons 'lambda
              (cons
               (map (lambda (x) (car x)) (car params))
               (cdr params)
               ))
        (map (lambda (x) (cadr x)) (car params))
        )
  )
