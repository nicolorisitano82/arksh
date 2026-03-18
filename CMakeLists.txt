cmake_minimum_required(VERSION 3.20)

project(oosh VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_library(oosh_core STATIC
  src/expand.c
  src/executor.c
  src/line_editor.c
  src/lexer.c
  src/object.c
  src/parser.c
  src/platform.c
  src/plugin.c
  src/prompt.c
  src/shell.c
)

target_include_directories(oosh_core PUBLIC include)

if(NOT WIN32)
  target_link_libraries(oosh_core PUBLIC dl)
endif()

add_executable(oosh src/main.c)
target_link_libraries(oosh PRIVATE oosh_core)

function(oosh_add_plugin target_name source_file)
  add_library(${target_name} MODULE ${source_file})
  target_include_directories(${target_name} PRIVATE include)
  set_target_properties(${target_name} PROPERTIES PREFIX "")

  if(APPLE)
    set_target_properties(${target_name} PROPERTIES SUFFIX ".dylib")
    target_link_options(${target_name} PRIVATE "-undefined" "dynamic_lookup")
  elseif(WIN32)
    set_target_properties(${target_name} PROPERTIES SUFFIX ".dll")
  else()
    set_target_properties(${target_name} PROPERTIES SUFFIX ".so")
  endif()
endfunction()

oosh_add_plugin(oosh_sample_plugin plugins/sample/sample_plugin.c)
oosh_add_plugin(oosh_skeleton_plugin plugins/skeleton/skeleton_plugin.c)

include(CTest)

if(BUILD_TESTING)
  add_executable(oosh_test_emit_args tests/emit_args.c)
  add_executable(oosh_test_count_lines tests/count_lines.c)
  add_executable(oosh_test_echo_stdin tests/echo_stdin.c)
  add_executable(oosh_test_pause tests/pause_seconds.c)

  file(COPY tests/fixtures DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/tests")

  if(WIN32)
    set(OOSH_TEST_EMIT "oosh_test_emit_args.exe")
    set(OOSH_TEST_COUNT "oosh_test_count_lines.exe")
    set(OOSH_TEST_ECHO "oosh_test_echo_stdin.exe")
    set(OOSH_TEST_PAUSE "oosh_test_pause.exe")
  else()
    set(OOSH_TEST_EMIT "./oosh_test_emit_args")
    set(OOSH_TEST_COUNT "./oosh_test_count_lines")
    set(OOSH_TEST_ECHO "./oosh_test_echo_stdin")
    set(OOSH_TEST_PAUSE "./oosh_test_pause")
  endif()

  add_test(NAME oosh_help COMMAND oosh -c help)
  add_test(NAME oosh_object_type COMMAND oosh -c "obj(\".\").type")
  add_test(NAME oosh_human_object_type COMMAND oosh -c ". -> type")
  add_test(NAME oosh_pipeline_count COMMAND oosh -c ". -> children() |> count()")
  add_test(NAME oosh_external_command COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" hello")
  add_test(NAME oosh_shell_pipeline COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" alpha beta | \"${OOSH_TEST_COUNT}\"")
  add_test(NAME oosh_glob_command_args COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" tests/fixtures/glob/*.fixture | \"${OOSH_TEST_COUNT}\"")
  add_test(NAME oosh_env_expansion COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" \"\$PWD\"")
  add_test(NAME oosh_command_substitution COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" \$(pwd)")
  add_test(NAME oosh_history_builtin COMMAND oosh -c "history")
  add_test(NAME oosh_sequence_operator COMMAND oosh -c "true ; true")
  add_test(NAME oosh_and_operator COMMAND oosh -c "true && true")
  add_test(NAME oosh_or_operator COMMAND oosh -c "false || true")
  add_test(NAME oosh_if_command COMMAND oosh -c "if true ; then \"${OOSH_TEST_EMIT}\" ok ; fi")
  add_test(NAME oosh_if_value_condition COMMAND oosh -c "if bool(false) ; then false ; else true ; fi")
  add_test(NAME oosh_ternary_true COMMAND oosh -c "bool(true) ? \"yes\" : \"no\"")
  add_test(NAME oosh_ternary_false COMMAND oosh -c "1 == 2 ? \"yes\" : \"no\"")
  add_test(NAME oosh_while_command COMMAND oosh -c "while false ; do true ; done")
  add_test(NAME oosh_for_command COMMAND oosh -c "for n in list(1, 2, 3) ; do n -> value ; done")
  add_test(NAME oosh_switch_match COMMAND oosh -c "switch 2 ; case 1 ; then text(\"one\") -> print() ; case 2 ; then text(\"two\") -> print() ; default ; then text(\"other\") -> print() ; endswitch")
  add_test(NAME oosh_switch_default COMMAND oosh -c "switch \"missing\" ; case \"one\" ; then text(\"one\") -> print() ; default ; then text(\"fallback\") -> print() ; endswitch")
  add_test(NAME oosh_block_local_reduce COMMAND oosh -c "list(1, 2, 3) |> reduce(number(0), [:acc :n | local next = acc + n ; next])")
  add_test(NAME oosh_block_local_scope_restore COMMAND oosh -c "let next = 99 ; let sum = list(1, 2) |> reduce(number(0), [:acc :n | local next = acc + n ; next]) ; text(\"%v\") -> print(next)")
  add_test(NAME oosh_background_job COMMAND oosh -c "\"${OOSH_TEST_PAUSE}\" 1 & jobs")
  add_test(NAME oosh_source_file COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/ooshrc")
  add_test(NAME oosh_control_flow_script COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/04-control-flow.oosh")
  add_test(NAME oosh_function_script COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/05-shell-functions.oosh")
  add_test(NAME oosh_case_builtins_script COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/07-case-and-builtins.oosh")
  add_test(NAME oosh_alias_rc COMMAND oosh -c "type ll")
  add_test(NAME oosh_shell_var_precedence COMMAND oosh -c "README.md -> read_text(\$LIMIT)")
  add_test(NAME oosh_function_basic COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/basic.oosh")
  add_test(NAME oosh_function_redefine COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/redefine.oosh")
  add_test(NAME oosh_function_scope COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/scope.oosh")
  add_test(NAME oosh_break_nested COMMAND oosh -c "while true ; do while true ; do text(\"once\") -> print() ; break 2 ; done ; done")
  add_test(NAME oosh_continue_nested COMMAND oosh -c "for outer in list(1, 2) ; do while true ; do outer -> print() ; continue 2 ; done ; done")
  add_test(NAME oosh_function_return COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/return.oosh")
  add_test(NAME oosh_function_return_from_loop COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/return-from-loop.oosh")
  add_test(NAME oosh_break_invalid COMMAND oosh -c "break || true")
  add_test(NAME oosh_continue_invalid COMMAND oosh -c "continue || true")
  add_test(NAME oosh_return_invalid COMMAND oosh -c "return text(\"x\") || true")
  add_test(NAME oosh_until_command COMMAND oosh -c "until true ; do text(\"retry\") -> print() ; done")
  add_test(NAME oosh_case_exact COMMAND oosh -c "case text(\"directory\") in directory) text(\"dir\") -> print() ;; *) text(\"other\") -> print() ;; esac")
  add_test(NAME oosh_case_wildcard COMMAND oosh -c "case text(\"demo.txt\") in *.md) text(\"md\") -> print() ;; *.txt) text(\"txt\") -> print() ;; esac")
  add_test(NAME oosh_case_alternatives COMMAND oosh -c "case text(\"blue\") in red|green) text(\"warm\") -> print() ;; blue|cyan) text(\"cool\") -> print() ;; esac")
  add_test(NAME oosh_eval_builtin COMMAND oosh -c "eval \"text(\\\"hi\\\") -> print()\"")
  add_test(NAME oosh_exec_builtin COMMAND oosh -c "exec \"${OOSH_TEST_EMIT}\" hello world")
  add_test(NAME oosh_wait_builtin COMMAND oosh -c "\"${OOSH_TEST_PAUSE}\" 0 & wait %1")
  add_test(NAME oosh_trap_exit COMMAND oosh -c "trap \"text(\\\"bye\\\") -> print()\" EXIT ; text(\"run\") -> print()")
  add_test(NAME oosh_heredoc_basic COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.oosh")
  add_test(NAME oosh_heredoc_strip_tabs COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/strip-tabs.oosh")
  add_test(NAME oosh_heredoc_pipeline COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/pipeline.oosh")
  add_test(NAME oosh_heredoc_preserve_blank_and_comment COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/preserve-blank-and-comment.oosh")
  add_test(NAME oosh_inline_if_heredoc COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/inline-if.oosh")
  add_test(NAME oosh_group_heredoc_tail COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/group-tail.oosh")
  add_test(NAME oosh_subshell_heredoc_tail COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/subshell-tail.oosh")
  add_test(NAME oosh_fd_redirect_output_dup COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" hello 3> fd3.out 1>&3 ; fd3.out -> read_text(64)")
  add_test(NAME oosh_fd_redirect_input_dup COMMAND oosh -c "\"${OOSH_TEST_EMIT}\" left right > fdin.txt ; \"${OOSH_TEST_COUNT}\" 3< fdin.txt 0<&3")
  add_test(NAME oosh_group_binding_scope COMMAND oosh -c "{ let flavor = text(\"mint\") ; } ; flavor -> value")
  add_test(NAME oosh_subshell_binding_scope COMMAND oosh -c "let flavor = text(\"vanilla\") ; ( let flavor = text(\"mint\") ) ; flavor -> value")
  add_test(NAME oosh_group_env_scope COMMAND oosh -c "{ set MODE grouped ; } ; text(\"%s\") -> print(\"\$MODE\")")
  add_test(NAME oosh_subshell_env_scope COMMAND oosh -c "set MODE outer ; ( set MODE inner ) ; text(\"%s\") -> print(\"\$MODE\")")
  add_test(NAME oosh_group_redirection COMMAND oosh -c "{ text(\"grouped\") -> print() ; } > group_scope.out ; group_scope.out -> read_text(32)")
  add_test(NAME oosh_subshell_redirection COMMAND oosh -c "( text(\"isolated\") -> print() ) > subshell_scope.out ; subshell_scope.out -> read_text(32)")
  add_test(NAME oosh_group_input_redirection COMMAND oosh -c "{ ${OOSH_TEST_COUNT} ; } < ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.oosh")
  add_test(NAME oosh_subshell_input_redirection COMMAND oosh -c "( ${OOSH_TEST_COUNT} ) < ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.oosh")
  add_test(NAME oosh_class_basic COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/basic.oosh")
  add_test(NAME oosh_class_init_inheritance COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/init-and-inheritance.oosh")
  add_test(NAME oosh_class_multiple_inheritance COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/multiple-inheritance.oosh")
  add_test(NAME oosh_class_introspection COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/introspection.oosh")
  add_test(NAME oosh_print_method COMMAND oosh -c "text(\"hello\") -> print()")
  add_test(NAME oosh_print_format_method COMMAND oosh -c "text(\"%s:%d\") -> print(\"file\", 3)")
  add_test(NAME oosh_proc_namespace COMMAND oosh -c "proc() -> pid")
  add_test(NAME oosh_shell_namespace COMMAND oosh -c "shell() -> plugins |> count()")
  add_test(NAME oosh_nested_json_roundtrip COMMAND oosh -c "tests/fixtures/json/nested.json -> read_json() |> to_json()")
  add_test(NAME oosh_plugin_value_resolver COMMAND oosh -c "plugin load $<TARGET_FILE_NAME:oosh_sample_plugin> ; sample() -> name")
  add_test(NAME oosh_plugin_pipeline_stage COMMAND oosh -c "plugin load $<TARGET_FILE_NAME:oosh_sample_plugin> ; text(\"ciao\") |> sample_wrap()")
  add_test(NAME oosh_positional_in_function COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/positional-in-function.oosh")
  add_test(NAME oosh_positional_count COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-count.oosh")
  add_test(NAME oosh_positional_all COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-all.oosh")
  add_test(NAME oosh_positional_shell_pid COMMAND oosh -c "text(\"%s\") -> print(\"$$\")")
  add_test(NAME oosh_positional_source_args COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/source-params.oosh hello")
  add_test(NAME oosh_positional_scope_restore COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/scope-restore.oosh")
  add_test(NAME oosh_param_expand_default COMMAND oosh -c "text(\"%s\") -> print(\"\${UNDEF:-fallback}\")")
  add_test(NAME oosh_param_expand_set_nodefault COMMAND oosh -c "set X hello ; text(\"%s\") -> print(\"\${X:-fallback}\")")
  add_test(NAME oosh_param_expand_assign COMMAND oosh -c "text(\"%s\") -> print(\"\${NEW:=assigned}\") ; text(\"%s\") -> print(\"\$NEW\")")
  add_test(NAME oosh_param_expand_alt COMMAND oosh -c "set Y yes ; text(\"%s\") -> print(\"\${Y:+alt}\")")
  add_test(NAME oosh_param_expand_alt_unset COMMAND oosh -c "text(\"%s\") -> print(\"\${NOPE:+alt}\")")
  add_test(NAME oosh_param_expand_length COMMAND oosh -c "set WORD hello ; text(\"%s\") -> print(\"\${#WORD}\")")
  add_test(NAME oosh_param_expand_suffix_shortest COMMAND oosh -c "set F file.tar.gz ; text(\"%s\") -> print(\"\${F%.*}\")")
  add_test(NAME oosh_param_expand_suffix_longest COMMAND oosh -c "set F file.tar.gz ; text(\"%s\") -> print(\"\${F%%.*}\")")
  add_test(NAME oosh_param_expand_prefix_shortest COMMAND oosh -c "set P /usr/local/bin ; text(\"%s\") -> print(\"\${P#/usr}\")")
  add_test(NAME oosh_param_expand_prefix_longest COMMAND oosh -c "set P /usr/local/bin ; text(\"%s\") -> print(\"\${P##*/}\")")
  add_test(NAME oosh_param_expand_script COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-expansion.oosh")
  add_test(NAME oosh_ifs_split_count COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/split-count.oosh")
  add_test(NAME oosh_ifs_split_first COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/split-first.oosh")
  add_test(NAME oosh_ifs_quoted_no_split COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/quoted-no-split.oosh")
  add_test(NAME oosh_ifs_custom COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/custom-ifs.oosh")
  add_test(NAME oosh_ifs_quote_removal COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/quote-removal.oosh")
  add_test(NAME oosh_ifs_empty_var COMMAND oosh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/empty-var.oosh")

  # E2-S4: arithmetic expansion
  add_test(NAME oosh_arith_basic   COMMAND oosh -c "text(\"%d\") -> print($((3 + 4)))")
  add_test(NAME oosh_arith_var     COMMAND oosh -c "set X \"6\" ; text(\"%d\") -> print($((X * 7)))")
  add_test(NAME oosh_arith_prec    COMMAND oosh -c "text(\"%d\") -> print($((2 + 3 * 4)))")
  add_test(NAME oosh_arith_quoted  COMMAND oosh -c "text(\"%d\") -> print(\"$((10 - 3))\")")

  # E2-S5: binary arithmetic operators in value expression context
  add_test(NAME oosh_binop_add     COMMAND oosh -c "number(3) + number(4)")
  add_test(NAME oosh_binop_sub     COMMAND oosh -c "number(10) - number(3)")
  add_test(NAME oosh_binop_mul     COMMAND oosh -c "number(4) * number(5)")
  add_test(NAME oosh_binop_div     COMMAND oosh -c "number(9) / number(3)")
  add_test(NAME oosh_binop_prec    COMMAND oosh -c "number(2) + number(3) * number(4)")
  add_test(NAME oosh_binop_nested  COMMAND oosh -c "number(6) + number(2) - number(1)")

  # E3-S1: builtin-kind metadata visible via `type`
  add_test(NAME oosh_type_pure   COMMAND oosh -c "type pwd")
  add_test(NAME oosh_type_mutant COMMAND oosh -c "type cd")
  add_test(NAME oosh_type_mixed  COMMAND oosh -c "type alias")

  # E3-S2: PURE built-ins as first stage in shell pipelines
  add_test(NAME oosh_builtin_pipe_pwd     COMMAND oosh -c "pwd | cat")
  add_test(NAME oosh_builtin_pipe_history COMMAND oosh -c "history | cat")
  add_test(NAME oosh_builtin_pipe_mutant  COMMAND oosh -c "cd /tmp | cat")

  set_tests_properties(
    oosh_external_command
    oosh_shell_pipeline
    oosh_glob_command_args
    oosh_env_expansion
    oosh_command_substitution
    oosh_if_command
    oosh_background_job
    oosh_exec_builtin
    oosh_wait_builtin
    oosh_fd_redirect_output_dup
    oosh_fd_redirect_input_dup
    oosh_print_method
    oosh_print_format_method
    oosh_proc_namespace
    oosh_shell_namespace
    oosh_nested_json_roundtrip
    oosh_plugin_value_resolver
    oosh_plugin_pipeline_stage
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    oosh_external_command
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    oosh_shell_pipeline
    oosh_glob_command_args
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    oosh_if_command
    PROPERTIES
      PASS_REGULAR_EXPRESSION "ok"
  )
  set_tests_properties(
    oosh_ternary_true
    PROPERTIES
      PASS_REGULAR_EXPRESSION "yes"
  )
  set_tests_properties(
    oosh_ternary_false
    PROPERTIES
      PASS_REGULAR_EXPRESSION "no"
  )
  set_tests_properties(
    oosh_switch_match
    PROPERTIES
      PASS_REGULAR_EXPRESSION "two"
  )
  set_tests_properties(
    oosh_switch_default
    PROPERTIES
      PASS_REGULAR_EXPRESSION "fallback"
  )
  set_tests_properties(
    oosh_case_exact
    PROPERTIES
      PASS_REGULAR_EXPRESSION "dir"
  )
  set_tests_properties(
    oosh_case_wildcard
    PROPERTIES
      PASS_REGULAR_EXPRESSION "txt"
  )
  set_tests_properties(
    oosh_case_alternatives
    PROPERTIES
      PASS_REGULAR_EXPRESSION "cool"
  )
  set_tests_properties(
    oosh_eval_builtin
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi"
  )
  set_tests_properties(
    oosh_exec_builtin
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello[\r\n]+world"
  )
  set_tests_properties(
    oosh_trap_exit
    PROPERTIES
      PASS_REGULAR_EXPRESSION "run[\r\n]+bye"
  )
  set_tests_properties(
    oosh_heredoc_basic
    oosh_heredoc_pipeline
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    oosh_heredoc_strip_tabs
    PROPERTIES
      PASS_REGULAR_EXPRESSION "one[\r\n]+two"
  )
  set_tests_properties(
    oosh_heredoc_preserve_blank_and_comment
    PROPERTIES
      PASS_REGULAR_EXPRESSION "one[\r\n]+[\r\n]+#kept[\r\n]+three"
  )
  set_tests_properties(
    oosh_inline_if_heredoc
    oosh_group_heredoc_tail
    oosh_subshell_heredoc_tail
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    oosh_fd_redirect_output_dup
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    oosh_fd_redirect_input_dup
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    oosh_block_local_reduce
    PROPERTIES
      PASS_REGULAR_EXPRESSION "6"
  )
  set_tests_properties(
    oosh_block_local_scope_restore
    PROPERTIES
      PASS_REGULAR_EXPRESSION "99"
  )
  set_tests_properties(
    oosh_class_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "animal"
  )
  set_tests_properties(
    oosh_class_init_inheritance
    PROPERTIES
      PASS_REGULAR_EXPRESSION "readme"
  )
  set_tests_properties(
    oosh_class_multiple_inheritance
    PROPERTIES
      PASS_REGULAR_EXPRESSION "left"
  )
  set_tests_properties(
    oosh_class_introspection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "true[\r\n]+Child[\r\n]+1"
  )
  set_tests_properties(
    oosh_print_method
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    oosh_case_builtins_script
    PROPERTIES
      PASS_REGULAR_EXPRESSION "until-once[\r\n]+text-file[\r\n]+eval-ok[\r\n]+script-end[\r\n]+exit-trap"
  )
  set_tests_properties(
    oosh_function_script
    PROPERTIES
      PASS_REGULAR_EXPRESSION "ciao nicolo"
  )
  set_tests_properties(
    oosh_function_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi nicolo"
  )
  set_tests_properties(
    oosh_function_redefine
    PROPERTIES
      PASS_REGULAR_EXPRESSION "two:demo"
  )
  set_tests_properties(
    oosh_function_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer"
  )
  set_tests_properties(
    oosh_break_nested
    PROPERTIES
      PASS_REGULAR_EXPRESSION "once"
  )
  set_tests_properties(
    oosh_continue_nested
    PROPERTIES
      PASS_REGULAR_EXPRESSION "1[\r\n]+2"
  )
  set_tests_properties(
    oosh_function_return
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi"
  )
  set_tests_properties(
    oosh_function_return_from_loop
    PROPERTIES
      PASS_REGULAR_EXPRESSION "1"
  )
  set_tests_properties(
    oosh_break_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "break is only valid inside loops"
  )
  set_tests_properties(
    oosh_continue_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "continue is only valid inside loops"
  )
  set_tests_properties(
    oosh_return_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "return is only valid inside shell functions"
  )
  set_tests_properties(
    oosh_group_binding_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mint"
  )
  set_tests_properties(
    oosh_subshell_binding_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "vanilla"
  )
  set_tests_properties(
    oosh_group_env_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "grouped"
  )
  set_tests_properties(
    oosh_subshell_env_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer"
  )
  set_tests_properties(
    oosh_group_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "grouped"
  )
  set_tests_properties(
    oosh_subshell_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "isolated"
  )
  set_tests_properties(
    oosh_group_input_redirection
    oosh_subshell_input_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "4"
  )
  set_tests_properties(
    oosh_print_format_method
    PROPERTIES
      PASS_REGULAR_EXPRESSION "file:3"
  )
  set_tests_properties(
    oosh_proc_namespace
    PROPERTIES
      PASS_REGULAR_EXPRESSION "[0-9]+"
  )
  set_tests_properties(
    oosh_shell_namespace
    PROPERTIES
      PASS_REGULAR_EXPRESSION "0"
  )
  set_tests_properties(
    oosh_nested_json_roundtrip
    PROPERTIES
      PASS_REGULAR_EXPRESSION "\\{\"a\":\\[1,2,\\{\"b\":true\\}\\],\"name\":\"demo\"\\}"
  )
  set_tests_properties(
    oosh_plugin_value_resolver
    PROPERTIES
      PASS_REGULAR_EXPRESSION "sample-plugin"
  )
  set_tests_properties(
    oosh_plugin_pipeline_stage
    PROPERTIES
      PASS_REGULAR_EXPRESSION "sample:ciao"
  )
  set_tests_properties(
    oosh_ifs_split_count
    PROPERTIES
      PASS_REGULAR_EXPRESSION "3"
  )
  set_tests_properties(
    oosh_ifs_split_first
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    oosh_ifs_quoted_no_split
    PROPERTIES
      PASS_REGULAR_EXPRESSION "x y z"
  )
  set_tests_properties(
    oosh_ifs_custom
    PROPERTIES
      PASS_REGULAR_EXPRESSION "a"
  )
  set_tests_properties(
    oosh_ifs_quote_removal
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello world"
  )
  set_tests_properties(
    oosh_ifs_empty_var
    PROPERTIES
      PASS_REGULAR_EXPRESSION "done"
  )
  set_tests_properties(
    oosh_arith_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "7"
  )
  set_tests_properties(
    oosh_arith_var
    PROPERTIES
      PASS_REGULAR_EXPRESSION "42"
  )
  set_tests_properties(
    oosh_arith_prec
    PROPERTIES
      PASS_REGULAR_EXPRESSION "14"
  )
  set_tests_properties(
    oosh_arith_quoted
    PROPERTIES
      PASS_REGULAR_EXPRESSION "7"
  )
  set_tests_properties(oosh_builtin_pipe_pwd     PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(oosh_builtin_pipe_history PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(oosh_builtin_pipe_mutant  PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(oosh_binop_add    PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(oosh_binop_sub    PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(oosh_binop_mul    PROPERTIES PASS_REGULAR_EXPRESSION "20")
  set_tests_properties(oosh_binop_div    PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(oosh_binop_prec   PROPERTIES PASS_REGULAR_EXPRESSION "14")
  set_tests_properties(oosh_binop_nested PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(
    oosh_type_pure
    PROPERTIES
      PASS_REGULAR_EXPRESSION "pure shell builtin"
  )
  set_tests_properties(
    oosh_type_mutant
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mutant shell builtin"
  )
  set_tests_properties(
    oosh_type_mixed
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mixed shell builtin"
  )
  set_tests_properties(
    oosh_source_file
    oosh_control_flow_script
    oosh_function_script
    oosh_case_builtins_script
    oosh_alias_rc
    oosh_shell_var_precedence
    oosh_function_basic
    oosh_function_redefine
    oosh_function_scope
    oosh_break_nested
    oosh_continue_nested
    oosh_function_return
    oosh_function_return_from_loop
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ENVIRONMENT "OOSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/ooshrc"
  )
  set_tests_properties(
    oosh_shell_var_precedence
    PROPERTIES
      ENVIRONMENT "OOSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/ooshrc;LIMIT=not-a-number"
  )
  set_tests_properties(
    oosh_group_redirection
    oosh_subshell_redirection
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    oosh_heredoc_basic
    oosh_heredoc_strip_tabs
    oosh_heredoc_pipeline
    oosh_heredoc_preserve_blank_and_comment
    oosh_inline_if_heredoc
    oosh_group_heredoc_tail
    oosh_subshell_heredoc_tail
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      ENVIRONMENT "OOSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/ooshrc"
  )
  set_tests_properties(
    oosh_group_input_redirection
    oosh_subshell_input_redirection
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  if(NOT WIN32)
    set_tests_properties(
      oosh_fd_redirect_output_dup
      oosh_fd_redirect_input_dup
      PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
  else()
    set_tests_properties(
      oosh_fd_redirect_output_dup
      oosh_fd_redirect_input_dup
      PROPERTIES
        DISABLED TRUE
    )
  endif()
endif()
