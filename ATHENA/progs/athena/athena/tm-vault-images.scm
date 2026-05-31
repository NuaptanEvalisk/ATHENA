(texmacs-module (athena athena tm-vault-images))


(tm-widget (vault-image-preferences-widget)
  (aligned
    (item (text "Auto copy images to vault:")
      (toggle (set-preference "vault auto copy images to vault"
                              (if answer "on" "off"))
              (equal? (get-preference "vault auto copy images to vault")
                      "on")))
    (item (text "Normalize image filename when inserting:")
      (toggle (set-preference "vault normalize image filename when inserting"
                              (if answer "on" "off"))
              (equal? (get-preference
                       "vault normalize image filename when inserting")
                      "on")))))
