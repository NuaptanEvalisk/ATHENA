// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "macro_analysis_core.hpp"
#include "descriptor_values.hpp"
#include "analyze.hpp"
#include "vars.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <tuple>

namespace athena::contracts {

bool analysis_field_input::operator == (const analysis_field_input& b) const {
  return std::tie (type, access, type_origin, access_origin, environment_origin, environment, name) ==
    std::tie (b.type, b.access, b.type_origin, b.access_origin, b.environment_origin, b.environment, b.name);
}

bool analysis_contract_input::operator == (const analysis_contract_input& b) const {
  return std::tie (name, layout, type, variable_type, with_like, type_origin,
    layout_origin, variable_type_origin, with_origin, fields) ==
    std::tie (b.name, b.layout, b.type, b.variable_type, b.with_like, b.type_origin,
      b.layout_origin, b.variable_type_origin, b.with_origin, b.fields);
}

namespace {

void check_cancelled (const std::atomic<bool>* cancelled) {
  if (cancelled && cancelled->load (std::memory_order_relaxed))
    throw std::runtime_error ("Macro analysis cancelled");
}

std::string bytes (const string& value) {
  return std::string (value.data (), N(value));
}
string native (const std::string& value) {
  return string (value.data (), value.size ());
}

// Missing keys inherit. A present null value is a conflict, not inheritance.
// Keeping that distinction makes cyclic propagation finite: a concrete value
// can become ambiguous, but an ambiguous value never becomes concrete again.
using environment= std::map<std::string, std::optional<tree>>;

void join_environment (environment& current, const environment& incoming) {
  for (auto& [key, value]: current) {
    const auto found= incoming.find (key);
    if (found == incoming.end () || value != found->second) value.reset ();
  }
  for (const auto& [key, value]: incoming)
    if (!current.count (key)) current.emplace (key, std::nullopt);
}

struct usage {
  bool seen= false, visible= false, guaranteed= false, dynamic_environment= false;
  bool ambiguous_type= false;
  std::set<int> types;
  std::optional<environment> context;

  void join (const usage& other) {
    seen |= other.seen;
    visible |= other.visible;
    guaranteed |= other.guaranteed;
    dynamic_environment |= other.dynamic_environment;
    ambiguous_type |= other.ambiguous_type;
    types.insert (other.types.begin (), other.types.end ());
    if (other.context) {
      if (context) join_environment (*context, *other.context);
      else context= *other.context;
    }
  }

  std::set<int> type_constraints () const {
    auto constraints= types;
    if (constraints.size () > 1) constraints.erase (TYPE_REGULAR);
    return constraints;
  }

  inference_state type_state () const {
    const auto constraints= type_constraints ();
    if (ambiguous_type || constraints.size () > 1) return inference_state::ambiguous;
    return constraints.empty () ? inference_state::unresolved : inference_state::resolved;
  }

  int type () const {
    const auto constraints= type_constraints ();
    return !ambiguous_type && constraints.size () == 1 ? *constraints.begin () : TYPE_UNKNOWN;
  }

