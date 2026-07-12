/******************************************************************************
* MODULE     : aofm_chatgpt.cpp
* DESCRIPTION: Repair ChatGPT clipboard Markdown before AOFM conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "aofm_chatgpt.hpp"

#include "convert.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct SourceLine {
  std::string text;
  std::string ending;
};

std::vector<SourceLine>
splitLines (const std::string& source) {
  std::vector<SourceLine> lines;
  size_t begin= 0;
  while (begin < source.size ()) {
    size_t end= source.find_first_of ("\r\n", begin);
    if (end == std::string::npos) {
      lines.push_back ({source.substr (begin), ""});
      break;
    }
    size_t endingLength= 1;
    if (source[end] == '\r' && end + 1 < source.size () &&
        source[end + 1] == '\n')
      endingLength= 2;
    lines.push_back ({source.substr (begin, end - begin),
                      source.substr (end, endingLength)});
    begin= end + endingLength;
  }
  return lines;
}

std::pair<size_t, size_t>
trimBounds (const std::string& text) {
  size_t first= 0;
  while (first < text.size () &&
         std::isspace (static_cast<unsigned char> (text[first])))
    ++first;
  size_t last= text.size ();
  while (last > first &&
         std::isspace (static_cast<unsigned char> (text[last - 1])))
    --last;
  return {first, last};
}

std::string
trimmed (const std::string& text) {
  auto bounds= trimBounds (text);
  return text.substr (bounds.first, bounds.second - bounds.first);
}

bool
trimmedEquals (const std::string& text, const char* expected) {
  return trimmed (text) == expected;
}

void
replaceTrimmed (std::string& text, const char* replacement) {
  auto bounds= trimBounds (text);
  text= text.substr (0, bounds.first) + replacement + text.substr (bounds.second);
}

bool
fenceRun (const std::string& text, char& marker, size_t& count,
          size_t& remainder) {
  auto bounds= trimBounds (text);
  if (bounds.first == bounds.second) return false;
  char candidate= text[bounds.first];
  if (candidate != '`' && candidate != '~') return false;
  size_t at= bounds.first;
  while (at < bounds.second && text[at] == candidate) ++at;
  if (at - bounds.first < 3) return false;
  marker= candidate;
  count= at - bounds.first;
  remainder= at;
  return true;
}

bool
fenceClosing (const std::string& text, char marker, size_t minimumCount) {
  char candidate= 0;
  size_t count= 0, remainder= 0;
  if (!fenceRun (text, candidate, count, remainder) || candidate != marker ||
      count < minimumCount)
    return false;
  auto bounds= trimBounds (text);
  return remainder == bounds.second;
}

bool
isRepeatedEqualsLine (const std::string& text) {
  std::string value= trimmed (text);
  return value.size () >= 3 &&
         std::all_of (value.begin (), value.end (),
                      [] (char c) { return c == '='; });
}

bool
isRowEnvironment (const std::string& name) {
  static const char* environments[]= {
    "array", "matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix",
    "Vmatrix", "smallmatrix", "cases", "aligned", "alignedat",
    "gathered", "split", "eqnarray", "eqnarray*"
  };
  for (const char* environment: environments)
    if (name == environment) return true;
  return false;
}

void
updateRowEnvironmentDepth (const std::string& text, int& depth) {
  size_t at= 0;
  while (at < text.size ()) {
    size_t begin= text.find ("\\begin{", at);
    size_t end= text.find ("\\end{", at);
    bool opening= begin != std::string::npos &&
                  (end == std::string::npos || begin < end);
    size_t command= opening ? begin : end;
    if (command == std::string::npos) break;
    size_t nameBegin= command + (opening ? 7 : 5);
    size_t nameEnd= text.find ('}', nameBegin);
    if (nameEnd == std::string::npos) break;
    std::string name= text.substr (nameBegin, nameEnd - nameBegin);
    if (isRowEnvironment (name)) {
      if (opening) ++depth;
      else depth= std::max (0, depth - 1);
    }
    at= nameEnd + 1;
  }
}

void
repairDisplayBody (std::vector<SourceLine>& lines, size_t begin, size_t end) {
  int rowEnvironmentDepth= 0;
  for (size_t i= begin; i < end; ++i) {
    if (isRepeatedEqualsLine (lines[i].text))
      replaceTrimmed (lines[i].text, "=");

    updateRowEnvironmentDepth (lines[i].text, rowEnvironmentDepth);
    if (rowEnvironmentDepth <= 0 ||
        lines[i].text.find ("\\end{") != std::string::npos)
      continue;

    auto bounds= trimBounds (lines[i].text);
    if (bounds.second == bounds.first ||
        lines[i].text[bounds.second - 1] != '\\')
      continue;
    size_t slashes= 0;
    for (size_t at= bounds.second;
         at > bounds.first && lines[i].text[at - 1] == '\\'; --at)
      ++slashes;
    if (slashes == 1) lines[i].text.insert (bounds.second, 1, '\\');
  }
}

void
restoreDisplayDelimiters (std::vector<SourceLine>& lines) {
  bool inFence= false;
  char fenceMarker= 0;
  size_t fenceLength= 0;

  for (size_t i= 0; i < lines.size (); ++i) {
    char marker= 0;
    size_t count= 0, remainder= 0;
    bool fence= fenceRun (lines[i].text, marker, count, remainder);
    if (inFence) {
      if (fenceClosing (lines[i].text, fenceMarker, fenceLength))
        inFence= false;
      continue;
    }
    if (fence) {
      inFence= true;
      fenceMarker= marker;
      fenceLength= count;
      continue;
    }
    if (!trimmedEquals (lines[i].text, "[")) continue;

    size_t close= std::string::npos;
    bool hasContent= false;
    size_t limit= std::min (lines.size (), i + 501);
    for (size_t j= i + 1; j < limit; ++j) {
      char nestedMarker= 0;
      size_t nestedCount= 0, nestedRemainder= 0;
      if (fenceRun (lines[j].text, nestedMarker, nestedCount,
                    nestedRemainder) || trimmedEquals (lines[j].text, "["))
        break;
      if (trimmedEquals (lines[j].text, "]")) {
        if (hasContent) close= j;
        break;
      }
      if (!trimmed (lines[j].text).empty ()) hasContent= true;
    }
    if (close == std::string::npos) continue;
    repairDisplayBody (lines, i + 1, close);
    replaceTrimmed (lines[i].text, "\\[");
    replaceTrimmed (lines[close].text, "\\]");
    i= close;
  }
}

bool
isWordByte (char value) {
  unsigned char byte= static_cast<unsigned char> (value);
  return std::isalnum (byte) || value == '_';
}

uint32_t
decodeUtf8 (const std::string& text, size_t& at) {
  unsigned char first= static_cast<unsigned char> (text[at]);
  if (first < 0x80) return text[at++];
  int extra= first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc0 ? 1 : 0;
  if (extra == 0 || at + static_cast<size_t> (extra) >= text.size ()) {
    ++at;
    return 0xfffd;
  }
  uint32_t result= first & ((1u << (6 - extra)) - 1u);
  for (int i= 1; i <= extra; ++i) {
    unsigned char next= static_cast<unsigned char> (text[at + i]);
    if ((next & 0xc0) != 0x80) {
      ++at;
      return 0xfffd;
    }
    result= (result << 6) | (next & 0x3f);
  }
  at += static_cast<size_t> (extra + 1);
  return result;
}

bool
isUnicodeMath (uint32_t codepoint) {
  return (codepoint >= 0x0370 && codepoint <= 0x03ff) ||
         (codepoint >= 0x2190 && codepoint <= 0x22ff) ||
         (codepoint >= 0x27c0 && codepoint <= 0x27ef) ||
         (codepoint >= 0x1d400 && codepoint <= 0x1d7ff) ||
         codepoint == 0x00b1 || codepoint == 0x00d7 || codepoint == 0x00f7;
}

bool
looksLikeInlineMath (const std::string& untrimmedContent) {
  std::string content= trimmed (untrimmedContent);
  if (content.empty () || content.size () > 4096 ||
      content.find ("\n") != std::string::npos ||
      content.find ("\r") != std::string::npos ||
      content.find ("://") != std::string::npos ||
      content.find ("www.") != std::string::npos ||
      content.find ('@') != std::string::npos)
    return false;

  int commands= 0;
  int words= 0;
  int longWords= 0;
  int digits= 0;
  bool allWordsShort= true;
  bool hasScript= false;
  bool hasRelation= false;
  bool hasBraces= false;
  bool hasNestedParentheses= false;
  bool hasComma= false;
  bool hasUnicodeMath= false;

  for (size_t i= 0; i < content.size ();) {
    unsigned char byte= static_cast<unsigned char> (content[i]);
    if (content[i] == '\\') {
      ++commands;
      ++i;
      while (i < content.size () &&
             std::isalpha (static_cast<unsigned char> (content[i])))
        ++i;
      continue;
    }
    if (std::isalpha (byte)) {
      size_t begin= i++;
      while (i < content.size () &&
             std::isalpha (static_cast<unsigned char> (content[i])))
        ++i;
      size_t length= i - begin;
      ++words;
      if (length > 2) {
        ++longWords;
        allWordsShort= false;
      }
      continue;
    }
    if (std::isdigit (byte)) {
      ++digits;
      ++i;
      continue;
    }
    if (byte >= 0x80) {
      uint32_t codepoint= decodeUtf8 (content, i);
      if (isUnicodeMath (codepoint)) hasUnicodeMath= true;
      continue;
    }
    if (content[i] == '^' || content[i] == '_') hasScript= true;
    else if (content[i] == '=' || content[i] == '+' || content[i] == '*' ||
             content[i] == '/' || content[i] == '<' || content[i] == '>' ||
             content[i] == '|')
      hasRelation= true;
    else if (content[i] == '{' || content[i] == '}' ||
             content[i] == '[' || content[i] == ']')
      hasBraces= true;
    else if (content[i] == '(' || content[i] == ')')
      hasNestedParentheses= true;
    else if (content[i] == ',') hasComma= true;
    ++i;
  }

  if (longWords >= 2 && commands == 0) return false;
  if (longWords >= 2 && commands < 2 && !hasScript && !hasRelation &&
      !hasBraces)
    return false;
  if (commands > 0 || hasScript || hasRelation || hasBraces || hasUnicodeMath)
    return true;
  if (hasNestedParentheses && longWords <= 1) return true;
  if (words == 0 && digits > 0) return true;
  if (words == 1 && longWords == 0) return true;
  if (hasComma && words > 0 && words <= 4 && allWordsShort) return true;
  return false;
}

size_t
findRun (const std::string& text, size_t begin, char marker, size_t count) {
  std::string delimiter (count, marker);
  size_t at= begin;
  while ((at= text.find (delimiter, at)) != std::string::npos) {
    size_t slashes= 0;
    for (size_t i= at; i > 0 && text[i - 1] == '\\'; --i) ++slashes;
    if ((slashes & 1u) == 0) return at;
    at += count;
  }
  return std::string::npos;
}

size_t
findBalancedClose (const std::string& text, size_t open) {
  int depth= 1;
  for (size_t i= open + 1; i < text.size (); ++i) {
    if (text[i] == '\\' && i + 1 < text.size () &&
        (text[i + 1] == '(' || text[i + 1] == ')')) {
      ++i;
      continue;
    }
    if (text[i] == '(') ++depth;
    else if (text[i] == ')' && --depth == 0) return i;
  }
  return std::string::npos;
}

std::string
restoreInlineDelimiters (const std::string& line) {
  std::string out;
  out.reserve (line.size () + 16);

  for (size_t i= 0; i < line.size ();) {
    if (line[i] == '`') {
      size_t count= 1;
      while (i + count < line.size () && line[i + count] == '`') ++count;
      size_t close= findRun (line, i + count, '`', count);
      if (close == std::string::npos) {
        out.append (line, i, std::string::npos);
        break;
      }
      out.append (line, i, close + count - i);
      i= close + count;
      continue;
    }
    if (line[i] == '$') {
      size_t count= i + 1 < line.size () && line[i + 1] == '$' ? 2 : 1;
      size_t close= findRun (line, i + count, '$', count);
      if (close == std::string::npos) {
        out.append (line, i, std::string::npos);
        break;
      }
      out.append (line, i, close + count - i);
      i= close + count;
      continue;
    }
    if (line.compare (i, 2, "\\(") == 0 ||
        line.compare (i, 2, "\\[") == 0) {
      const char* closeDelimiter= line[i + 1] == '(' ? "\\)" : "\\]";
      size_t close= line.find (closeDelimiter, i + 2);
      if (close == std::string::npos) {
        out.append (line, i, std::string::npos);
        break;
      }
      out.append (line, i, close + 2 - i);
      i= close + 2;
      continue;
    }
    if (line[i] != '(') {
      out += line[i++];
      continue;
    }

    size_t close= findBalancedClose (line, i);
    if (close == std::string::npos) {
      out += line[i++];
      continue;
    }
    bool attached= i > 0 &&
      (isWordByte (line[i - 1]) || line[i - 1] == ']' ||
       line[i - 1] == '\\');
    std::string content= line.substr (i + 1, close - i - 1);
    if (!attached && looksLikeInlineMath (content)) {
      out += "\\(";
      out += content;
      out += "\\)";
    }
    else out.append (line, i, close + 1 - i);
    i= close + 1;
  }
  return out;
}

std::string
restoreInlineDelimiters (const std::vector<SourceLine>& lines) {
  std::string result;
  bool inFence= false;
  char fenceMarker= 0;
  size_t fenceLength= 0;
  bool inDisplayMath= false;
  bool inDollarMath= false;

  for (const SourceLine& line: lines) {
    char marker= 0;
    size_t count= 0, remainder= 0;
    bool fence= fenceRun (line.text, marker, count, remainder);
    if (inFence) {
      result += line.text + line.ending;
      if (fenceClosing (line.text, fenceMarker, fenceLength)) inFence= false;
      continue;
    }
    if (fence) {
      inFence= true;
      fenceMarker= marker;
      fenceLength= count;
      result += line.text + line.ending;
      continue;
    }

    if (inDisplayMath) {
      result += line.text + line.ending;
      if (trimmedEquals (line.text, "\\]")) inDisplayMath= false;
      continue;
    }
    if (trimmedEquals (line.text, "\\[")) {
      inDisplayMath= true;
      result += line.text + line.ending;
      continue;
    }
    if (inDollarMath) {
      result += line.text + line.ending;
      if (trimmedEquals (line.text, "$$")) inDollarMath= false;
      continue;
    }
    if (trimmedEquals (line.text, "$$")) {
      inDollarMath= true;
      result += line.text + line.ending;
      continue;
    }
    if (line.text.rfind ("    ", 0) == 0 ||
        (!line.text.empty () && line.text[0] == '\t'))
      result += line.text + line.ending;
    else
      result += restoreInlineDelimiters (line.text) + line.ending;
  }
  return result;
}

} // namespace

std::string
aofm_normalize_chatgpt_markdown (const std::string& source) {
  std::vector<SourceLine> lines= splitLines (source);
  restoreDisplayDelimiters (lines);
  return restoreInlineDelimiters (lines);
}

tree
aofm_chatgpt_to_tree (string source) {
  std::string raw (as_charp (source), N(source));
  std::string normalized= aofm_normalize_chatgpt_markdown (raw);
  return aofm_markdown_to_tree (
    string (normalized.data (), static_cast<int> (normalized.size ())));
}
