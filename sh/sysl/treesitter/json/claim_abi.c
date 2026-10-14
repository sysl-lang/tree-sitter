// A copy of the JSON grammar with its ABI version overwritten, so that a test can show
// `ts_parser_set_language` refusing a grammar from an ABI this tree-sitter does not read. One copy
// per version asked for would be the general answer; the tests ask for one at a time, and the
// storage is static so that the language outlives every parser given it.

#include <string.h>

#include "tree_sitter/parser.h"

const TSLanguage *tree_sitter_json(void);

const TSLanguage *sysl_treesitter_json_with_abi(uint32_t version) {
  static TSLanguage copy;

  memcpy(&copy, tree_sitter_json(), sizeof copy);
  copy.version = version;
  return &copy;
}