  bool operator == (const usage& other) const {
    return seen == other.seen && visible == other.visible &&
      guaranteed == other.guaranteed && dynamic_environment == other.dynamic_environment &&
      ambiguous_type == other.ambiguous_type && types == other.types && context == other.context;
  }
};

void dependencies (const tree& source, std::set<std::string>& result,
                   const std::map<tree_label, std::string>& symbols) {
  if (is_atomic (source)) return;
  result.insert (symbols.at (L(source)));
  if (is_func (source, MACRO) || is_func (source, XMACRO)) return;
  if ((is_func (source, COMPOUND) || is_func (source, VALUE) ||
       is_func (source, QUOTE_VALUE) || is_func (source, EXTERN)) &&
      N(source) > 0 && is_atomic (source[0]))
    result.insert ((L(source) == EXTERN ? "extern:" : "") + bytes (source[0]->label));
  if (is_func (source, MAP_ARGS) && N(source) >= 3)
    for (int i=0; i<2; ++i)
      if (is_atomic (source[i])) result.insert (bytes (source[i]->label));
  for (int i=0; i<N(source); ++i) dependencies (source[i], result, symbols);
}

int minimal_arguments (const tree& source, const tree& formal) {
  if (is_atomic (source) || is_func (source, MACRO) || is_func (source, XMACRO)) return 0;
  if (is_func (source, ARG, 2) && source[0] == formal && is_int (source[1]))
    return as_int (source[1]) + 1;
  if (is_func (source, MAP_ARGS) && N(source) >= 4 &&
      source[2] == formal && is_int (source[3])) {
    const int start= as_int (source[3]);
    return N(source) == 5 && is_int (source[4]) ?
      std::max (start, as_int (source[4])) : start;
  }
  int count= 0;
  for (int i=0; i<N(source); ++i)
    count= std::max (count, minimal_arguments (source[i], formal));
  return count;
}

bool literal_length (const string& value) {
  int i= 0;
  while (i<N(value) && !is_locase (value[i])) ++i;
  return is_double (value (0, i)) && is_locase_alpha (value (i, N(value)));
}

struct definition {
  tree source;
  std::set<std::string> dependencies;
  std::vector<usage> fields;
};

struct field {
  int type= TYPE_UNKNOWN, access= ACCESSIBLE_NEVER;
  property_authority type_origin= property_authority::inferred;
  property_authority access_origin= property_authority::inferred;
  property_authority environment_origin= property_authority::inferred;
  tree environment= tree (ATTR);
  std::string name;
  field_analysis analysis;

  bool operator == (const field& other) const {
    return type == other.type && access == other.access &&
      type_origin == other.type_origin && access_origin == other.access_origin &&
      environment_origin == other.environment_origin && environment == other.environment &&
      name == other.name && analysis == other.analysis;
  }
};

struct contract {
  field_layout layout;
  int type= TYPE_REGULAR, variable_type= VAR_MACRO;
  bool with_like= false;
  property_authority type_origin= property_authority::inferred;
  property_authority layout_origin= property_authority::inferred;
  property_authority variable_type_origin= property_authority::inferred;
  property_authority with_origin= property_authority::inferred;
  std::vector<field> children;

  explicit contract (const analysis_contract_input& input):
    layout (input.layout), type (input.type), variable_type (input.variable_type),
    with_like (input.with_like), type_origin (input.type_origin),
    layout_origin (input.layout_origin), variable_type_origin (input.variable_type_origin),
    with_origin (input.with_origin) {
    for (const auto& child: input.fields)
      children.push_back ({child.type, child.access, child.type_origin,
        child.access_origin, child.environment_origin, tree (child.environment), child.name});
  }
  contract ()= default;
  bool operator == (const contract& other) const {
    return layout == other.layout && type == other.type &&
      variable_type == other.variable_type && with_like == other.with_like &&
      type_origin == other.type_origin && layout_origin == other.layout_origin &&
      variable_type_origin == other.variable_type_origin && with_origin == other.with_origin &&
      children == other.children;
  }
  bool operator != (const contract& other) const { return !(*this == other); }
};

template<typename T>
void infer (T& target, const T& value, property_authority authority) {
  if (authority == property_authority::inferred) target= value;
}

bool needs_analysis (const contract& info) {
  const auto inferred= property_authority::inferred;
  if (info.type_origin == inferred || info.layout_origin == inferred ||
      info.with_origin == inferred || info.variable_type_origin == inferred)
    return true;
  for (const auto& child: info.children)
    if (child.type_origin == inferred || child.access_origin == inferred ||
        child.environment_origin == inferred) return true;
  return false;
}

} // namespace

struct macro_analysis_core::impl {
  std::map<tree_label, std::string> symbols;
  std::map<std::string, tree_label> labels;
  std::map<std::string, contract> contracts;
  std::map<std::string, definition> definitions;
  std::map<std::string, std::set<std::string>> dependants;
  std::set<std::string> dirty;

