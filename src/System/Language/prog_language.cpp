/******************************************************************************
* MODULE     : prog_language.cpp
* DESCRIPTION: Owner-local KF6 highlighting for structured program text
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "impl_language.hpp"
#include "analyze.hpp"
#include "drd_std.hpp"
#include "modification.hpp"
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QString>
#include <vector>

namespace {
using namespace KSyntaxHighlighting;

struct syntax_repository {
  Repository repository;
};

Definition definition_for (string name) {
  auto& repository= font_domain_local<syntax_repository> ().repository;
  QString key= QString::fromUtf8 (name.data (), N(name));
  if (key == "cpp") key= "C++";
  else if (key == "shell") key= "Bash";
  else if (key == "scm") key= "Scheme";
  auto definition= repository.definitionForName (key);
  if (definition.isValid ()) return definition;
  for (const auto& candidate: repository.definitions ())
    if (candidate.name ().compare (key, Qt::CaseInsensitive) == 0)
      return candidate;
  return repository.definitionForFileName ("source." + key);
}

// Owns no document tree; edits discard the state along with color observers.
class line_state_rep final: public observer_rep {
public:
  int language;
  Definition definition;
  State before, after;
  line_state_rep (int lan, Definition def, State in, State out):
    language (lan), definition (def), before (in), after (out) {}
  void announce (tree& ref, modification) override {
    remove_observer (ref->obs, observer (this));
  }
};

line_state_rep* line_state (observer o, int language) {
  if (is_nil (o)) return nullptr;
  if (auto* state= dynamic_cast<line_state_rep*> (o.operator-> ()))
    return state->language == language? state: nullptr;
  if (o->get_type () != OBSERVER_LIST) return nullptr;
  auto* left= line_state (o->get_child (0), language);
  return left? left: line_state (o->get_child (1), language);
}

bool highlights_valid (tree t, int language) {
  if (!has_highlight (t, language)) return false;
  if (is_compound (t))
    for (int i=0; i<N(t); ++i)
      if (!highlights_valid (t[i], language)) return false;
  return true;
}

class program_highlighter final: public AbstractHighlighter {
  struct character {
    size_t leaf;
    int begin, end;
  };
  std::vector<tree> leaves;
  std::vector<character> positions;
  QString text;
  std::vector<int> colors;
  int line_offset= 0;

  void append (tree t) {
    if (is_atomic (t)) {
      size_t leaf= leaves.size ();
      leaves.push_back (t);
      for (int pos=0; pos<N(t->label);) {
        int begin= pos;
        tm_char_forwards (t->label, pos);
        if (pos == begin+1 && static_cast<unsigned char> (t->label[begin]) < 128) {
          text+= QChar::fromLatin1 (t->label[begin]);
          positions.push_back ({leaf, begin, pos});
          continue;
        }
        string utf8= cork_to_utf8 (t->label (begin, pos));
        QString character_text= QString::fromUtf8 (utf8.data (), N(utf8));
        text+= character_text;
        for (int i=0; i<character_text.size (); ++i)
          positions.push_back ({leaf, begin, pos});
      }
    }
    else if (is_func (t, WITH) && N(t) > 0) append (t[N(t)-1]);
    else if (is_func (t, SURROUND, 3)) {
      append (t[0]); append (t[2]); append (t[1]);
    }
    else if (is_func (t, HIDDEN) || is_func (t, RAW_DATA)) return;
    else {
      for (int i=0; i<N(t); ++i) {
        if (i > 0 && (is_func (t, DOCUMENT) || is_func (t, PARA))) {
          text+= QChar::LineFeed;
          positions.push_back ({size_t(-1), 0, 0});
        }
        if (is_func (t, CONCAT) || is_func (t, DOCUMENT) ||
            the_drd->is_accessible_child (t, i))
          append (t[i]);
      }
    }
  }

  void applyFormat (int offset, int length, const Format& format) override {
    QColor color= format.textColor (theme ());
    int value= format.textStyle () == Theme::Normal || !color.isValid ()?
      0: int(color.rgb () & 0xffffff) + 1;
    for (int i=offset; i<offset+length; ++i) colors[line_offset+i]= value;
  }

public:
  explicit program_highlighter (Definition definition) {
    setDefinition (definition);
    setTheme (font_domain_local<syntax_repository> ().repository.defaultTheme (
      Repository::LightTheme));
  }

  State line (tree t, int language, const State& before) {
    if (auto* cached= line_state (t->obs, language)) {
      if (cached->definition == definition () &&
          cached->before == before && highlights_valid (t, language))
        return cached->after;
      // Invalidate boxes built with a different preceding-line context.
      observer old (cached);
      detach_highlight (t, language);
      remove_observer (t->obs, old);
    }
    leaves.clear (); positions.clear (); text.clear ();
    append (t);
    colors.assign (positions.size (), 0);
    State after= before;
    line_offset= 0;
    while (true) {
      int end= text.indexOf (QChar::LineFeed, line_offset);
      if (end < 0) end= text.size ();
      after= highlightLine (QStringView (text).mid (line_offset, end-line_offset), after);
      if (end == text.size ()) break;
      line_offset= end+1;
    }
    for (tree leaf: leaves)
      attach_highlight (leaf, language, 0, 0, N(leaf->label));
    for (size_t i=0; i<positions.size (); ++i) {
      const auto& p= positions[i];
      if (p.leaf != size_t(-1))
        attach_highlight (leaves[p.leaf], language, colors[i], p.begin, p.end);
    }
    attach_highlight (t, language);
    attach_observer (t, tm_new<line_state_rep> (language, definition (), before, after));
    leaves.clear (); positions.clear (); text.clear (); colors.clear ();
    return after;
  }
};

class kf6_language_rep final: public verb_language_rep {
  program_highlighter highlighter;
public:
  kf6_language_rep (string name, Definition definition):
    verb_language_rep (name), highlighter (definition) {
    const auto definitions= font_domain_local<syntax_repository> ().repository.definitions ();
    for (int i=0; i<definitions.size (); ++i)
      if (definitions[i] == definition) { hl_lan= -1-i; break; }
  }

  void highlight (tree t) override {
    check_owner ();
    State state;
    if (is_func (t, DOCUMENT)) {
      for (int i=0; i<N(t); ++i)
        state= highlighter.line (t[i], hl_lan, state);
      attach_highlight (t, hl_lan, 0, 0, 0);
    }
    else if (!highlights_valid (t, hl_lan))
      highlighter.line (t, hl_lan, state);
  }

  string get_color (tree t, int start, int end) override {
    auto colors= obtain_highlight (t, hl_lan);
    if (start >= end || start >= N(colors) || colors[start] == 0) return "";
    auto color= QColor::fromRgb (colors[start]-1).name ().toLatin1 ();
    return string (color.constData (), color.size ());
  }
};
}

bool prog_lang_exists (string name) {
  return definition_for (name).isValid ();
}

language prog_language (string name) {
  if (language::instances->contains (name)) return language (name);
  auto definition= definition_for (name);
  if (definition.isValid ())
    return language (tm_new<kf6_language_rep> (name, definition));
  return language (tm_new<verb_language_rep> (name));
}
