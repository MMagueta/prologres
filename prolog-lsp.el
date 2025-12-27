;; Force .pl files to use prolog-mode instead of idlwave-mode or perl-mode
(add-to-list 'auto-mode-alist '("\\.pl\\'" . prolog-mode))
(add-to-list 'auto-mode-alist '("\\.pro\\'" . prolog-mode))

(use-package prolog
  :config
  (setq prolog-system 'swi)
  ;; Ensure prolog-mode is used for .pl files
  (add-to-list 'auto-mode-alist '("\\.pl\\'" . prolog-mode) t))

(require 'lsp-mode)

(lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection '("swipl"
                                            "-g" "use_module(library(lsp_server))."
                                            "-g" "lsp_server:main"
                                            "-t" "halt"
                                            "--" "stdio"))
    :major-modes '(prolog-mode)
    :priority 1
    :multi-root t
    :server-id 'prolog-lsp))