  void remember_symbol (tree_label label) {
    if (symbols.count (label)) return;
    const auto name= bytes (as_string (label));
    symbols.emplace (label, name);
    labels.emplace (name, label);
  }

  void discover_symbols (const tree& source, const std::atomic<bool>* cancelled) {
    // This is a worker-private thawed expression. The process label catalog
    // supplies spelling only; bindings and declarations come from the input.
    std::vector<const tree*> pending {&source};
    while (!pending.empty ()) {
      check_cancelled (cancelled);
      const tree& value= *pending.back ();
      pending.pop_back ();
      remember_symbol (L(value));
      if (is_atomic (value)) continue;
      if (is_func (value, COMPOUND) || is_func (value, MAP_ARGS))
        for (int i=0; i<(L(value) == MAP_ARGS ? 2 : 1) && i<N(value); ++i)
          if (is_atomic (value[i]) && existing_tree_label (value[i]->label))
            remember_symbol (as_tree_label (value[i]->label));
      for (int i=0; i<N(value); ++i) pending.push_back (&value[i]);
    }
  }

  const contract& lookup (const std::string& name) const {
    const auto found= contracts.find (name);
    static const contract unknown;
    return found == contracts.end () ? unknown : found->second;
  }

  const contract& descriptor (const tree& value) const {
    if (is_func (value, EXTERN) && N(value) > 0 && is_atomic (value[0])) {
      const std::string name= "extern:" + bytes (value[0]->label);
      if (contracts.count (name)) return lookup (name);
    }
    return lookup (symbols.at (L(value)));
  }

  const usage* inferred_field (const tree& value, int index) const {
    const auto found= definitions.find (symbols.at (L(value)));
    if (found == definitions.end () || index < 0 ||
        index >= int (found->second.fields.size ())) return nullptr;
    return &found->second.fields[index];
  }

  bool context_ready (const tree& value, int index) const {
    const auto& info= descriptor (value);
    if (info.children[index].environment_origin != property_authority::inferred) return true;
    const auto* field= inferred_field (value, index);
    return field == nullptr || (field->context.has_value () && !field->dynamic_environment);
  }

  environment child_environment (const tree& value, int child,
                                  environment context) const {
    const auto& info= descriptor (value);
    const int index= info.layout.index (child, N(value));
    if (index < 0 || index >= int(info.children.size ())) return context;
    const auto& field= info.children[index];
    const tree entries= field.environment;
    for (int i=0; i+1<N(entries); i+=2) {
      if (!is_atomic (entries[i])) continue;
      tree entry= entries[i+1];
      if (is_func (entry, ARG, 1) && is_int (entry[0])) {
        const int actual= as_int (entry[0]);
        if (actual >= 0 && actual < N(value)) entry= value[actual];
      }
      context[bytes (entries[i]->label)]= entry;
    }
    if (field.environment_origin == property_authority::inferred)
      if (const auto* inferred= inferred_field (value, index))
        if (inferred->context)
          for (const auto& [key, entry]: *inferred->context)
            if (!entry) context[key]= std::nullopt;
    return context;
  }

