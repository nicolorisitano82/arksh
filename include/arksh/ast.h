#ifndef ARKSH_AST_H
#define ARKSH_AST_H

#include "arksh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARKSH_MAX_LINE 2048
#define ARKSH_MAX_PIPELINE_STAGES 8
#define ARKSH_MAX_REDIRECTIONS 8
#define ARKSH_MAX_LIST_ENTRIES 16
#define ARKSH_MAX_SWITCH_CASES 16
#define ARKSH_MAX_CASE_BRANCHES 16
#define ARKSH_MAX_FUNCTION_PARAMS 8
#define ARKSH_MAX_CLASS_BASES 8
#define ARKSH_MAX_OBJECT_MEMBERS 8

typedef enum {
  ARKSH_AST_EMPTY = 0,
  ARKSH_AST_SIMPLE_COMMAND,
  ARKSH_AST_VALUE_EXPRESSION,
  ARKSH_AST_OBJECT_EXPRESSION,
  ARKSH_AST_OBJECT_PIPELINE,
  ARKSH_AST_COMMAND_PIPELINE,
  ARKSH_AST_COMMAND_LIST,
  ARKSH_AST_GROUP_COMMAND,
  ARKSH_AST_SUBSHELL_COMMAND,
  ARKSH_AST_IF_COMMAND,
  ARKSH_AST_WHILE_COMMAND,
  ARKSH_AST_UNTIL_COMMAND,
  ARKSH_AST_FOR_COMMAND,
  ARKSH_AST_CASE_COMMAND,
  ARKSH_AST_SWITCH_COMMAND,
  ARKSH_AST_FUNCTION_COMMAND,
  ARKSH_AST_CLASS_COMMAND,
  ARKSH_AST_WITH_SUDO_COMMAND  /* E15-S3: with sudo do … endwith */
} ArkshAstKind;

typedef enum {
  ARKSH_MEMBER_PROPERTY = 0,
  ARKSH_MEMBER_METHOD
} ArkshMemberKind;

typedef enum {
  ARKSH_PARSED_ARG_RAW = 0,
  ARKSH_PARSED_ARG_STRING_LITERAL,
  ARKSH_PARSED_ARG_NUMBER_LITERAL,
  ARKSH_PARSED_ARG_BOOLEAN_LITERAL,
  ARKSH_PARSED_ARG_BLOCK_LITERAL
} ArkshParsedArgKind;

typedef struct {
  ArkshParsedArgKind kind;
  char text[ARKSH_MAX_LINE];
  char raw_text[ARKSH_MAX_LINE];
  ArkshBlock block;
} ArkshParsedArgNode;

typedef struct {
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int argc;
} ArkshSimpleCommandNode;

typedef enum {
  ARKSH_REDIRECT_INPUT = 0,
  ARKSH_REDIRECT_OUTPUT_TRUNCATE,
  ARKSH_REDIRECT_OUTPUT_APPEND,
  ARKSH_REDIRECT_ERROR_TRUNCATE,
  ARKSH_REDIRECT_ERROR_APPEND,
  ARKSH_REDIRECT_ERROR_TO_OUTPUT,
  ARKSH_REDIRECT_FD_INPUT,
  ARKSH_REDIRECT_FD_OUTPUT_TRUNCATE,
  ARKSH_REDIRECT_FD_OUTPUT_APPEND,
  ARKSH_REDIRECT_FD_DUP_INPUT,
  ARKSH_REDIRECT_FD_DUP_OUTPUT,
  ARKSH_REDIRECT_FD_CLOSE,
  ARKSH_REDIRECT_HERESTRING,
  ARKSH_REDIRECT_HEREDOC
} ArkshRedirectionKind;

