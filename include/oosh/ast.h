#ifndef OOSH_AST_H
#define OOSH_AST_H

#include "oosh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OOSH_MAX_LINE 2048
#define OOSH_MAX_PIPELINE_STAGES 8
#define OOSH_MAX_REDIRECTIONS 8
#define OOSH_MAX_LIST_ENTRIES 16
#define OOSH_MAX_SWITCH_CASES 16
#define OOSH_MAX_CASE_BRANCHES 16
#define OOSH_MAX_FUNCTION_PARAMS 8
#define OOSH_MAX_CLASS_BASES 8

typedef enum {
  OOSH_AST_EMPTY = 0,
  OOSH_AST_SIMPLE_COMMAND,
  OOSH_AST_VALUE_EXPRESSION,
  OOSH_AST_OBJECT_EXPRESSION,
  OOSH_AST_OBJECT_PIPELINE,
  OOSH_AST_COMMAND_PIPELINE,
  OOSH_AST_COMMAND_LIST,
  OOSH_AST_GROUP_COMMAND,
  OOSH_AST_SUBSHELL_COMMAND,
  OOSH_AST_IF_COMMAND,
  OOSH_AST_WHILE_COMMAND,
  OOSH_AST_UNTIL_COMMAND,
  OOSH_AST_FOR_COMMAND,
  OOSH_AST_CASE_COMMAND,
  OOSH_AST_SWITCH_COMMAND,
  OOSH_AST_FUNCTION_COMMAND,
  OOSH_AST_CLASS_COMMAND
} OoshAstKind;

typedef enum {
  OOSH_MEMBER_PROPERTY = 0,
  OOSH_MEMBER_METHOD
} OoshMemberKind;

typedef struct {
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
} OoshSimpleCommandNode;

typedef enum {
  OOSH_REDIRECT_INPUT = 0,
  OOSH_REDIRECT_OUTPUT_TRUNCATE,
  OOSH_REDIRECT_OUTPUT_APPEND,
  OOSH_REDIRECT_ERROR_TRUNCATE,
  OOSH_REDIRECT_ERROR_APPEND,
  OOSH_REDIRECT_ERROR_TO_OUTPUT,
  OOSH_REDIRECT_FD_INPUT,
  OOSH_REDIRECT_FD_OUTPUT_TRUNCATE,
  OOSH_REDIRECT_FD_OUTPUT_APPEND,
  OOSH_REDIRECT_FD_DUP_INPUT,
  OOSH_REDIRECT_FD_DUP_OUTPUT,
  OOSH_REDIRECT_FD_CLOSE,
  OOSH_REDIRECT_HEREDOC
} OoshRedirectionKind;

typedef struct {
  OoshRedirectionKind kind;
  int fd;
  int target_fd;
  int heredoc_strip_tabs;
  char target[OOSH_MAX_PATH];
  char raw_target[OOSH_MAX_TOKEN];
  char heredoc_delimiter[OOSH_MAX_TOKEN];
  char heredoc_body[OOSH_MAX_OUTPUT];
} OoshRedirectionNode;