  usage argument (const tree& value, const tree& formal,
                  const environment& context, int expected= TYPE_UNKNOWN,
                  bool visible= true, bool guaranteed= true,
                  bool ready= true, bool ambiguous_type= false) const {
    usage result;
    if (is_atomic (value)) return result;
    if (value == formal || (is_func (value, QUOTE_ARG, 1) &&
                            N(formal) == 1 && value[0] == formal[0])) {
      result.seen= true;
      result.ambiguous_type= ambiguous_type;
      if (expected != TYPE_UNKNOWN && expected != TYPE_INVALID && expected != TYPE_ADHOC)
        result.types.insert (expected);
      result.visible= visible;
      result.guaranteed= visible && guaranteed;
      if (visible && ready) result.context= context;
      return result;
    }
    if (is_func (value, MACRO) || is_func (value, XMACRO)) return result;
    if ((is_func (value, IF) || is_func (value, VAR_IF)) && N(value) >= 2) {
      usage first= argument (value[1], formal, context, expected, visible, guaranteed, ready, ambiguous_type);
      usage second;
      if (N(value) == 3)
        second= argument (value[2], formal, context, expected, visible, guaranteed, ready, ambiguous_type);
      result.join (first);
      result.join (second);
      result.guaranteed= first.guaranteed && second.guaranteed;
      return result;
    }
    if (is_func (value, CASE) && N(value) >= 2) {
      bool all= (N(value) & 1) != 0;
      for (int i=1; i<N(value); i+=2) {
        const auto branch= argument (value[i], formal, context, expected, visible, guaranteed, ready, ambiguous_type);
        all= all && branch.guaranteed;
        result.join (branch);
      }
      if (N(value) & 1) {
        const auto branch= argument (value[N(value)-1], formal, context, expected, visible, guaranteed, ready, ambiguous_type);
        all= all && branch.guaranteed;
        result.join (branch);
      }
      result.guaranteed= all;
      return result;
    }
    if (is_func (value, WITH) && N(value) > 0) {
      environment local= context;
      bool dynamic= false;
      for (int i=0; i+1<N(value); i+=2)
        if (is_atomic (value[i])) local[bytes (value[i]->label)]= value[i+1];
        else dynamic= true;
      result= argument (value[N(value)-1], formal, local, expected, visible, guaranteed, ready, ambiguous_type);
      result.dynamic_environment |= dynamic;
      return result;
    }
    if (is_func (value, TFORMAT) && N(value) > 0) {
      environment local= context;
      const auto previous= local.find (bytes (CELL_FORMAT));
      if (previous == local.end () || previous->second) {
        tree format= previous == local.end () ? tree (TFORMAT) : *previous->second;
        format= format * tree (TFORMAT, A (value (0, N(value)-1)));
        local[bytes (CELL_FORMAT)]= format;
      }
      const int last= N(value)-1;
      return argument (value[last], formal, child_environment (value, last, local),
                       expected, visible, guaranteed, ready, ambiguous_type);
    }
    if (is_func (value, COMPOUND) && N(value) > 0 && is_atomic (value[0])) {
      const auto found= labels.find (bytes (value[0]->label));
      if (found == labels.end ()) return result;
      const tree expanded (found->second, A (value (1, N(value))));
      return argument (expanded, formal, context, expected, visible, guaranteed, ready, ambiguous_type);
    }
    if (is_func (value, MAP_ARGS) && N(value) >= 3 && value[2] == formal[0]) {
      if (!is_atomic (value[0]) || !is_atomic (value[1]) ||
          (value[0] != "identity" && !labels.count (bytes (value[0]->label))) ||
          !labels.count (bytes (value[1]->label))) return result;
      if (N(formal) != 2 || !is_int (formal[1])) return result;
      if ((N(value) >= 4 && !is_int (value[3])) ||
          (N(value) == 5 && !is_int (value[4]))) {
        result.dynamic_environment= true;
        return result;
      }
      const int start= N(value) >= 4 ? as_int (value[3]) : 0;
      const int source_index= as_int (formal[1]);
      if (source_index < start || (N(value) == 5 && source_index >= as_int (value[4])))
        return result;
      const tree_label outer= labels.at (bytes (value[1]->label));
      const auto& outer_info= lookup (bytes (value[1]->label));
      const int first_count= N(value) == 5 ? as_int (value[4])-start :
        std::max (source_index-start+1, outer_info.layout.minimum ());
      // A variable suffix can move this argument between descriptor fields.
      // Once it is in the repeated prefix, increasing arity adds no new case.
      const int last_count= N(value) == 5 ? first_count :
        std::max (first_count, source_index-start+outer_info.layout.base+
                                   outer_info.layout.extra+1);
      bool all= true, any= false;
      for (int count=first_count; count<=last_count; ++count) {
        if (!outer_info.layout.accepts (count)) continue;
        tree expansion (outer, count);
        for (int i=0; i<count; ++i) {
          const tree forwarded (ARG, value[2], as_string (start+i));
          expansion[i]= value[0] == "identity" ? forwarded :
            tree (labels.at (bytes (value[0]->label)), forwarded, as_string (start+i));
        }
        const auto branch= argument (expansion, formal, context, expected,
                                     visible, guaranteed, ready, ambiguous_type);
        result.join (branch);
        all= all && branch.guaranteed;
        any= true;
      }
      result.guaranteed= any && all;
      return result;
    }
    const auto& info= descriptor (value);
    for (int i=0; i<N(value); ++i) {
      const int index= info.layout.index (i, N(value));
      if (index < 0 || index >= int(info.children.size ())) continue;
      const auto& field= info.children[index];
      int type= field.type;
      if (type == TYPE_BINDING) type= (i & 1) ? TYPE_REGULAR : TYPE_VARIABLE;
      const bool access= visible && field.access == ACCESSIBLE_ALWAYS;
      bool certain= guaranteed;
      if (field.access_origin == property_authority::inferred)
        if (const auto* inferred= inferred_field (value, index))
          certain= certain && inferred->guaranteed;
      result.join (argument (value[i], formal, child_environment (value, i, context),
                             type, access, certain, ready && context_ready (value, index),
                             field.analysis.type == inference_state::ambiguous));
    }
    return result;
  }

