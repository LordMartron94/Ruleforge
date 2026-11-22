#pragma once

#include <pharos.h>

#include "tool.h"

/*
The language server of the genesis tool: the Genesis side of a Pharos server. t_lsp.c turns a Genesis source, a mapping file or
a document of any language with a grammar into names and syntax problems, and runs the server. Only syntax is judged here;
what a name means is for the semantic analyzer.
*/

/* The kind of a name that belongs to a language described by a grammar and a mapping (the Genesis kinds are LGenesisSymbolKind). */
#define LTOOL_LS_KIND_GENERIC 100

typedef struct LToolLsLanguage {
  char             *name;
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  NexusDataList    *extensions;   /* char * */
  NexusDataList    *declarations; /* LToolLsDeclaration * */
  NexusDataList    *references;   /* uint32 token kinds, besides the names of declarations */
  uint64            source_hash;  /* Of the grammar (as exported) and its mapping file: what cached analyses of its documents depend on. */
} LToolLsLanguage;

typedef enum LToolLsDocumentKind {
  LTLD_GENESIS = 1,
  LTLD_MAPPING,
  LTLD_GENERIC
} LToolLsDocumentKind;

typedef struct LToolLs {
  const LToolOptions *options;
  NexusDataList      *languages; /* LToolLsLanguage * */
  PharosServer       *pharos;
  char               *cache_directory; /* The strings the settings of the server point to, from genesis.toml. */
  char              **ignore;
  uint32              ignore_count;
  char               *fingerprint; /* What cached analyses depend on: this tool's analysis version and the grammars it serves. */
} LToolLs;

/* The version of what the analyses produce; a change in how names or problems are made is a new number, which drops every cache. */
#define LTOOL_LS_ANALYSIS_VERSION 1

/* The state a generic document keeps for hovers: the language and the tree its text parsed to. */
typedef struct LToolLsGeneric {
  LToolLsLanguage *language;
  LParserTree     *tree;
} LToolLsGeneric;