typedef struct {
  ArkshRedirectionKind kind;
  int fd;
  int target_fd;
  int heredoc_strip_tabs;
  char target[ARKSH_MAX_PATH];
  char raw_target[ARKSH_MAX_TOKEN];
  char heredoc_delimiter[ARKSH_MAX_TOKEN];
  char heredoc_body[ARKSH_MAX_OUTPUT];
} ArkshRedirectionNode;

typedef struct {
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int argc;
  ArkshRedirectionNode redirections[ARKSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} ArkshCommandStageNode;

typedef struct {
  ArkshCommandStageNode stages[ARKSH_MAX_PIPELINE_STAGES];
  size_t stage_count;
} ArkshCommandPipelineNode;

typedef enum {
  ARKSH_LIST_CONDITION_ALWAYS = 0,
  ARKSH_LIST_CONDITION_ON_SUCCESS,
  ARKSH_LIST_CONDITION_ON_FAILURE
} ArkshListCondition;

typedef struct {
  char text[ARKSH_MAX_LINE];
  ArkshListCondition condition;
  int run_in_background;
} ArkshCommandListEntry;

typedef struct {
  ArkshCommandListEntry entries[ARKSH_MAX_LIST_ENTRIES];
  size_t count;
} ArkshCommandListNode;

typedef struct {
  char body[ARKSH_MAX_LINE];
  ArkshRedirectionNode redirections[ARKSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} ArkshCompoundCommandNode;

typedef struct {
  char condition[ARKSH_MAX_LINE];
  char then_branch[ARKSH_MAX_LINE];
  char else_branch[ARKSH_MAX_LINE];
  int has_else_branch;
} ArkshIfCommandNode;

typedef struct {
  char condition[ARKSH_MAX_LINE];
  char body[ARKSH_MAX_LINE];
} ArkshWhileCommandNode;

typedef struct {
  char condition[ARKSH_MAX_LINE];
  char body[ARKSH_MAX_LINE];
} ArkshUntilCommandNode;

typedef struct {
  char variable[ARKSH_MAX_NAME];
  char source[ARKSH_MAX_LINE];
  char body[ARKSH_MAX_LINE];
} ArkshForCommandNode;

typedef struct {
  char patterns[ARKSH_MAX_LINE];
  char body[ARKSH_MAX_LINE];
} ArkshCaseBranchNode;

typedef struct {
  char expression[ARKSH_MAX_LINE];
  ArkshCaseBranchNode branches[ARKSH_MAX_CASE_BRANCHES];
  size_t branch_count;
} ArkshCaseCommandNode;

typedef struct {
  char match[ARKSH_MAX_LINE];
  char body[ARKSH_MAX_LINE];
} ArkshSwitchCaseNode;

typedef struct {
  char expression[ARKSH_MAX_LINE];
  ArkshSwitchCaseNode cases[ARKSH_MAX_SWITCH_CASES];
  size_t case_count;
  char default_branch[ARKSH_MAX_LINE];
  int has_default_branch;
} ArkshSwitchCommandNode;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char params[ARKSH_MAX_FUNCTION_PARAMS][ARKSH_MAX_NAME];
  int param_count;
  char body[ARKSH_MAX_LINE];
  char source[ARKSH_MAX_LINE];
} ArkshFunctionCommandNode;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char bases[ARKSH_MAX_CLASS_BASES][ARKSH_MAX_NAME];
  int base_count;
  char body[ARKSH_MAX_LINE];
  char source[ARKSH_MAX_LINE];
} ArkshClassCommandNode;

/* E15-S3: with sudo do … endwith */
typedef struct {
  char body[ARKSH_MAX_LINE];
} ArkshWithSudoCommandNode;

typedef struct {
  char member[ARKSH_MAX_NAME];
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  ArkshParsedArgNode parsed_args[ARKSH_MAX_ARGS];
  int argc;
  ArkshMemberKind member_kind;
} ArkshObjectMemberNode;

typedef struct {
  char selector[ARKSH_MAX_LINE];
  char raw_selector[ARKSH_MAX_LINE];
  ArkshObjectMemberNode members[ARKSH_MAX_OBJECT_MEMBERS];
  size_t member_count;
  int legacy_syntax;
} ArkshObjectExpressionNode;