  bool with_like (const tree& value, const tree& formal) const {
    if (value == tree (ARG, formal)) return true;
    if (is_atomic (value)) return false;
    if ((is_func (value, IF) || is_func (value, VAR_IF)) && N(value) == 3)
      return with_like (value[1], formal) && with_like (value[2], formal);
    return N(value)>0 && descriptor (value).with_like &&
      with_like (value[N(value)-1], formal);
  }

  void reset (const std::string& name) {
    auto& info= contracts[name];
    if (!needs_analysis (info)) return;
    infer (info.type, TYPE_UNKNOWN, info.type_origin);
    infer (info.with_like, false, info.with_origin);
    infer (info.variable_type, VAR_MACRO, info.variable_type_origin);
    for (auto& child: info.children) {
      infer (child.type, TYPE_UNKNOWN, child.type_origin);
      infer (child.access, ACCESSIBLE_NEVER, child.access_origin);
      infer (child.environment, tree (ATTR), child.environment_origin);
      child.analysis= {};
    }
    const auto found= definitions.find (name);
    if (found != definitions.end ())
      found->second.fields.assign (info.children.size (), usage {});
    else if (info.layout_origin == property_authority::inferred) {
      info.layout= {};
      info.children.clear ();
    }
  }

  void initialize_signature (const std::string& name, definition& definition) {
    const tree value= definition.source;
    auto& info= contracts[name];
    if (info.layout_origin == property_authority::inferred) {
      if (is_func (value, MACRO) && N(value)>0)
        info.layout= {N(value)-1, 0, arity_kind::fixed, field_mapping::detailed};
      else if (is_func (value, XMACRO, 2))
        info.layout= {minimal_arguments (value[1], value[0]), 1,
                       arity_kind::repeated_suffix, field_mapping::detailed};
      else info.layout= {};
      info.children.resize (info.layout.fields ());
    }
    if (is_func (value, MACRO) && info.layout_origin != property_authority::builtin)
      for (int i=0; i+1<N(value) && i<int(info.children.size ()); ++i)
        if (is_atomic (value[i])) info.children[i].name= bytes (value[i]->label);
    definition.fields.resize (info.children.size ());
  }