typedef struct {
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
  OoshRedirectionNode redirections[OOSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} OoshCommandStageNode;

typedef struct {
  OoshCommandStageNode stages[OOSH_MAX_PIPELINE_STAGES];
  size_t stage_count;
} OoshCommandPipelineNode;

typedef enum {
  OOSH_LIST_CONDITION_ALWAYS = 0,
  OOSH_LIST_CONDITION_ON_SUCCESS,
  OOSH_LIST_CONDITION_ON_FAILURE
} OoshListCondition;

typedef struct {
  char text[OOSH_MAX_LINE];
  OoshListCondition condition;
  int run_in_background;
} OoshCommandListEntry;

typedef struct {
  OoshCommandListEntry entries[OOSH_MAX_LIST_ENTRIES];
  size_t count;
} OoshCommandListNode;

typedef struct {
  char body[OOSH_MAX_LINE];
  OoshRedirectionNode redirections[OOSH_MAX_REDIRECTIONS];
  size_t redirection_count;
} OoshCompoundCommandNode;

typedef struct {
  char condition[OOSH_MAX_LINE];
  char then_branch[OOSH_MAX_LINE];
  char else_branch[OOSH_MAX_LINE];
  int has_else_branch;
} OoshIfCommandNode;

typedef struct {
  char condition[OOSH_MAX_LINE];
  char body[OOSH_MAX_LINE];
} OoshWhileCommandNode;

typedef struct {
  char condition[OOSH_MAX_LINE];
  char body[OOSH_MAX_LINE];
} OoshUntilCommandNode;

typedef struct {
  char variable[OOSH_MAX_NAME];
  char source[OOSH_MAX_LINE];
  char body[OOSH_MAX_LINE];
} OoshForCommandNode;

typedef struct {
  char patterns[OOSH_MAX_LINE];
  char body[OOSH_MAX_LINE];
} OoshCaseBranchNode;

typedef struct {
  char expression[OOSH_MAX_LINE];
  OoshCaseBranchNode branches[OOSH_MAX_CASE_BRANCHES];
  size_t branch_count;
} OoshCaseCommandNode;

typedef struct {
  char match[OOSH_MAX_LINE];
  char body[OOSH_MAX_LINE];
} OoshSwitchCaseNode;

typedef struct {
  char expression[OOSH_MAX_LINE];
  OoshSwitchCaseNode cases[OOSH_MAX_SWITCH_CASES];
  size_t case_count;
  char default_branch[OOSH_MAX_LINE];
  int has_default_branch;
} OoshSwitchCommandNode;

typedef struct {
  char name[OOSH_MAX_NAME];
  char params[OOSH_MAX_FUNCTION_PARAMS][OOSH_MAX_NAME];
  int param_count;
  char body[OOSH_MAX_LINE];
  char source[OOSH_MAX_LINE];
} OoshFunctionCommandNode;

typedef struct {
  char name[OOSH_MAX_NAME];
  char bases[OOSH_MAX_CLASS_BASES][OOSH_MAX_NAME];
  int base_count;
  char body[OOSH_MAX_LINE];
  char source[OOSH_MAX_LINE];
} OoshClassCommandNode;

typedef struct {
  char selector[OOSH_MAX_PATH];
  char raw_selector[OOSH_MAX_TOKEN];
  char member[OOSH_MAX_NAME];
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
  OoshMemberKind member_kind;
  int legacy_syntax;
} OoshObjectExpressionNode;

typedef enum {
  OOSH_VALUE_SOURCE_OBJECT_EXPRESSION = 0,
  OOSH_VALUE_SOURCE_BINDING,
  OOSH_VALUE_SOURCE_STRING_LITERAL,
  OOSH_VALUE_SOURCE_NUMBER_LITERAL,
  OOSH_VALUE_SOURCE_BOOLEAN_LITERAL,
  OOSH_VALUE_SOURCE_LIST_LITERAL,
  OOSH_VALUE_SOURCE_BLOCK_LITERAL,
  OOSH_VALUE_SOURCE_CAPTURE_TEXT,
  OOSH_VALUE_SOURCE_CAPTURE_LINES,
  OOSH_VALUE_SOURCE_TERNARY,
  OOSH_VALUE_SOURCE_RESOLVER_CALL,
  /* Binary operator in value context: left OP right.
     OP is one of '+', '-', '*', '/'.
     Dispatch: numeric operands → native arithmetic;
     other types → extension method __add__ / __sub__ / __mul__ / __div__. */
  OOSH_VALUE_SOURCE_BINARY_OP
} OoshValueSourceKind;

typedef struct {
  OoshValueSourceKind kind;
  OoshObjectExpressionNode object_expression;
  char binding[OOSH_MAX_NAME];
  char text[OOSH_MAX_LINE];
  char raw_text[OOSH_MAX_LINE];
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
  OoshBlock block;
  /* Binary operator fields (used when kind == OOSH_VALUE_SOURCE_BINARY_OP). */
  char binary_left[OOSH_MAX_LINE];
  char binary_right[OOSH_MAX_LINE];
  char binary_op; /* '+', '-', '*', '/' */
} OoshValueSourceNode;

typedef struct {
  char name[OOSH_MAX_NAME];
  char raw_args[OOSH_MAX_LINE];
} OoshPipelineStageNode;

typedef struct {
  OoshValueSourceNode source;
  OoshPipelineStageNode stages[OOSH_MAX_PIPELINE_STAGES];
  size_t stage_count;
} OoshObjectPipelineNode;

typedef struct {
  OoshAstKind kind;
  char line[OOSH_MAX_LINE];
  union {
    OoshSimpleCommandNode command;
    OoshValueSourceNode value_expression;
    OoshObjectExpressionNode object_expression;
    OoshObjectPipelineNode pipeline;
    OoshCommandPipelineNode command_pipeline;
    OoshCommandListNode command_list;
    OoshCompoundCommandNode group_command;
    OoshCompoundCommandNode subshell_command;
    OoshIfCommandNode if_command;
    OoshWhileCommandNode while_command;
    OoshUntilCommandNode until_command;
    OoshForCommandNode for_command;
    OoshCaseCommandNode case_command;
    OoshSwitchCommandNode switch_command;
    OoshFunctionCommandNode function_command;
    OoshClassCommandNode class_command;
  } as;
} OoshAst;

void oosh_ast_init(OoshAst *ast);

#ifdef __cplusplus
}
#endif

#endif