typedef enum {
  ARKSH_VALUE_SOURCE_OBJECT_EXPRESSION = 0,
  ARKSH_VALUE_SOURCE_BINDING,
  ARKSH_VALUE_SOURCE_STRING_LITERAL,
  ARKSH_VALUE_SOURCE_NUMBER_LITERAL,
  ARKSH_VALUE_SOURCE_BOOLEAN_LITERAL,
  ARKSH_VALUE_SOURCE_LIST_LITERAL,
  ARKSH_VALUE_SOURCE_BLOCK_LITERAL,
  ARKSH_VALUE_SOURCE_CAPTURE_TEXT,
  ARKSH_VALUE_SOURCE_CAPTURE_LINES,
  ARKSH_VALUE_SOURCE_TERNARY,
  ARKSH_VALUE_SOURCE_RESOLVER_CALL,
  /* Binary operator in value context: left OP right.
     OP is one of '+', '-', '*', '/'.
     Dispatch: numeric operands → native arithmetic;
     other types → extension method __add__ / __sub__ / __mul__ / __div__. */
  ARKSH_VALUE_SOURCE_BINARY_OP,
  /* Shell command line captured as text (E3-S3 bridge): raw_text is passed
     directly to arksh_shell_execute_line(); stdout becomes a text value.
     Unlike CAPTURE_TEXT, no quote-stripping is applied — the source text is
     used verbatim so that `ls -la |> lines` works without extra quoting. */
  ARKSH_VALUE_SOURCE_CAPTURE_SHELL
} ArkshValueSourceKind;

typedef struct {
  ArkshValueSourceKind kind;
  ArkshObjectExpressionNode object_expression;
  char binding[ARKSH_MAX_NAME];
  char text[ARKSH_MAX_LINE];
  char raw_text[ARKSH_MAX_LINE];
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int argc;
  ArkshBlock block;
  /* Binary operator fields (used when kind == ARKSH_VALUE_SOURCE_BINARY_OP). */
  char binary_left[ARKSH_MAX_LINE];
  char binary_right[ARKSH_MAX_LINE];
  char binary_op; /* '+', '-', '*', '/' */
} ArkshValueSourceNode;

typedef struct {
  char name[ARKSH_MAX_NAME];
  char raw_args[ARKSH_MAX_LINE];
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  ArkshParsedArgNode parsed_args[ARKSH_MAX_ARGS];
  int argc;
} ArkshPipelineStageNode;

typedef struct {
  ArkshValueSourceNode source;
  ArkshPipelineStageNode stages[ARKSH_MAX_PIPELINE_STAGES];
  size_t stage_count;
} ArkshObjectPipelineNode;

typedef struct {
  ArkshAstKind kind;
  char line[ARKSH_MAX_LINE];
  union {
    ArkshSimpleCommandNode command;
    ArkshValueSourceNode value_expression;
    ArkshObjectExpressionNode object_expression;
    ArkshObjectPipelineNode pipeline;
    ArkshCommandPipelineNode command_pipeline;
    ArkshCommandListNode command_list;
    ArkshCompoundCommandNode group_command;
    ArkshCompoundCommandNode subshell_command;
    ArkshIfCommandNode if_command;
    ArkshWhileCommandNode while_command;
    ArkshUntilCommandNode until_command;
    ArkshForCommandNode for_command;
    ArkshCaseCommandNode case_command;
    ArkshSwitchCommandNode switch_command;
    ArkshFunctionCommandNode function_command;
    ArkshClassCommandNode class_command;
    ArkshWithSudoCommandNode with_sudo_command; /* E15-S3 */
  } as;
} ArkshAst;

void arksh_ast_init(ArkshAst *ast);

#ifdef __cplusplus
}
#endif

#endif