  bool evaluate (const std::string& name, definition& definition) {
    auto& info= contracts.at (name);
    if (!needs_analysis (info)) return false;
    const auto before= info;
    const auto previous_fields= definition.fields;
    const tree value= definition.source;
    const bool macro= is_func (value, MACRO) && N(value)>0;
    const bool xmacro= is_func (value, XMACRO, 2);
    if (macro || xmacro) {
      const int last= N(value)-1;
      int type= descriptor (value[last]).type;
      if (macro && last == 0 && !is_atomic (value[0]) && N(value[0]) == 1 &&
          symbols.at (L(value[0])) == "localize" && is_atomic (value[0][0])) {
        type= TYPE_STRING;
        infer (info.variable_type, VAR_MACRO_PARAMETER, info.variable_type_origin);
      }
      else if (macro && last == 0 && is_atomic (value[0]) && literal_length (value[0]->label)) {
        type= TYPE_LENGTH;
        infer (info.variable_type, VAR_MACRO_PARAMETER, info.variable_type_origin);
      }
      infer (info.type, type, info.type_origin);
      infer (info.with_like, macro && last>0 && with_like (value[last], value[last-1]),
             info.with_origin);
      for (int i=0; i<int(definition.fields.size ()); ++i) {
        if (macro && i>=last) break;
        const tree formal= macro ? tree (ARG, value[i]) : tree (ARG, value[0], as_string (i));
        auto& usage= definition.fields[i];
        auto& child= info.children[i];
        usage.join (argument (value[last], formal, environment {}));
        infer (child.type, usage.type (), child.type_origin);
        infer (child.access, usage.visible ? ACCESSIBLE_ALWAYS : ACCESSIBLE_NEVER,
               child.access_origin);
        auto& evidence= child.analysis;
        evidence.type= child.type_origin == property_authority::inferred ?
          usage.type_state () : inference_state::resolved;
        evidence.conditional= usage.visible && !usage.guaranteed;
        evidence.access= child.access_origin != property_authority::inferred ? inference_state::resolved :
          evidence.conditional ? inference_state::dynamic :
          usage.seen ? inference_state::resolved : inference_state::unresolved;
        evidence.environment= usage.dynamic_environment ? inference_state::dynamic :
          usage.context ? inference_state::resolved : inference_state::unresolved;
        evidence.conflicts.clear ();
        if (evidence.type == inference_state::ambiguous) evidence.conflicts.push_back ("type");
        if (usage.dynamic_environment) evidence.conflicts.push_back ("dynamic-environment");
        tree entries (ATTR);
        if (usage.context && !usage.dynamic_environment)
          for (const auto& [key, input]: *usage.context) {
            if (!input) {
              evidence.environment= inference_state::ambiguous;
              evidence.conflicts.push_back (key);
              continue;
            }
            tree entry= *input;
            if (macro && is_func (entry, ARG, 1))
              for (int j=0; j<last; ++j)
                if (value[j] == entry[0]) { entry= tree (ARG, as_tree (j)); break; }
            entries << native (key) << entry;
          }
        infer (child.environment, entries, child.environment_origin);
        if (child.environment_origin != property_authority::inferred)
          evidence.environment= inference_state::resolved;
      }
    }
    else {
      infer (info.variable_type, VAR_PARAMETER, info.variable_type_origin);
      int type= descriptor (value).type;
      const string var= native (name);
      if (var == "color" || ends (var, "-color") || ends (var, "-recolor")) type= TYPE_COLOR;
      else if (ends (var, "-length") || ends (var, "-width")) type= TYPE_LENGTH;
      else if (is_atomic (value)) {
        const string text= value->label;
        type= text == "true" || text == "false" ? TYPE_BOOLEAN :
          is_int (text) ? TYPE_INTEGER : is_double (text) ? TYPE_NUMERIC :
          literal_length (text) ? TYPE_LENGTH : TYPE_STRING;
      }
      infer (info.type, type, info.type_origin);
    }
    return before != info || previous_fields != definition.fields;
  }

