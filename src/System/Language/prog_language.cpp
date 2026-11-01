/******************************************************************************
* MODULE     : prog_language.cpp
* DESCRIPTION: Owner-local KF6 highlighting for structured program text
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "impl_language.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "analyze.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "modification.hpp"
#include "unicode_text.hpp"
#include "program_syntax.hpp"
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

class token_state_rep final: public observer_rep {
public:
  std::vector<program_token> tokens;
  explicit token_state_rep (int size): tokens (size, program_token::code) {}
  void announce (tree& ref, modification) override {
    remove_observer (ref->obs, observer (this));
  }
};

token_state_rep* token_state (observer o) {
  if (is_nil (o)) return nullptr;
  if (auto* state= dynamic_cast<token_state_rep*> (o.operator-> ())) return state;
  if (o->get_type () != OBSERVER_LIST) return nullptr;
  auto* left= token_state (o->get_child (0));
  return left? left: token_state (o->get_child (1));
}

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
  std::vector<program_token> tokens;
  int line_offset= 0;
  int probe_offset= -1;
  program_token probe_token= program_token::code;

  void append (tree t) {
    if (is_atomic (t)) {
      size_t leaf= leaves.size ();
      leaves.push_back (t);
      const std::string_view source (t->label.data (), N(t->label));
      athena::text::require_utf8 (source);
      for (int pos=0; pos<N(t->label);) {
        int begin= pos;
        pos= athena::text::next_scalar (source, pos);
        if (pos == begin+1 && static_cast<unsigned char> (t->label[begin]) < 128) {
          text+= QChar::fromLatin1 (t->label[begin]);
          positions.push_back ({leaf, begin, pos});
          continue;
        }
        QString character_text= QString::fromUtf8 (
          t->label.data () + begin, pos - begin);
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
    else if (is_compound (t, "math")) {
      text+= QChar::ObjectReplacementCharacter;
      positions.push_back ({size_t(-1), 0, 0});
    }
    else {
      for (int i=0; i<N(t); ++i) {
        if (i > 0 && (is_func (t, DOCUMENT) || is_func (t, PARA))) {
          text+= QChar::LineFeed;
          positions.push_back ({size_t(-1), 0, 0});
        }
        if (is_func (t, CONCAT) || is_func (t, DOCUMENT) ||
            athena::contracts::editing_query {athena::contracts::current_evaluation_state ().contracts}.accessible (t, i))
          append (t[i]);
      }
    }
  }

  void applyFormat (int offset, int length, const Format& format) override {
    QColor color= format.textColor (theme ());
    int value= format.textStyle () == Theme::Normal || !color.isValid ()?
      0: int(color.rgb () & 0xffffff) + 1;
    program_token token= program_token::code;
    switch (format.textStyle ()) {
    case Theme::Char: case Theme::SpecialChar: case Theme::String:
    case Theme::VerbatimString: case Theme::SpecialString:
      token= program_token::string; break;
    case Theme::Comment: case Theme::Documentation: case Theme::Annotation:
    case Theme::CommentVar: case Theme::RegionMarker: case Theme::Information:
    case Theme::Warning: case Theme::Alert:
      token= program_token::comment; break;
    default: break;
    }
    if (probe_offset >= 0) {
      if (line_offset+offset <= probe_offset && probe_offset < line_offset+offset+length)
        probe_token= token;
      return;
    }
    for (int i=offset; i<offset+length; ++i) {
      colors[line_offset+i]= value;
      tokens[line_offset+i]= token;
    }
  }

public:
  explicit program_highlighter (Definition definition) {
    setDefinition (definition);
    setTheme (font_domain_local<syntax_repository> ().repository.defaultTheme (
      Repository::LightTheme));
  }

  program_token insertion_token (tree line, tree leaf, int offset, State state) {
    append (line);
    int position= 0;
    size_t target= 0;
    while (target < leaves.size () && leaves[target].operator-> () != leaf.operator-> ())
      ++target;
    for (size_t i=0; i<positions.size (); ++i) {
      const auto& p= positions[i];
      if (p.leaf < target || (p.leaf == target && p.end <= offset))
        position= int(i)+1;
    }
    // Classify a character inserted at the caret, not the preceding quote.
    // The preceding-line state retains multiline string/comment context.
    text= text.left (position) + QLatin1Char ('x');
    probe_offset= position;
    line_offset= 0;
    while (true) {
      int end= text.indexOf (QChar::LineFeed, line_offset);
      if (end < 0) end= text.size ();
      state= highlightLine (QStringView(text).mid(line_offset,end-line_offset),state);
      if (end == text.size ()) break;
      line_offset= end+1;
    }
    return probe_token;
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
    tokens.assign (positions.size (), program_token::code);
    State after= before;
    line_offset= 0;
    while (true) {
      int end= text.indexOf (QChar::LineFeed, line_offset);
      if (end < 0) end= text.size ();
      after= highlightLine (QStringView (text).mid (line_offset, end-line_offset), after);
      if (end == text.size ()) break;
      line_offset= end+1;
    }
    for (tree leaf: leaves) {
      if (auto* old= token_state (leaf->obs))
        remove_observer (leaf->obs, observer (old));
      attach_observer (leaf, tm_new<token_state_rep> (N(leaf->label)));
      attach_highlight (leaf, language, 0, 0, N(leaf->label));
    }
    for (size_t i=0; i<positions.size (); ++i) {
      const auto& p= positions[i];
      if (p.leaf != size_t(-1)) {
        attach_highlight (leaves[p.leaf], language, colors[i], p.begin, p.end);
        auto* state= token_state (leaves[p.leaf]->obs);
        for (int j=p.begin; j<p.end; ++j) state->tokens[j]= tokens[i];
      }
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

void prepare_program_syntax (tree source, string name) {
  language lan= prog_language (name);
  if (!has_highlight (source, lan->hl_lan)) lan->highlight (source);
}

program_token program_token_at (tree leaf, int offset) {
  auto* state= token_state (leaf->obs);
  if (!state || offset < 0 || offset >= int(state->tokens.size ()))
    return program_token::code;
  return state->tokens[offset];
}

program_token program_insertion_token (tree document, int row, tree leaf,
                                       int offset, string name) {
  auto definition= definition_for (name);
  if (!definition.isValid ()) return program_token::code;
  language lan= prog_language (name);
  auto* cached= line_state (document[row]->obs, lan->hl_lan);
  program_highlighter probe (definition);
  return probe.insertion_token (document[row],leaf,offset,cached?cached->before:State());
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
