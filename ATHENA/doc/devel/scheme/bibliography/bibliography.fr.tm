<TeXmacs|1.0.7.3>

<style|tmdoc>

<\body>
  <tmdoc-title|Styles bibliographiques pour <TeXmacs>>

  <section|Styles bibliographiques>

  Il est possible d'associer à une bibliographie BibTeX un ou plusieurs
  styles, qu'ils soient standards ou personnalisés. Les styles BibTeX sont
  dénotés par leur nom usuel. Les styles personnalisés propres à <TeXmacs>
  sont systématiquement préfixés par <verbatim|tm-> (par exemple, le syle
  <verbatim|tm-plain> correspond au style <verbatim|plain> de <TeXmacs>).
  D'autres styles peuvent être ajoutés dans le répertoire
  <verbatim|$ATHENA_PATH\\prog\\bibtex>.

  Pour l'éditeur, chaque style correspond à un fichier <verbatim|.scm>. Les
  fichiers correspondant à chaque style sont traités comme des programmes
  Scheme ordinaire : il est nécessaire de respecter scrupuleusement
  l'utilisation des fonctions spécifiques aux styles bibliographiques.

  <section|Styles BibTeX>

  Actuellement, les styles BibTeX suivants ont été implémentés :
  <verbatim|abbrv>, <verbatim|alpha>, <verbatim|ieeetr>, <verbatim|plain> et
  <verbatim|siam>. Leur utilisation ne nécessite pas l'installation de
  BibTeX.

  <section|Création de styles bibliographiques>

  Si les fichiers de style standards de <TeXmacs> ne sont pas adaptés à vos
  besoins, vous pouvez en créer d'autres. Néanmoins, la création d'un fichier
  de style à partir de rien est une tâche complexe. Nous vous recommandons
  donc de vous servir des fichiers de style ou des modules existants ou de
  les personnaliser.

  <subsection|Étude d'un exemple>

  Un fichier de style bibliographique est un<with|font-shape|italic|> fichier
  placé dans le répertoire <verbatim|$ATHENA_PATH/prog/bibtex> portant le
  nom du style suivi de l'extension <verbatim|.scm>, par exemple
  <verbatim|example.scm> pour un style <verbatim|example> ; ce dernier sera
  dénoté par <verbatim|tm-example> lors de son utilisation dans un document
  <TeXmacs>.

  Tout fichier de style doit se déclarer en tant que module à l'aide de la
  déclaration suivante :

  <\scm-fragment>
    (texmacs-module (bibtex example)

    \ \ (:use (bibtex bib-utils)))
  </scm-fragment>

  Le module <verbatim|bib-utils> contient toutes les fonctions nécessaires à
  la rédation et à l'interprétation d'un fichier de style bibliographique.

  Tout fichier de style doit se déclarer en tant que style bibliographique à
  l'aide de la commande suivante :

  <scm-fragment|(bib-define-style "example" "plain")>

  Le premier paramètre de la fonction <scm|bib-define-style> est le nom du
  style courant, et le second paramètre est le nom du style par défaut,
  <verbatim|plain> dans notre cas. Si une fonction n'est pas définie dans le
  style courant, la version du style par défaut est alors utilisée
  automatiquement.

  Ainsi, notre fichier de style minimal a l'aspect suivant :

  <\scm-fragment>
    (texmacs-module (bibtex example)

    \ \ (:use (bibtex bib-utils)))

    \;

    (bib-define-style "example" "plain")
  </scm-fragment>

  Chaque fonction de formatage définie dans le style par défaut peut être
  surchargée dans le fichier de style courant. Par exemple, la fonction de
  formatage de la date dans le style <verbatim|plain> s'appelle
  <scm|bib-format-date> ; elle redéfinissable dans notre style de la manière
  suivante :

  <\scm-fragment>
    (tm-define (bib-format-date e)

    \ \ (:mode bib-example?)

    \ \ (bib-format-field e "year"))
  </scm-fragment>

  Toute fonction exportée doit être préfixée par <verbatim|bib->. Une
  fonction surchargée doit être suivie de la directive <scm|(:mode
  bib-example?)>, dans laquelle <verbatim|example> est le nom du style en
  cours.

  Voici maintenant à quoi ressemble notre fichier de style
  <verbatim|example.scm> :

  <\scm-fragment>
    (texmacs-module (bibtex example)

    \ \ (:use (bibtex bib-utils)))

    \;

    (bib-define-style "example" "plain")

    \;

    (tm-define (bib-format-date e)

    \ \ (:mode bib-example?)

    \ \ (bib-format-field e "year"))
  </scm-fragment>

  <subsection|Fonctions utiles pour la création de fichiers de style>

  <\explain>
    <scm|(bib-abbreviate name dot spc)><explain-synopsis|abbréviation d'un
    nom>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> correspondant à l'abbréviation du
    nom contenu dans <scm|name> (un arbre <TeXmacs>) : on récupère la liste
    des premières lettres de chaque mot, suivi de <scm|dot> (un arbre
    <TeXmacs>) et séparées par <scm|spc> (un arbre <TeXmacs>).
  </explain>

  <\explain>
    <scm|(bib-add-period tm)><explain-synopsis|ajout d'un point>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> en ajoutant un point à la fin de
    l'arbre <TeXmacs> <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-default tm)><explain-synopsis|arbre <TeXmacs> par défaut>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> sans occurence du label
    <verbatim|keepkase>.
  </explain>

  <\explain>
    <scm|(bib-define-style name default)><explain-synopsis|déclaration d'un
    style>
  <|explain>
    Cette fonction déclare un style nommé <scm|name> (de type string) avec
    <scm|default> (de type string) le style par défaut. Le style est alors
    sélectionné en choisissant <verbatim|tm-><scm|name> lors de l'ajout d'une
    bibliographie à un document. Quand une fonction de formatage n'est pas
    définie dans le style, celle du style <scm|default> est évaluée en
    remplacement.
  </explain>

  <\explain>
    <scm|(bib-emphasize tm)><explain-synopsis|mise en italique>
  <|explain>
    Cette fonction renvoie un arbre <TeXmacs> correspondant à la mise en
    italique de l'abre <TeXmacs> <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-empty? entry field)><explain-synopsis|test à zéro d'un champ>
  <|explain>
    Cette fonction renvoie le booléen <scm|#t> si le champ de l'entrée
    <scm|entry> dont le nom est <scm|field> (de type string) est vide ou
    inexistant ; elle renvoie <scm|#f> dans le cas contraire.
  </explain>

  <\explain>
    <scm|(bib-field entry field)><explain-synopsis|récupération d'un champ>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> correspondant au contenu du champ
    de l'entrée <scm|entry> dont le nom est <scm|field> (de type string),
    sans aucun formatage. Dans certains cas, une donnée spécifique est
    renvoyée :

    <\itemize-dot>
      <item>si <scm|field> est égal à <scm|"author"> ou <scm|"editor">, la
      donnée renvoyée est un arbre dont le label est <verbatim|bib-names>,
      contenant une liste de noms d'auteurs ; chaque nom d'auteur est un
      arbre dont le label est <verbatim|bib-name> et qui contient quatre
      éléments, dans l'ordre : le prénom, la particule, le nom et un
      qualificatif (junior ou senior).

      <item>si <scm|field> est égal à <scm|"page">, la donnée contient une
      liste d'entiers : soit une liste vide, soit un singleton contenant la
      page référencée, soit un couple dénotant l'intervalle des pages
      référencées.
    </itemize-dot>
  </explain>

  <\explain>
    <scm|(bib-format-field entry field)><explain-synopsis|formatage basique
    d'un champ>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> correspondant au contenu du champ
    de l'entrée <scm|entry> dont le nom est <scm|field> (de type string),
    avec un formatage basique.
  </explain>

  <\explain>
    <scm|(bib-format-field-Locase entry field)><explain-synopsis|formatage
    spécial d'un champ>
  <|explain>
    Cette fonction est similaire à la fonction <scm|bib-format-entry> ; mais
    le champ est formaté en minuscules, avec une majuscule en tête.
  </explain>

  <\explain>
    <scm|(bib-locase tm)><explain-synopsis|mise en minucule>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> en mettant en minuscule toutes les
    lettres de l'arbre <TeXmacs> <scm|tm>, sauf celles englobées dans un bloc
    <verbatim|keepkase>.
  </explain>

  <\explain>
    <scm|(bib-new-block tm)><explain-synopsis|nouveau bloc>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> consistant en un bloc contenant
    l'arbre <TeXmacs> <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-new-list sep ltm)><explain-synopsis|liste séparée>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> en concaténant tous les éléments
    de la liste <scm|ltm> (ses éléments sont des arbres <TeXmacs>) en les
    séparant par l'arbre <TeXmacs> <scm|sep>.
  </explain>

  <\explain>
    <scm|(bib-new-list-spc ltm)><explain-synopsis|liste séparée par des
    blancs>
  <|explain>
    Cette fonction est équivalente à l'appel de <scm|(bib-new-list " " ltm)>.
  </explain>

  <\explain>
    <scm|(bib-new-sentence ltm)><explain-synopsis|nouvelle phrase>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> correspondant à une phrase
    correctement ponctuée, contenant tous les éléments de la liste <scm|ltm>
    (ses éléments sont des arbres <TeXmacs>) séparés par des virgules.
  </explain>

  <\explain>
    <scm|(bib-null? v)><explain-synopsis|test à zéro>
  <|explain>
    Cette fonction renvoie le booléen <scm|#t> si la valeur <scm|v> est vide
    (<with|font-shape|italic|i.e.> la donnée vide correspondant à son type,
    comme la liste vide pour le type list) ; elle renvoie <scm|#f> dans le
    cas contraire.
  </explain>

  <\explain>
    <scm|(bib-prefix tm nbcar)><explain-synopsis|préfix d'un arbre <TeXmacs>>
  <|explain>
    Cette fonction renvoie une chaîne de caractères contenant les <scm|nbcar>
    premiers caractères de l'arbre <TeXmacs> <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-purify tm)><explain-synopsis|applatissement d'un arbre
    <TeXmacs>>
  <|explain>
    Cette fonction crée une chaîne de caractères à partir des suites de
    lettres de l'arbre <TeXmacs> <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-simplify tm)><explain-synopsis|simplification d'un arbre
    <TeXmacs>>
  <|explain>
    Cette fonction renvoie un arbre <TeXmacs> correspondant à la
    simplification de l'abre <TeXmacs> <scm|tm>, c'est-à-dire la
    concaténation des chaînes de caractères adjacentes, l'élimination des
    n÷uds inutiles, etc.
  </explain>

  <\explain>
    <scm|(bib-text-length tm)><explain-synopsis|longueur d'un arbre
    <TeXmacs>>
  <|explain>
    Cette fonction renvoie le nombre de caractères de l'arbre <TeXmacs>
    <scm|tm>.
  </explain>

  <\explain>
    <scm|(bib-translate msg)><explain-synopsis|traduction>
  <|explain>
    Cette fonction traduit le message <scm|msg> (de type string) de l'anglais
    vers la langue du document en cours d'édition.
  </explain>

  <\explain>
    <scm|(bib-upcase tm)><explain-synopsis|mise en majuscule>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> en mettant en majuscule toutes les
    lettres de l'arbre <TeXmacs> <scm|tm>, sauf celles englobées dans un bloc
    <verbatim|keepkase>.
  </explain>

  <\explain>
    <scm|(bib-upcase-first tm)><explain-synopsis|majuscule en tête>
  <|explain>
    Cette fonction crée un arbre <TeXmacs> en mettant en majuscule la
    première lettre de l'arbre <TeXmacs> <scm|tm>, sauf si elle est englobée
    dans un bloc <verbatim|keepkase>.
  </explain>

  <\explain>
    <scm|(bib-with-style style expr)><explain-synopsis|style local>
  <|explain>
    Cette fonction exécute l'expression <scm|expr> comme si le style en cours
    était <scm|style> (de type string).
  </explain>
</body>

<\initial>
  <\collection>
    <associate|language|french>
    <associate|preamble|false>
  </collection>
</initial>