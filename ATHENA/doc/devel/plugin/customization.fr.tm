<TeXmacs|1.0.1.11>

<style|tmdoc>

<\body>
  <expand|tmdoc-title|Personnalisation de l'interface>

  Une fois que vous aurez créé une première interface entre votre système et
  <apply|TeXmacs>, vous aurez sûrement envie de l'améliorer. Vous trouverez
  ci-dessous quelques idées pour le faire.

  Tout d'abord, vous pouvez personnaliser le comportement du clavier dans une
  session <verbatim|myplugin> et ajouter les menus désirés. Vous trouverez
  les explications pour le faire dans le chapitre consacré au langage
  d'extension <name|Guile/Scheme>. Vous pouvez intégrer vos changements au
  fichier <verbatim|init-myplugin.scm>. Nous vous recommandons d'examiner
  attentivement les plugins livrés avec <TeXmacs> et situés dans le
  répertoire <verbatim|$ATHENA_HOME_PATH/plugins>.

  Il vous faudra peut-être créer des balises spéciales pour certaines sorties
  sur votre système. Supposons que vous vouliez associer un type invisible à
  chaque sous-expression de sortie. Pour ce faire, vous pouvez créer une
  macro <verbatim|exprtype> à deux arguments dans <verbatim|myplugin.ts> et
  envoyez des appels <apply|LaTeX>, tel <verbatim|\\exprtype{1}{Integer}>, à
  <apply|TeXmacs> durant la sortie.

  Dans le cas où vous utilisez des tubes pour connecter votre système à
  <apply|TeXmacs>, vous pouvez exécuter directement des commandes
  <apply|TeXmacs> pendant la sortie de votre système en intégrant dans votre
  sortie des morceaux de code de la forme :

  <\verbatim>
    \ \ \ \ [DATA_BEGIN]command:scheme-program[DATA_END]
  </verbatim>

  À l'inverse, quand le curseur est dans une session système, vous pouvez
  utiliser la commande <name|Scheme> :

  <\verbatim>
    \ \ \ \ (extern-exec plugin-command)
  </verbatim>

  pour exécuter une commande du système.

  <apply|tmdoc-copyright|1998--2003|Joris van der Hoeven|Michèle Garoche>

  <expand|tmdoc-license|Permission is granted to copy, distribute and/or
  modify this document under the terms of the GNU Free Documentation License,
  Version 1.1 or any later version published by the Free Software Foundation;
  with no Invariant Sections, with no Front-Cover Texts, and with no
  Back-Cover Texts. A copy of the license is included in the section entitled
  "GNU Free Documentation License".>
</body>

<\initial>
  <\collection>
    <associate|paragraph width|150mm>
    <associate|odd page margin|30mm>
    <associate|shrinking factor|4>
    <associate|page right margin|30mm>
    <associate|page top margin|30mm>
    <associate|reduction page right margin|25mm>
    <associate|page type|a4>
    <associate|reduction page bottom margin|15mm>
    <associate|even page margin|30mm>
    <associate|reduction page left margin|25mm>
    <associate|page bottom margin|30mm>
    <associate|reduction page top margin|15mm>
    <associate|language|french>
  </collection>
</initial>

<\references>
  <\collection>
    <associate|idx-1|<tuple|<uninit>|?>>
    <associate|toc-1|<tuple|<uninit>|?>>
    <associate|idx-2|<tuple|<uninit>|?>>
    <associate|toc-2|<tuple|<uninit>|?>>
  </collection>
</references>