  std::vector<analysis_contract_result> apply (macro_analysis_input input,
                                              const std::atomic<bool>* cancelled) {
    for (auto& [label, name]: input.symbols) {
      symbols[label]= name;
      labels[name]= label;
    }
    for (const auto& item: input.contracts) {
      contracts[item.name]= contract (item);
      dirty.insert (item.name);
    }
    for (const auto& item: input.changed) {
      check_cancelled (cancelled);
      auto& definition= definitions[item.name];
      for (const auto& dependency: definition.dependencies)
        dependants[dependency].erase (item.name);
      definition.source= tree (item.expression);
      const tree& value= definition.source;
      discover_symbols (value, cancelled);
      definition.dependencies.clear ();
      if ((is_func (value, MACRO) || is_func (value, XMACRO)) && N(value)>0)
        dependencies (value[N(value)-1], definition.dependencies, symbols);
      else dependencies (value, definition.dependencies, symbols);
      for (const auto& dependency: definition.dependencies)
        dependants[dependency].insert (item.name);
      dirty.insert (item.name);
    }
    for (const auto& name: input.removed) {
      auto found= definitions.find (name);
      if (found == definitions.end ()) continue;
      for (const auto& dependency: found->second.dependencies)
        dependants[dependency].erase (name);
      definitions.erase (found);
      dirty.insert (name);
    }
    std::deque<std::string> queue (dirty.begin (), dirty.end ());
    while (!queue.empty ()) {
      check_cancelled (cancelled);
      const std::string name= queue.front (); queue.pop_front ();
      for (const auto& dependant: dependants[name])
        if (dirty.insert (dependant).second) queue.push_back (dependant);
    }
    for (const auto& name: dirty) {
      const auto found= definitions.find (name);
      if (found != definitions.end ()) initialize_signature (name, found->second);
    }
    for (const auto& name: dirty)
      if (definitions.count (name) ||
          std::find (input.removed.begin (), input.removed.end (), name) != input.removed.end ())
        reset (name);
    queue.assign (dirty.begin (), dirty.end ());
    std::set<std::string> queued= dirty;
    while (!queue.empty ()) {
      check_cancelled (cancelled);
      const std::string name= queue.front (); queue.pop_front (); queued.erase (name);
      const auto found= definitions.find (name);
      if (found == definitions.end () || !evaluate (name, found->second)) continue;
      for (const auto& dependant: dependants[name])
        if (dirty.count (dependant) && queued.insert (dependant).second)
          queue.push_back (dependant);
    }
    std::vector<analysis_contract_result> output;
    for (const auto& name: dirty) {
      if (!definitions.count (name) &&
          std::find (input.removed.begin (), input.removed.end (), name) == input.removed.end ())
        continue;
      const auto& info= contracts.at (name);
      analysis_contract_result result {name, info.layout, info.type,
                                        info.variable_type, info.with_like, {}};
      result.type_origin= info.type_origin;
      result.layout_origin= info.layout_origin;
      result.variable_type_origin= info.variable_type_origin;
      result.with_origin= info.with_origin;
      const auto source= definitions.find (name);
      if (source != definitions.end ())
        result.dependencies.assign (source->second.dependencies.begin (), source->second.dependencies.end ());
      for (const auto& child: info.children)
        result.fields.push_back ({child.type, child.access,
          node::frozen_tree (child.environment), child.name, child.analysis,
          child.type_origin, child.access_origin, child.environment_origin});
      output.push_back (std::move (result));
    }
    dirty.clear ();
    return output;
  }
};

macro_analysis_core::macro_analysis_core (): state (std::make_unique<impl> ()) {}
macro_analysis_core::~macro_analysis_core ()= default;
std::vector<analysis_contract_result>
macro_analysis_core::apply (macro_analysis_input input, const std::atomic<bool>* cancelled) {
  return state->apply (std::move (input), cancelled);
}

} // namespace athena::contracts
