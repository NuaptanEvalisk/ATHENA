<TeXmacs|1.99.19>

<style|source>

<\body>
  <active*|<\src-title>
    <src-package|section-namespace-export|1.0>

    <\src-purpose>
      Extra book hierarchy levels for namespace exports.
    </src-purpose>

    <src-copyright|2026|Felix>

    <\src-license>
      This software falls under the <hlink|GNU general public license,
      version 3 or later|$ATHENA_PATH/LICENSE>. It comes WITHOUT ANY
      WARRANTY WHATSOEVER. You should have received a copy of the license
      which the software. If not, see <hlink|http://www.gnu.org/licenses/gpl-3.0.html|http://www.gnu.org/licenses/gpl-3.0.html>.
    </src-license>
  </src-title>>

  <use-package|section-book>

  <new-section|subchapter>

  <new-section|subsubchapter>

  <new-section|subsubsubchapter>

  <new-section|subsubsubsubchapter>

  <new-section|subsubsubsubsubchapter>

  <new-section|subsubsubsubsubsubchapter>

  <assign|chapter-clean|<macro|<reset-subchapter><subchapter-clean><reset-std-env>>>

  <assign|subchapter-clean|<macro|<reset-subsubchapter><subsubchapter-clean>>>

  <assign|subsubchapter-clean|<macro|<reset-subsubsubchapter><subsubsubchapter-clean>>>

  <assign|subsubsubchapter-clean|<macro|<reset-subsubsubsubchapter><subsubsubsubchapter-clean>>>

  <assign|subsubsubsubchapter-clean|<macro|<reset-subsubsubsubsubchapter><subsubsubsubsubchapter-clean>>>

  <assign|subsubsubsubsubchapter-clean|<macro|<reset-subsubsubsubsubsubchapter><subsubsubsubsubsubchapter-clean>>>

  <assign|subsubsubsubsubsubchapter-clean|<macro|<reset-section><section-clean>>>

  <assign|display-subchapter|<macro|nr|<chapter-prefix><arg|nr>>>

  <assign|display-subsubchapter|<macro|nr|<subchapter-prefix><arg|nr>>>

  <assign|display-subsubsubchapter|<macro|nr|<subsubchapter-prefix><arg|nr>>>

  <assign|display-subsubsubsubchapter|<macro|nr|<subsubsubchapter-prefix><arg|nr>>>

  <assign|display-subsubsubsubsubchapter|<macro|nr|<subsubsubsubchapter-prefix><arg|nr>>>

  <assign|display-subsubsubsubsubsubchapter|<macro|nr|<subsubsubsubsubchapter-prefix><arg|nr>>>

  <assign|namespace-export-section-prefix|<macro|<if|<subsubsubsubsubsubchapter-numbered>|<subsubsubsubsubsubchapter-prefix>|<if|<subsubsubsubsubchapter-numbered>|<subsubsubsubsubchapter-prefix>|<if|<subsubsubsubchapter-numbered>|<subsubsubsubchapter-prefix>|<if|<subsubsubchapter-numbered>|<subsubsubchapter-prefix>|<if|<subsubchapter-numbered>|<subsubchapter-prefix>|<if|<subchapter-numbered>|<subchapter-prefix>|<chapter-prefix>>>>>>>>>

  <assign|display-section|<macro|nr|<namespace-export-section-prefix><arg|nr>>>

  <assign|part-header|<macro|name|<header-primary|<arg|name>|<if|<part-numbered>|<the-part>>|<part-text>>>>

  <assign|subchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subchapter-numbered>|<the-subchapter>>|<subchapter-text>>>>

  <assign|subsubchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subsubchapter-numbered>|<the-subsubchapter>>|<subsubchapter-text>>>>

  <assign|subsubsubchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subsubsubchapter-numbered>|<the-subsubsubchapter>>|<subsubsubchapter-text>>>>

  <assign|subsubsubsubchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subsubsubsubchapter-numbered>|<the-subsubsubsubchapter>>|<subsubsubsubchapter-text>>>>

  <assign|subsubsubsubsubchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subsubsubsubsubchapter-numbered>|<the-subsubsubsubsubchapter>>|<subsubsubsubsubchapter-text>>>>

  <assign|subsubsubsubsubsubchapter-header|<macro|name|<header-secondary|<arg|name>|<if|<subsubsubsubsubsubchapter-numbered>|<the-subsubsubsubsubsubchapter>>|<subsubsubsubsubsubchapter-text>>>>

  <assign|subchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|3fn><huge|<arg|name>><vspace|1fn>>>>>

  <assign|subsubchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|2.5fn><very-large|<arg|name>><vspace|0.8fn>>>>>

  <assign|subsubsubchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|2fn><large|<arg|name>><vspace|0.6fn>>>>>

  <assign|subsubsubsubchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|1.5fn><large|<arg|name>><vspace|0.5fn>>>>>

  <assign|subsubsubsubsubchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|1.25fn><arg|name><vspace|0.4fn>>>>>

  <assign|subsubsubsubsubsubchapter-title|<macro|name|<style-with|src-compact|none|<sectional-normal-bold|<vspace*|1fn><arg|name><vspace|0.4fn>>>>>

  <assign|subchapter-toc|<macro|name|<toc-normal-1|<toc-title|subchapter|<arg|name>>>>>

  <assign|subsubchapter-toc|<macro|name|<toc-normal-2|<toc-title|subsubchapter|<arg|name>>>>>

  <assign|subsubsubchapter-toc|<macro|name|<toc-normal-3|<toc-title|subsubsubchapter|<arg|name>>>>>

  <assign|subsubsubsubchapter-toc|<macro|name|<toc-small-1|<toc-title|subsubsubsubchapter|<arg|name>>>>>

  <assign|subsubsubsubsubchapter-toc|<macro|name|<toc-small-2|<toc-title|subsubsubsubsubchapter|<arg|name>>>>>

  <assign|subsubsubsubsubsubchapter-toc|<macro|name|<toc-small-2|<toc-title|subsubsubsubsubsubchapter|<arg|name>>>>>

  \;
</body>

<\initial>
  <\collection>
    <associate|preamble|true>
  </collection>
</initial>
