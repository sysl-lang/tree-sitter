# tree-sitter

[tree-sitter](https://tree-sitter.github.io/) for [sysl](https://github.com/sysl-lang/sysl): the incremental parsing library behind most editors' syntax trees. You give it a grammar and some text, and it gives back a concrete syntax tree. After an edit it re-parses only what changed, and it can answer structural queries over the tree. Module `sh.sysl.treesitter`.

It is meant as the base for [sysl-lang/highlighter](https://github.com/sysl-lang/highlighter) and for a future `sysl lsp`.

```hocon
dependencies {
  tree-sitter { git = "github.com/sysl-lang/tree-sitter", version = "0.1.0" }
}
```

The library and its headers have to be installed (`brew install tree-sitter`, or Debian's `libtree-sitter-dev`; 0.25 or newer). `pkg-config tree-sitter` is how the build finds them. Requires sysl `0.1.0-alpha.12`.

## Example

This is the test `the_readme_example_runs` in `sh/sysl/treesitter/tests/tests.sysl`:

```sysl
import sh.sysl.treesitter.{parser, query, replacement, describe}
import sh.sysl.treesitter.json.json_language

example() -> Result[unit, string]
    val p = parser()

    p.set_language(json_language()).map_err(describe)?

    val text = "{\"name\": \"tree\", \"size\": 3}"
    val tree = p.parse(text).map_err(describe)?

    assert_eq(tree.root().sexp(), "(document (object (pair key: (string (string_content)) " +
              "value: (string (string_content))) (pair key: (string (string_content)) value: (number))))", "")

    // Every key, through a query.
    val q = query(json_language(), "(pair key: (string (string_content) @key))")
        .map_err((e) -> s"query error at byte ${e.offset}")?
    val keys = q.captures(tree.root())

    assert_eq(keys[0].node.text(), "name", "")
    assert_eq(keys[1].node.text(), "size", "")

    // Change `3` to `42` and re-parse, reusing the old tree.
    tree.edit(replacement(text, 25, 26, "42"))

    val again = p.parse("{\"name\": \"tree\", \"size\": 42}", Some(tree)).map_err(describe)?

    assert_eq(again.root().named_descendant_for_bytes(25, 27).expect("a node").text(), "42", "")
    Ok(())
```

## Bringing a grammar

A grammar is the `parser.c` that `tree-sitter generate` writes, plus `scanner.c` if the grammar has an external scanner. It goes with `src/tree_sitter/parser.h` and exposes `const TSLanguage *tree_sitter_<name>(void)`. To use one, make a package (or a module in your own project) whose directory holds those C files with `tree_sitter/parser.h` beside them. sysl compiles any `.c` file it finds in a module directory. Add one `extern` and one function:

```sysl
module my.grammars.toml

import sh.sysl.treesitter.{Language, language}
import sh.sysl.treesitter.c

extern "tree_sitter_toml" raw_toml() -> *c.Language

toml_language() -> Language = language(raw_toml())
```

`sh.sysl.treesitter.json` is a worked example of this. It vendors **tree-sitter-json v0.24.8** (MIT, `sh/sysl/treesitter/json/LICENSE`): `src/parser.c` and `src/tree_sitter/parser.h` unchanged, taken from `https://github.com/tree-sitter/tree-sitter-json/archive/refs/tags/v0.24.8.tar.gz`, SHA-256 `acf6e8362457e819ed8b613f2ad9a0e1b621a77556c296f3abea58f7880a9213`. That grammar has no scanner and is generated for ABI 14. Its C is linked into a program only if the program calls `json_language()`.

## What is bound

| tree-sitter | here |
|---|---|
| `TSParser` | `parser() -> &Parser`: `set_language` (fails with `VersionMismatch(found, oldest, newest)` if the grammar's ABI is not one this tree-sitter reads), `parse(text, old)`, `parse_with(read, old)`, `set_logger` / `clear_logger`, `set_included_ranges` / `included_ranges`, `reset` |
| `TSTree` | `&Tree`: `root`, `edit`, `changed_ranges`, `copy`, `language`, `text` |
| `TSNode` | `Node`: `kind`, `grammar_kind`, `symbol`; `is_named` / `is_missing` / `is_extra` / `is_error` / `has_error` / `has_changes`; bytes, points and `span`; `text` and `text_in`; `sexp` (`ts_node_string`); `parent`; children and named children by index or as a `Buf`; `child_by_field`; `field_name_for_child`; siblings; `descendant_count`; descendants for a byte range; `walk`; `==` |
| `TSTreeCursor` | `&Cursor`: `node`, `field_name`, `depth`, the six `goto_*` moves, `reset` |
| `TSQuery` | `query(lang, source) -> Result[&Query, QueryError]`: counts, capture names and indices, string values, quantifiers, `predicates` (as data), `text_predicates_hold` (`#eq?`, `#not-eq?`, `#any-of?`, `#not-any-of?`), `disable_capture`, `disable_pattern`, `matches`, `captures` |
| `TSQueryCursor` | `query_cursor() -> &QueryCursor`: `exec`, `next_match`, `next_capture`, `set_byte_range`, `set_match_limit`, `did_exceed_match_limit` |
| `TSLanguage` | `Language`: `abi_version`, `name`, symbols (count, name, kind, lookup), fields (count, name, lookup) |
| `TSInputEdit` | `Edit`, and `replacement(old, start, end, inserted)`, which works out the three points |

The raw `extern` layer is `sh.sysl.treesitter.c`. It has no C shim. Every record that crosses by value is laid out as in the header, and a test checks each one's `sizeof` against the header's.

**Ownership.** `Parser`, `Tree`, `Cursor`, `Query` and `QueryCursor` are `&T` boxes, and each one's destructor calls its `ts_*_delete` exactly once. A `Node` is tree-sitter's own four-word value plus the `&Tree` it points into, so a node keeps its tree alive. A query cursor holds the query and the tree it was last run on. Match captures are copied out of the cursor's buffer, which the next call overwrites.

**Callbacks.** The read callback of `parse_with` is a sysl closure `&Fn(u32, Point) -> string`. It goes through one `@export`ed trampoline, because `TSInput.read` takes its `TSPoint` by value, and C can only call a function that takes an aggregate argument if that function is exported. The binding keeps the chunk it returned alive until tree-sitter asks for the next one. The logger is a plain-address trampoline. It reaches the closure through the parser box's address, which it receives as its payload. Both callbacks run on the thread that called `parse`, during the call, so neither needs to be a `&sync Fn`.

**Text.** A tree from `parse` holds its text, so `node.text()` takes no argument. A tree from `parse_with` holds none, and `node.text_in(source)` takes it instead.

## The survey: what is deliberately absent

The C API (`api.h`, 0.27.1) exports 147 functions. They fall into three groups:

- **Expressible: all of them.** Records go by value (`TSNode`, `TSPoint`, `TSRange`, `TSTreeCursor`, `TSInput`, `TSLogger`), opaque handles are `opaque struct`, and the callback with an aggregate argument goes through `@export`.
- **Shim-able: none needed.**
- **Needing the language to move: none.**

What is not bound, and why:

- **Wasm grammars** (`ts_wasm_store_*`, `ts_parser_set_wasm_store`, `ts_language_is_wasm`). brew's library is built without wasmtime, so these would only ever fail.
- **`ts_set_allocator`.** It is process-global, and swapping the allocator under live trees is unsafe. This binding frees what tree-sitter returns with the system `free`.
- **Graphviz dumps** (`ts_parser_print_dot_graphs`, `ts_tree_print_dot_graph`). They write to a file descriptor and are debugging aids. The logger covers the same ground as data.
- **Progress callbacks** (`ts_parser_parse_with_options`, `ts_query_cursor_exec_with_options`). They are expressible in the same way as the read callback but are not needed by any consumer yet.
- **The lookahead iterator** (`ts_lookahead_iterator_*`).
- **Point-based variants of byte-based calls** (`*_for_point_range`, `set_point_range`, `goto_first_child_for_point`, `ts_node_edit`, `ts_point_edit`, `ts_range_edit`).
- **The remaining parse-state and descendant-index calls** (`next_parse_state`, `child_with_descendant`, `goto_descendant`, `ts_tree_root_node_with_offset`, `parse_string_encoding`, `remove_match`, `set_max_start_depth`, `is_pattern_rooted` / `non_local` / `guaranteed_at_step`, `ts_language_metadata`, `supertypes` / `subtypes`, `next_state`). Each is a one-line `extern` plus a method and would be added on request.

## Tests

`sysl test .` runs 22 tests. The oracle is the `tree-sitter` CLI 0.27.1, run with the same grammar in its own checkout. The CLI output is quoted in the tests next to the command that produced it. The tests check that:

- the S-expression matches `tree-sitter parse --no-ranges`, for a valid document, an `ERROR` case and an edited text;
- the `MISSING` node and the `ERROR` node sit where `tree-sitter parse --cst` and its summary line put them;
- the descendant count matches the `--cst` node count;
- the captures from the grammar's own `highlights.scm` match `tree-sitter query --captures` line for line (pattern, capture, name, both positions, text);
- an `#eq?` match is the one `tree-sitter query` keeps;
- query errors give the CLI's offset and kind (`Query error at 1:38. Invalid syntax`, `1:8. Invalid node type`);
- after `edit` and re-parse, the tree equals a fresh parse and the CLI's tree, and a changed range covers the edit.

They also cover the read callback (three-byte chunks, with each request's position checked), the logger (the first line is the CLI's first `--debug` line, `new_parse`), an ABI mismatch provoked with a copy of the grammar that claims ABI 99 or 12, parsing with no language, included ranges in and out of order, and that every record's layout agrees with the C header.

## License

ISC, as for the other sysl-lang packages. The vendored grammar is MIT.
