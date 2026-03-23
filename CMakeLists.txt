cmake_minimum_required(VERSION 3.20)

project(arksh VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# E8-S3-T1: AddressSanitizer + UndefinedBehaviorSanitizer
#
# ARKSH_SANITIZE=ON  — full ASan+UBSan; intended for Linux CI (GitHub Actions).
#                     On macOS 14+/SIP, ASan shadow-memory setup may hang at
#                     launch unless the binary is signed with get-task-allow.
#                     Use ARKSH_UBSAN=ON for local macOS development instead.
#
# ARKSH_UBSAN=ON     — UBSan only; works on macOS without special entitlements.
#
# Usage (Linux / CI):
#   cmake -S . -B build-san -DARKSH_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-san && ctest --test-dir build-san
#
# Usage (macOS local):
#   cmake -S . -B build-ubsan -DARKSH_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-ubsan && ctest --test-dir build-ubsan
option(ARKSH_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (Linux CI)" OFF)
option(ARKSH_UBSAN    "Build with UndefinedBehaviorSanitizer only (macOS-safe)"               OFF)
if(ARKSH_SANITIZE AND ARKSH_UBSAN)
  message(FATAL_ERROR "ARKSH_SANITIZE and ARKSH_UBSAN are mutually exclusive")
endif()
if(ARKSH_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "ARKSH_SANITIZE is not supported with MSVC")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "Sanitizers enabled: -fsanitize=address,undefined")
endif()
if(ARKSH_UBSAN)
  if(MSVC)
    message(FATAL_ERROR "ARKSH_UBSAN is not supported with MSVC")
  endif()
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
  message(STATUS "Sanitizers enabled: -fsanitize=undefined")
endif()

# E8-S3-T3: libFuzzer target for lexer/parser/expander.
#
# Usage (requires clang):
#   cmake -S . -B build-fuzz -DARKSH_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug \
#         -DCMAKE_C_COMPILER=clang
#   cmake --build build-fuzz --target arksh_fuzz_input
#   ./build-fuzz/arksh_fuzz_input [corpus/] [-max_len=512]
option(ARKSH_FUZZ "Build libFuzzer target for lexer/parser (requires clang)" OFF)
if(ARKSH_FUZZ)
  if(MSVC)
    message(FATAL_ERROR "ARKSH_FUZZ is not supported with MSVC")
  endif()
  message(STATUS "Fuzzer target enabled: arksh_fuzz_input")
endif()

add_library(arksh_core STATIC
  src/arena.c
  src/expand.c
  src/executor.c
  src/line_editor.c
  src/lexer.c
  src/object.c
  src/parser.c
  src/perf.c
  src/platform.c
  src/plugin.c
  src/prompt.c
  src/shell.c
)

target_include_directories(arksh_core PUBLIC include)

if(WIN32)
  set(ARKSH_PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}/plugins")
  set(ARKSH_RUNTIME_PLUGIN_RELDIR "plugins")
else()
  set(ARKSH_PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/arksh/plugins")
  set(ARKSH_RUNTIME_PLUGIN_RELDIR "../${CMAKE_INSTALL_LIBDIR}/arksh/plugins")
endif()

target_compile_definitions(arksh_core PUBLIC
  ARKSH_RUNTIME_PLUGIN_RELDIR="${ARKSH_RUNTIME_PLUGIN_RELDIR}"
)

if(NOT WIN32)
  target_link_libraries(arksh_core PUBLIC dl)
endif()

add_executable(arksh src/main.c)
target_link_libraries(arksh PRIVATE arksh_core)

add_executable(arksh_perf_runner tests/perf_runner.c)
target_link_libraries(arksh_perf_runner PRIVATE arksh_core)

if(WIN32)
  add_custom_target(arksh_perf
    COMMAND ${CMAKE_COMMAND} -E echo "arksh_perf is currently supported on POSIX hosts only"
    DEPENDS arksh arksh_perf_runner
    USES_TERMINAL
  )
else()
  add_custom_target(arksh_perf
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> startup command "perf show"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> object-pipeline script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/object-pipeline.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> object-chain script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/object-chain.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> registry-lookups script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/registry-lookups.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> json-structured script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/json-structured.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> function-scope script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/function-scope.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> subshell script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/subshell.arksh"
    COMMAND $<TARGET_FILE:arksh_perf_runner> $<TARGET_FILE:arksh> command-substitution script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/command-substitution.arksh"
    DEPENDS arksh arksh_perf_runner
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
  )
endif()

function(arksh_add_plugin target_name source_file)
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

arksh_add_plugin(arksh_sample_plugin plugins/sample/sample_plugin.c)
arksh_add_plugin(arksh_skeleton_plugin plugins/skeleton/skeleton_plugin.c)
# E6-S2-T3: typed-map example plugin (links against libm for sqrt)
arksh_add_plugin(arksh_point_plugin plugins/point/point_plugin.c)
if(NOT WIN32)
  target_link_libraries(arksh_point_plugin PRIVATE m)
endif()
# E6-S9: system trash integration plugin
arksh_add_plugin(arksh_trash_plugin plugins/trash/trash_plugin.c)
if(APPLE)
  target_link_libraries(arksh_trash_plugin PRIVATE "-framework Foundation")
elseif(WIN32)
  target_link_libraries(arksh_trash_plugin PRIVATE Shell32)
endif()

install(TARGETS arksh
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS
  arksh_sample_plugin
  arksh_skeleton_plugin
  arksh_point_plugin
  arksh_trash_plugin
  LIBRARY DESTINATION ${ARKSH_PLUGIN_INSTALL_DIR}
  RUNTIME DESTINATION ${ARKSH_PLUGIN_INSTALL_DIR}
  BUNDLE DESTINATION ${ARKSH_PLUGIN_INSTALL_DIR}
)

install(FILES
  examples/arkshrc
  DESTINATION ${CMAKE_INSTALL_DATADIR}/arksh/examples
  RENAME arkshrc.example
)

install(FILES
  examples/arksh.conf
  DESTINATION ${CMAKE_INSTALL_DATADIR}/arksh/examples
  RENAME prompt.conf.example
)

install(DIRECTORY
  examples/scripts
  DESTINATION ${CMAKE_INSTALL_DATADIR}/arksh/examples
)

include(CTest)

if(BUILD_TESTING)
  add_executable(arksh_test_emit_args tests/emit_args.c)
  add_executable(arksh_test_count_lines tests/count_lines.c)
  add_executable(arksh_test_echo_stdin tests/echo_stdin.c)
  add_executable(arksh_test_pause tests/pause_seconds.c)

  # E8-S1: unit test executables (link against arksh_core for all APIs)
  add_executable(arksh_unit_lexer tests/unit_lexer.c)
  target_link_libraries(arksh_unit_lexer PRIVATE arksh_core)
  add_executable(arksh_unit_parser tests/unit_parser.c)
  target_link_libraries(arksh_unit_parser PRIVATE arksh_core)
  # E8-S1-T3: executor unit tests
  add_executable(arksh_unit_executor tests/unit_executor.c)
  target_link_libraries(arksh_unit_executor PRIVATE arksh_core)
  # E8-S1-T4: object model unit tests (link with math library for fabs())
  add_executable(arksh_unit_object tests/unit_object.c)
  target_link_libraries(arksh_unit_object PRIVATE arksh_core m)
  add_executable(arksh_unit_arena tests/unit_arena.c)
  target_link_libraries(arksh_unit_arena PRIVATE arksh_core)

  # E8-S3-T3: libFuzzer target — built only when ARKSH_FUZZ=ON
  if(ARKSH_FUZZ)
    add_executable(arksh_fuzz_input tests/fuzz_input.c)
    target_link_libraries(arksh_fuzz_input PRIVATE arksh_core)
    target_compile_options(arksh_fuzz_input PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(arksh_fuzz_input PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()

  file(COPY tests/fixtures DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/tests")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime/config-home")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime/state-home")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/arkshrc"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/config-home/arkshrc"
    COPYONLY
  )
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/arksh.conf"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/config-home/prompt.conf"
    COPYONLY
  )

  if(WIN32)
    set(ARKSH_TEST_EMIT "arksh_test_emit_args.exe")
    set(ARKSH_TEST_COUNT "arksh_test_count_lines.exe")
    set(ARKSH_TEST_ECHO "arksh_test_echo_stdin.exe")
    set(ARKSH_TEST_PAUSE "arksh_test_pause.exe")
  else()
    set(ARKSH_TEST_EMIT "./arksh_test_emit_args")
    set(ARKSH_TEST_COUNT "./arksh_test_count_lines")
    set(ARKSH_TEST_ECHO "./arksh_test_echo_stdin")
    set(ARKSH_TEST_PAUSE "./arksh_test_pause")
  endif()

  add_test(NAME arksh_help COMMAND arksh -c help)
  add_test(NAME arksh_object_type COMMAND arksh -c "obj(\".\").type")
  add_test(NAME arksh_human_object_type COMMAND arksh -c ". -> type")
  add_test(NAME arksh_pipeline_count COMMAND arksh -c ". -> children() |> count()")
  add_test(NAME arksh_pipeline_sort_numbers COMMAND arksh -c "list(1, 20, 3) |> sort(value desc)")
  add_test(NAME arksh_pipeline_sort_property COMMAND arksh -c ". -> children() |> where(type == \"file\") |> sort(size desc)")
  add_test(NAME arksh_external_command COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" hello")
  add_test(NAME arksh_shell_pipeline COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" alpha beta | \"${ARKSH_TEST_COUNT}\"")
  add_test(NAME arksh_glob_command_args COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" tests/fixtures/glob/*.fixture | \"${ARKSH_TEST_COUNT}\"")
  add_test(NAME arksh_env_expansion COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" \"\$PWD\"")
  add_test(NAME arksh_command_substitution COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" \$(pwd)")
  add_test(NAME arksh_history_builtin COMMAND arksh -c "history")
  add_test(NAME arksh_sequence_operator COMMAND arksh -c "true ; true")
  add_test(NAME arksh_and_operator COMMAND arksh -c "true && true")
  add_test(NAME arksh_or_operator COMMAND arksh -c "false || true")
  add_test(NAME arksh_if_command COMMAND arksh -c "if true ; then \"${ARKSH_TEST_EMIT}\" ok ; fi")
  add_test(NAME arksh_if_value_condition COMMAND arksh -c "if bool(false) ; then false ; else true ; fi")
  add_test(NAME arksh_ternary_true COMMAND arksh -c "bool(true) ? \"yes\" : \"no\"")
  add_test(NAME arksh_ternary_false COMMAND arksh -c "1 == 2 ? \"yes\" : \"no\"")
  add_test(NAME arksh_while_command COMMAND arksh -c "while false ; do true ; done")
  add_test(NAME arksh_for_command COMMAND arksh -c "for n in list(1, 2, 3) ; do n -> value ; done")
  add_test(NAME arksh_switch_match COMMAND arksh -c "switch 2 ; case 1 ; then text(\"one\") -> print() ; case 2 ; then text(\"two\") -> print() ; default ; then text(\"other\") -> print() ; endswitch")
  add_test(NAME arksh_switch_default COMMAND arksh -c "switch \"missing\" ; case \"one\" ; then text(\"one\") -> print() ; default ; then text(\"fallback\") -> print() ; endswitch")
  add_test(NAME arksh_block_local_reduce COMMAND arksh -c "list(1, 2, 3) |> reduce(number(0), [:acc :n | local next = acc + n ; next])")
  add_test(NAME arksh_block_local_scope_restore COMMAND arksh -c "let next = 99 ; let sum = list(1, 2) |> reduce(number(0), [:acc :n | local next = acc + n ; next]) ; text(\"%v\") -> print(next)")
  add_test(NAME arksh_background_job COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 1 & jobs")
  add_test(NAME arksh_source_file COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/arkshrc")
  add_test(NAME arksh_source_fg COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/control/source-fg.arksh")
  add_test(NAME arksh_control_flow_script COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/04-control-flow.arksh")
  add_test(NAME arksh_function_script COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/05-shell-functions.arksh")
  add_test(NAME arksh_case_builtins_script COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts/07-case-and-builtins.arksh")
  add_test(NAME arksh_alias_rc COMMAND arksh -c "type ll")
  add_test(NAME arksh_shell_var_precedence COMMAND arksh -c "README.md -> read_text(\$LIMIT)")
  add_test(NAME arksh_function_basic COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/basic.arksh")
  add_test(NAME arksh_function_redefine COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/redefine.arksh")
  add_test(NAME arksh_function_scope COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/scope.arksh")
  add_test(NAME arksh_function_local_shadow
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scope/function-local-shadow.arksh")
  add_test(NAME arksh_function_shift_scope
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scope/function-shift-scope.arksh")
  add_test(NAME arksh_command_substitution_local_scope
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scope/function-command-substitution.arksh")
  add_test(NAME arksh_subshell_rich_state_scope
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scope/subshell-rich-state.arksh")
  add_test(NAME arksh_command_substitution_rich_state_scope
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scope/command-substitution-rich-state.arksh")
  add_test(NAME arksh_break_nested COMMAND arksh -c "while true ; do while true ; do text(\"once\") -> print() ; break 2 ; done ; done")
  add_test(NAME arksh_continue_nested COMMAND arksh -c "for outer in list(1, 2) ; do while true ; do outer -> print() ; continue 2 ; done ; done")
  add_test(NAME arksh_function_return COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/return.arksh")
  add_test(NAME arksh_function_return_from_loop COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/return-from-loop.arksh")
  add_test(NAME arksh_break_invalid COMMAND arksh -c "break || true")
  add_test(NAME arksh_continue_invalid COMMAND arksh -c "continue || true")
  add_test(NAME arksh_return_invalid COMMAND arksh -c "return text(\"x\") || true")
  add_test(NAME arksh_until_command COMMAND arksh -c "until true ; do text(\"retry\") -> print() ; done")
  add_test(NAME arksh_case_exact COMMAND arksh -c "case text(\"directory\") in directory) text(\"dir\") -> print() ;; *) text(\"other\") -> print() ;; esac")
  add_test(NAME arksh_case_wildcard COMMAND arksh -c "case text(\"demo.txt\") in *.md) text(\"md\") -> print() ;; *.txt) text(\"txt\") -> print() ;; esac")
  add_test(NAME arksh_case_alternatives COMMAND arksh -c "case text(\"blue\") in red|green) text(\"warm\") -> print() ;; blue|cyan) text(\"cool\") -> print() ;; esac")
  add_test(NAME arksh_eval_builtin COMMAND arksh -c "eval 'text(\"hi\") -> print()'")
  add_test(NAME arksh_exec_builtin COMMAND arksh -c "exec \"${ARKSH_TEST_EMIT}\" hello world")
  add_test(NAME arksh_wait_builtin COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & wait %1")
  add_test(NAME arksh_wait_done_message COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & wait %1")
  add_test(NAME arksh_jobs_current_marker COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 1 & jobs")
  set_tests_properties(arksh_source_fg PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PASS_REGULAR_EXPRESSION "\\[1\\].*running pid="
  )
  add_test(NAME arksh_pipeline_three_stage COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" a b c | \"${ARKSH_TEST_ECHO}\" | \"${ARKSH_TEST_COUNT}\"")
  add_test(NAME arksh_pipeline_sigpipe_safe COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" x y z | \"${ARKSH_TEST_COUNT}\"")
  add_test(NAME arksh_bridge_capture_lines COMMAND arksh -c "capture_lines(\"${ARKSH_TEST_EMIT} a b c\") |> count()")
  # E4-S4: force_capture path (capture() sets force_capture=1 — exercises the
  # interactive-detection + capture-pipe branch on both POSIX and Windows)
  add_test(NAME arksh_capture_text_first
    COMMAND arksh -c "capture(\"${ARKSH_TEST_EMIT} hello\") |> lines() |> first()")
  add_test(NAME arksh_capture_text_count
    COMMAND arksh -c "capture(\"${ARKSH_TEST_EMIT} alpha beta\") |> lines() |> count()")
  add_test(NAME arksh_bridge_multiword COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" x y z |> lines() |> count()")
  add_test(NAME arksh_trap_exit COMMAND arksh -c "trap 'text(\"bye\") -> print()' EXIT ; text(\"run\") -> print()")
  add_test(NAME arksh_heredoc_basic COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.arksh")
  add_test(NAME arksh_heredoc_strip_tabs COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/strip-tabs.arksh")
  add_test(NAME arksh_heredoc_pipeline COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/pipeline.arksh")
  add_test(NAME arksh_heredoc_preserve_blank_and_comment COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/preserve-blank-and-comment.arksh")
  add_test(NAME arksh_inline_if_heredoc COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/inline-if.arksh")
  add_test(NAME arksh_group_heredoc_tail COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/group-tail.arksh")
  add_test(NAME arksh_subshell_heredoc_tail COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/subshell-tail.arksh")
  add_test(NAME arksh_fd_redirect_output_dup COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" hello 3> fd3.out 1>&3 ; fd3.out -> read_text(64)")
  add_test(NAME arksh_fd_redirect_input_dup COMMAND arksh -c "\"${ARKSH_TEST_EMIT}\" left right > fdin.txt ; \"${ARKSH_TEST_COUNT}\" 3< fdin.txt 0<&3")
  add_test(NAME arksh_group_binding_scope COMMAND arksh -c "{ let flavor = text(\"mint\") ; } ; flavor -> value")
  add_test(NAME arksh_subshell_binding_scope COMMAND arksh -c "let flavor = text(\"vanilla\") ; ( let flavor = text(\"mint\") ) ; flavor -> value")
  add_test(NAME arksh_group_env_scope COMMAND arksh -c "{ set MODE grouped ; } ; text(\"%s\") -> print(\"\$MODE\")")
  add_test(NAME arksh_subshell_env_scope COMMAND arksh -c "set MODE outer ; ( set MODE inner ) ; text(\"%s\") -> print(\"\$MODE\")")
  add_test(NAME arksh_group_redirection COMMAND arksh -c "{ text(\"grouped\") -> print() ; } > group_scope.out ; group_scope.out -> read_text(32)")
  add_test(NAME arksh_subshell_redirection COMMAND arksh -c "( text(\"isolated\") -> print() ) > subshell_scope.out ; subshell_scope.out -> read_text(32)")
  add_test(NAME arksh_group_input_redirection COMMAND arksh -c "{ ${ARKSH_TEST_COUNT} ; } < ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.arksh")
  add_test(NAME arksh_subshell_input_redirection COMMAND arksh -c "( ${ARKSH_TEST_COUNT} ) < ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/heredoc/basic.arksh")
  add_test(NAME arksh_class_basic COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/basic.arksh")
  add_test(NAME arksh_class_init_inheritance COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/init-and-inheritance.arksh")
  add_test(NAME arksh_class_multiple_inheritance COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/multiple-inheritance.arksh")
  add_test(NAME arksh_class_introspection COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/classes/introspection.arksh")
  add_test(NAME arksh_print_method COMMAND arksh -c "text(\"hello\") -> print()")
  add_test(NAME arksh_print_format_method COMMAND arksh -c "text(\"%s:%d\") -> print(\"file\", 3)")
  add_test(NAME arksh_proc_namespace COMMAND arksh -c "proc() -> pid")
  add_test(NAME arksh_shell_namespace COMMAND arksh -c "shell() -> plugins |> count()")
  add_test(NAME arksh_nested_json_roundtrip COMMAND arksh -c "tests/fixtures/json/nested.json -> read_json() |> to_json()")
  add_test(NAME arksh_json_get_path COMMAND arksh -c "tests/fixtures/json/nested.json -> read_json() -> get_path(\"a[2].b\")")
  add_test(NAME arksh_json_chain_pipeline_render COMMAND arksh -c "tests/fixtures/json/nested.json -> read_json() -> get_path(\"meta.version\") |> render()")
  add_test(NAME arksh_pipeline_pluck COMMAND arksh -c "list(map(\"name\", \"alpha\"), map(\"name\", \"beta\")) |> pluck(\"name\") |> join(\",\")")
  add_test(NAME arksh_plugin_value_resolver COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_sample_plugin> ; sample() -> name")
  add_test(NAME arksh_plugin_pipeline_stage COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_sample_plugin> ; text(\"ciao\") |> sample_wrap()")
  add_test(NAME arksh_standard_rc_dir COMMAND arksh -c "type docs")
  set_tests_properties(arksh_standard_rc_dir PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;USERPROFILE=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;ARKSH_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/config-home"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_standard_state_dir COMMAND arksh -c "shell() -> history_path")
  set_tests_properties(arksh_standard_state_dir PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;USERPROFILE=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;ARKSH_STATE_HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/state-home"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PASS_REGULAR_EXPRESSION "state-home.*history"
  )
  add_test(NAME arksh_standard_plugin_dir COMMAND arksh -c "plugin load sample-plugin ; plugin info sample-plugin")
  set_tests_properties(arksh_standard_plugin_dir PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;USERPROFILE=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;ARKSH_PLUGIN_HOME=${CMAKE_CURRENT_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PASS_REGULAR_EXPRESSION "name=sample-plugin"
  )
  set_tests_properties(
    arksh_subshell_rich_state_scope
    arksh_command_substitution_rich_state_scope
    PROPERTIES
      ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;USERPROFILE=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;ARKSH_PLUGIN_HOME=${CMAKE_CURRENT_BINARY_DIR}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_standard_plugin_autoload_set COMMAND arksh -c "plugin autoload set sample-plugin ; plugin autoload list")
  set_tests_properties(arksh_standard_plugin_autoload_set PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;USERPROFILE=${CMAKE_CURRENT_BINARY_DIR}/runtime/home-empty;ARKSH_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/runtime/config-home;ARKSH_PLUGIN_HOME=${CMAKE_CURRENT_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PASS_REGULAR_EXPRESSION "arksh_sample_plugin"
  )
  add_test(NAME arksh_perf_builtin_show COMMAND arksh -c "perf on ; perf reset ; . -> type ; perf show")
  set_tests_properties(arksh_perf_builtin_show PROPERTIES
    PASS_REGULAR_EXPRESSION "enabled=1"
  )
  add_test(NAME arksh_perf_runner_smoke COMMAND arksh_perf_runner $<TARGET_FILE:arksh> startup command "perf show")
  set_tests_properties(arksh_perf_runner_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "case=startup"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME arksh_perf_registry_lookup_smoke
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> registry-lookups script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/registry-lookups.arksh")
  set_tests_properties(arksh_perf_registry_lookup_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "case=registry-lookups"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME arksh_perf_startup_base_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> startup command "perf show" "calloc_bytes<=2200000")
  add_test(NAME arksh_perf_object_pipeline_footprint_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> object-pipeline script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/object-pipeline.arksh" "max_rss_kb<=42000")
  add_test(NAME arksh_perf_object_chain_render_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> object-chain script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/object-chain.arksh" "value_render_calls<=40")
  add_test(NAME arksh_perf_json_structured_footprint_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> json-structured script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/json-structured.arksh" "max_rss_kb<=72000")
  add_test(NAME arksh_perf_function_scope_alloc_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> function-scope script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/function-scope.arksh" "calloc_calls<=400")
  add_test(NAME arksh_perf_subshell_alloc_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> subshell script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/subshell.arksh" "max_rss_kb<=55000")
  add_test(NAME arksh_perf_command_subst_alloc_drop
    COMMAND arksh_perf_runner $<TARGET_FILE:arksh> command-substitution script "${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/command-substitution.arksh" "calloc_calls<=90")
  set_tests_properties(
    arksh_perf_startup_base_drop
    arksh_perf_object_pipeline_footprint_drop
    arksh_perf_object_chain_render_drop
    arksh_perf_json_structured_footprint_drop
    arksh_perf_function_scope_alloc_drop
    arksh_perf_subshell_alloc_drop
    arksh_perf_command_subst_alloc_drop
    arksh_perf_registry_lookup_smoke
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME arksh_positional_in_function COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/positional-in-function.arksh")
  add_test(NAME arksh_positional_count COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-count.arksh")
  add_test(NAME arksh_positional_all COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-all.arksh")
  add_test(NAME arksh_positional_shell_pid COMMAND arksh -c "text(\"%s\") -> print(\"$$\")")
  add_test(NAME arksh_positional_source_args COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/source-params.arksh hello")
  add_test(NAME arksh_positional_scope_restore COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/scope-restore.arksh")
  add_test(NAME arksh_param_expand_default COMMAND arksh -c "text(\"%s\") -> print(\"\${UNDEF:-fallback}\")")
  add_test(NAME arksh_param_expand_set_nodefault COMMAND arksh -c "set X hello ; text(\"%s\") -> print(\"\${X:-fallback}\")")
  add_test(NAME arksh_param_expand_assign COMMAND arksh -c "text(\"%s\") -> print(\"\${NEW:=assigned}\") ; text(\"%s\") -> print(\"\$NEW\")")
  add_test(NAME arksh_param_expand_alt COMMAND arksh -c "set Y yes ; text(\"%s\") -> print(\"\${Y:+alt}\")")
  add_test(NAME arksh_param_expand_alt_unset COMMAND arksh -c "text(\"%s\") -> print(\"\${NOPE:+alt}\")")
  add_test(NAME arksh_param_expand_length COMMAND arksh -c "set WORD hello ; text(\"%s\") -> print(\"\${#WORD}\")")
  add_test(NAME arksh_param_expand_suffix_shortest COMMAND arksh -c "set F file.tar.gz ; text(\"%s\") -> print(\"\${F%.*}\")")
  add_test(NAME arksh_param_expand_suffix_longest COMMAND arksh -c "set F file.tar.gz ; text(\"%s\") -> print(\"\${F%%.*}\")")
  add_test(NAME arksh_param_expand_prefix_shortest COMMAND arksh -c "set P /usr/local/bin ; text(\"%s\") -> print(\"\${P#/usr}\")")
  add_test(NAME arksh_param_expand_prefix_longest COMMAND arksh -c "set P /usr/local/bin ; text(\"%s\") -> print(\"\${P##*/}\")")
  add_test(NAME arksh_param_expand_script COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/params/param-expansion.arksh")
  add_test(NAME arksh_ifs_split_count COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/split-count.arksh")
  add_test(NAME arksh_ifs_split_first COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/split-first.arksh")
  add_test(NAME arksh_ifs_quoted_no_split COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/quoted-no-split.arksh")
  add_test(NAME arksh_ifs_custom COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/custom-ifs.arksh")
  add_test(NAME arksh_ifs_quote_removal COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/quote-removal.arksh")
  add_test(NAME arksh_ifs_empty_var COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ifs/empty-var.arksh")

  # E2-S4: arithmetic expansion
  add_test(NAME arksh_arith_basic   COMMAND arksh -c "text(\"%d\") -> print($((3 + 4)))")
  add_test(NAME arksh_arith_var     COMMAND arksh -c "set X \"6\" ; text(\"%d\") -> print($((X * 7)))")
  add_test(NAME arksh_arith_prec    COMMAND arksh -c "text(\"%d\") -> print($((2 + 3 * 4)))")
  add_test(NAME arksh_arith_quoted  COMMAND arksh -c "text(\"%d\") -> print(\"$((10 - 3))\")")

  # E2-S5: binary arithmetic operators in value expression context
  add_test(NAME arksh_binop_add     COMMAND arksh -c "number(3) + number(4)")
  add_test(NAME arksh_binop_sub     COMMAND arksh -c "number(10) - number(3)")
  add_test(NAME arksh_binop_mul     COMMAND arksh -c "number(4) * number(5)")
  add_test(NAME arksh_binop_div     COMMAND arksh -c "number(9) / number(3)")
  add_test(NAME arksh_binop_prec    COMMAND arksh -c "number(2) + number(3) * number(4)")
  add_test(NAME arksh_binop_nested  COMMAND arksh -c "number(6) + number(2) - number(1)")

  # E3-S1: builtin-kind metadata visible via `type`
  add_test(NAME arksh_type_pure   COMMAND arksh -c "type pwd")
  add_test(NAME arksh_type_mutant COMMAND arksh -c "type cd")
  add_test(NAME arksh_type_mixed  COMMAND arksh -c "type alias")

  # E3-S2: PURE built-ins as first stage in shell pipelines
  add_test(NAME arksh_builtin_pipe_pwd     COMMAND arksh -c "pwd | cat")
  add_test(NAME arksh_builtin_pipe_history COMMAND arksh -c "history | cat")
  add_test(NAME arksh_builtin_pipe_mutant  COMMAND arksh -c "cd /tmp | cat")

  # E3-S2-T3: built-in stderr/FD redirections
  add_test(NAME arksh_builtin_redir_err_trunc  COMMAND arksh -c "history 2> /dev/null")
  add_test(NAME arksh_builtin_redir_err_append COMMAND arksh -c "history 2>> /dev/null")
  add_test(NAME arksh_builtin_redir_err_merge  COMMAND arksh -c "pwd 2>&1")

  # E3-S3: shell command as object pipeline source (bridge)
  add_test(NAME arksh_shell_obj_bridge_count  COMMAND arksh -c "capture(\"ls\") |> lines |> count")
  add_test(NAME arksh_shell_obj_bridge_words  COMMAND arksh -c "echo hello world |> lines |> count")
  add_test(NAME arksh_shell_obj_bridge_trim   COMMAND arksh -c "echo hello |> trim")

  # grep stage: filter lines/list items by substring
  add_test(NAME arksh_grep_list     COMMAND arksh -c "capture_lines(\"ls /usr\") |> grep(\"lib\") |> count")
  add_test(NAME arksh_grep_string   COMMAND arksh -c "echo hello |> grep(\"hello\")")
  add_test(NAME arksh_grep_no_match COMMAND arksh -c "capture_lines(\"ls /usr\") |> grep(\"ZZZNOMATCH\") |> count")

  # E3-S4: boolean literals in object expressions and conditions
  add_test(NAME arksh_bool_lit_true_value   COMMAND arksh -c "true -> value")
  add_test(NAME arksh_bool_lit_false_type   COMMAND arksh -c "false -> type")
  add_test(NAME arksh_bool_lit_true_type    COMMAND arksh -c "true -> type")
  add_test(NAME arksh_bool_lit_if_true      COMMAND arksh -c "if true ; then echo yes ; fi")
  add_test(NAME arksh_bool_lit_if_false     COMMAND arksh -c "if false ; then echo wrong ; else echo ok ; fi")

  # E3-S5: command override + builtin escape
  add_test(NAME arksh_builtin_cd_override
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/builtin-cd-override.arksh")
  add_test(NAME arksh_builtin_pwd_override
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/builtin-pwd-override.arksh")
  add_test(NAME arksh_builtin_escape
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/builtin-escape.arksh")

  set_tests_properties(
    arksh_external_command
    arksh_shell_pipeline
    arksh_glob_command_args
    arksh_env_expansion
    arksh_command_substitution
    arksh_if_command
    arksh_background_job
    arksh_exec_builtin
    arksh_wait_builtin
    arksh_wait_done_message
    arksh_jobs_current_marker
    arksh_pipeline_three_stage
    arksh_pipeline_sigpipe_safe
    arksh_bridge_capture_lines
    arksh_bridge_multiword
    arksh_fd_redirect_output_dup
    arksh_fd_redirect_input_dup
    arksh_print_method
    arksh_print_format_method
    arksh_proc_namespace
    arksh_shell_namespace
    arksh_nested_json_roundtrip
    arksh_json_get_path
    arksh_pipeline_pluck
    arksh_plugin_value_resolver
    arksh_plugin_pipeline_stage
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_external_command
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    arksh_shell_pipeline
    arksh_glob_command_args
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    arksh_json_get_path
    PROPERTIES
      PASS_REGULAR_EXPRESSION "true"
  )
  set_tests_properties(
    arksh_pipeline_pluck
    PROPERTIES
      PASS_REGULAR_EXPRESSION "alpha,beta"
  )
  set_tests_properties(
    arksh_if_command
    PROPERTIES
      PASS_REGULAR_EXPRESSION "ok"
  )
  set_tests_properties(
    arksh_ternary_true
    PROPERTIES
      PASS_REGULAR_EXPRESSION "yes"
  )
  set_tests_properties(
    arksh_ternary_false
    PROPERTIES
      PASS_REGULAR_EXPRESSION "no"
  )
  set_tests_properties(
    arksh_switch_match
    PROPERTIES
      PASS_REGULAR_EXPRESSION "two"
  )
  set_tests_properties(
    arksh_switch_default
    PROPERTIES
      PASS_REGULAR_EXPRESSION "fallback"
  )
  set_tests_properties(
    arksh_case_exact
    PROPERTIES
      PASS_REGULAR_EXPRESSION "dir"
  )
  set_tests_properties(
    arksh_case_wildcard
    PROPERTIES
      PASS_REGULAR_EXPRESSION "txt"
  )
  set_tests_properties(
    arksh_case_alternatives
    PROPERTIES
      PASS_REGULAR_EXPRESSION "cool"
  )
  set_tests_properties(
    arksh_eval_builtin
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi"
  )
  set_tests_properties(
    arksh_exec_builtin
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello[\r\n]+world"
  )
  set_tests_properties(
    arksh_trap_exit
    PROPERTIES
      PASS_REGULAR_EXPRESSION "run[\r\n]+bye"
  )
  set_tests_properties(
    arksh_heredoc_basic
    arksh_heredoc_pipeline
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    arksh_heredoc_strip_tabs
    PROPERTIES
      PASS_REGULAR_EXPRESSION "one[\r\n]+two"
  )
  set_tests_properties(
    arksh_heredoc_preserve_blank_and_comment
    PROPERTIES
      PASS_REGULAR_EXPRESSION "one[\r\n]+[\r\n]+#kept[\r\n]+three"
  )
  set_tests_properties(
    arksh_inline_if_heredoc
    arksh_group_heredoc_tail
    arksh_subshell_heredoc_tail
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    arksh_fd_redirect_output_dup
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    arksh_fd_redirect_input_dup
    PROPERTIES
      PASS_REGULAR_EXPRESSION "2"
  )
  set_tests_properties(
    arksh_block_local_reduce
    PROPERTIES
      PASS_REGULAR_EXPRESSION "6"
  )
  set_tests_properties(
    arksh_block_local_scope_restore
    PROPERTIES
      PASS_REGULAR_EXPRESSION "99"
  )
  set_tests_properties(
    arksh_class_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "animal"
  )
  set_tests_properties(
    arksh_class_init_inheritance
    PROPERTIES
      PASS_REGULAR_EXPRESSION "readme"
  )
  set_tests_properties(
    arksh_class_multiple_inheritance
    PROPERTIES
      PASS_REGULAR_EXPRESSION "left"
  )
  set_tests_properties(
    arksh_class_introspection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "true[\r\n]+Child[\r\n]+1"
  )
  set_tests_properties(
    arksh_print_method
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    arksh_case_builtins_script
    PROPERTIES
      PASS_REGULAR_EXPRESSION "until-once[\r\n]+text-file[\r\n]+eval-ok[\r\n]+script-end[\r\n]+exit-trap"
  )
  set_tests_properties(
    arksh_function_script
    PROPERTIES
      PASS_REGULAR_EXPRESSION "ciao nicolo"
  )
  set_tests_properties(
    arksh_function_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi nicolo"
  )
  set_tests_properties(
    arksh_function_redefine
    PROPERTIES
      PASS_REGULAR_EXPRESSION "two:demo"
  )
  set_tests_properties(
    arksh_function_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer"
  )
  set_tests_properties(
    arksh_break_nested
    PROPERTIES
      PASS_REGULAR_EXPRESSION "once"
  )
  set_tests_properties(
    arksh_continue_nested
    PROPERTIES
      PASS_REGULAR_EXPRESSION "1[\r\n]+2"
  )
  set_tests_properties(
    arksh_function_return
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hi"
  )
  set_tests_properties(
    arksh_function_return_from_loop
    PROPERTIES
      PASS_REGULAR_EXPRESSION "1"
  )
  set_tests_properties(
    arksh_break_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "break is only valid inside loops"
  )
  set_tests_properties(
    arksh_continue_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "continue is only valid inside loops"
  )
  set_tests_properties(
    arksh_return_invalid
    PROPERTIES
      PASS_REGULAR_EXPRESSION "return is only valid inside shell functions"
  )
  set_tests_properties(
    arksh_group_binding_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mint"
  )
  set_tests_properties(
    arksh_subshell_binding_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "vanilla"
  )
  set_tests_properties(
    arksh_group_env_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "grouped"
  )
  set_tests_properties(
    arksh_subshell_env_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer"
  )
  set_tests_properties(
    arksh_subshell_rich_state_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer[\r\n]+outer[\r\n]+no plugins loaded"
  )
  set_tests_properties(
    arksh_command_substitution_rich_state_scope
    PROPERTIES
      PASS_REGULAR_EXPRESSION "outer[\r\n]+outer[\r\n]+no plugins loaded"
  )
  set_tests_properties(
    arksh_group_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "grouped"
  )
  set_tests_properties(
    arksh_subshell_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "isolated"
  )
  set_tests_properties(
    arksh_group_input_redirection
    arksh_subshell_input_redirection
    PROPERTIES
      PASS_REGULAR_EXPRESSION "4"
  )
  set_tests_properties(
    arksh_print_format_method
    PROPERTIES
      PASS_REGULAR_EXPRESSION "file:3"
  )
  set_tests_properties(
    arksh_proc_namespace
    PROPERTIES
      PASS_REGULAR_EXPRESSION "[0-9]+"
  )
  set_tests_properties(
    arksh_shell_namespace
    PROPERTIES
      PASS_REGULAR_EXPRESSION "0"
  )
  set_tests_properties(
    arksh_nested_json_roundtrip
    PROPERTIES
      PASS_REGULAR_EXPRESSION "\\{\"a\":\\[1,2,\\{\"b\":true\\}\\],\"name\":\"demo\"\\}"
  )
  set_tests_properties(
    arksh_plugin_value_resolver
    PROPERTIES
      PASS_REGULAR_EXPRESSION "sample-plugin"
  )
  set_tests_properties(
    arksh_plugin_pipeline_stage
    PROPERTIES
      PASS_REGULAR_EXPRESSION "sample:ciao"
  )
  set_tests_properties(
    arksh_ifs_split_count
    PROPERTIES
      PASS_REGULAR_EXPRESSION "3"
  )
  set_tests_properties(
    arksh_ifs_split_first
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello"
  )
  set_tests_properties(
    arksh_ifs_quoted_no_split
    PROPERTIES
      PASS_REGULAR_EXPRESSION "x y z"
  )
  set_tests_properties(
    arksh_ifs_custom
    PROPERTIES
      PASS_REGULAR_EXPRESSION "a"
  )
  set_tests_properties(
    arksh_ifs_quote_removal
    PROPERTIES
      PASS_REGULAR_EXPRESSION "hello world"
  )
  set_tests_properties(
    arksh_ifs_empty_var
    PROPERTIES
      PASS_REGULAR_EXPRESSION "done"
  )
  set_tests_properties(
    arksh_arith_basic
    PROPERTIES
      PASS_REGULAR_EXPRESSION "7"
  )
  set_tests_properties(
    arksh_arith_var
    PROPERTIES
      PASS_REGULAR_EXPRESSION "42"
  )
  set_tests_properties(
    arksh_arith_prec
    PROPERTIES
      PASS_REGULAR_EXPRESSION "14"
  )
  set_tests_properties(
    arksh_arith_quoted
    PROPERTIES
      PASS_REGULAR_EXPRESSION "7"
  )
  set_tests_properties(arksh_builtin_pipe_pwd     PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(arksh_builtin_pipe_history PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(arksh_builtin_pipe_mutant  PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(arksh_builtin_redir_err_trunc  PROPERTIES PASS_REGULAR_EXPRESSION ".*")
  set_tests_properties(arksh_builtin_redir_err_append PROPERTIES PASS_REGULAR_EXPRESSION ".*")
  set_tests_properties(arksh_builtin_redir_err_merge  PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(arksh_shell_obj_bridge_count PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")
  set_tests_properties(arksh_shell_obj_bridge_words PROPERTIES PASS_REGULAR_EXPRESSION "1")
  set_tests_properties(arksh_shell_obj_bridge_trim  PROPERTIES PASS_REGULAR_EXPRESSION "hello")
  set_tests_properties(arksh_grep_list      PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")
  set_tests_properties(arksh_grep_string    PROPERTIES PASS_REGULAR_EXPRESSION "hello")
  set_tests_properties(arksh_grep_no_match  PROPERTIES PASS_REGULAR_EXPRESSION "0")
  set_tests_properties(arksh_bool_lit_true_value  PROPERTIES PASS_REGULAR_EXPRESSION "true")
  set_tests_properties(arksh_bool_lit_false_type  PROPERTIES PASS_REGULAR_EXPRESSION "bool")
  set_tests_properties(arksh_bool_lit_true_type   PROPERTIES PASS_REGULAR_EXPRESSION "bool")
  set_tests_properties(arksh_bool_lit_if_true     PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_bool_lit_if_false    PROPERTIES PASS_REGULAR_EXPRESSION "ok")
  set_tests_properties(arksh_builtin_cd_override  PROPERTIES PASS_REGULAR_EXPRESSION "tmp")
  set_tests_properties(arksh_builtin_pwd_override PROPERTIES PASS_REGULAR_EXPRESSION "overridden")
  set_tests_properties(arksh_builtin_escape       PROPERTIES PASS_REGULAR_EXPRESSION ".+")
  set_tests_properties(arksh_binop_add    PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(arksh_binop_sub    PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(arksh_binop_mul    PROPERTIES PASS_REGULAR_EXPRESSION "20")
  set_tests_properties(arksh_binop_div    PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_binop_prec   PROPERTIES PASS_REGULAR_EXPRESSION "14")
  set_tests_properties(arksh_binop_nested PROPERTIES PASS_REGULAR_EXPRESSION "7")
  set_tests_properties(
    arksh_type_pure
    PROPERTIES
      PASS_REGULAR_EXPRESSION "pure shell builtin"
  )
  set_tests_properties(
    arksh_type_mutant
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mutant shell builtin"
  )
  set_tests_properties(
    arksh_type_mixed
    PROPERTIES
      PASS_REGULAR_EXPRESSION "mixed shell builtin"
  )
  set_tests_properties(
    arksh_source_file
    arksh_control_flow_script
    arksh_function_script
    arksh_case_builtins_script
    arksh_alias_rc
    arksh_shell_var_precedence
    arksh_function_basic
    arksh_function_redefine
    arksh_function_scope
    arksh_break_nested
    arksh_continue_nested
    arksh_function_return
    arksh_function_return_from_loop
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ENVIRONMENT "ARKSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/arkshrc"
  )
  set_tests_properties(
    arksh_shell_var_precedence
    PROPERTIES
      ENVIRONMENT "ARKSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/arkshrc;LIMIT=not-a-number"
  )
  set_tests_properties(
    arksh_group_redirection
    arksh_subshell_redirection
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_heredoc_basic
    arksh_heredoc_strip_tabs
    arksh_heredoc_pipeline
    arksh_heredoc_preserve_blank_and_comment
    arksh_inline_if_heredoc
    arksh_group_heredoc_tail
    arksh_subshell_heredoc_tail
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      ENVIRONMENT "ARKSH_RC=${CMAKE_CURRENT_SOURCE_DIR}/examples/arkshrc"
  )
  set_tests_properties(
    arksh_group_input_redirection
    arksh_subshell_input_redirection
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(arksh_wait_done_message    PROPERTIES PASS_REGULAR_EXPRESSION "done")
  set_tests_properties(arksh_jobs_current_marker  PROPERTIES PASS_REGULAR_EXPRESSION "\\+")
  set_tests_properties(arksh_pipeline_three_stage  PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_pipeline_sigpipe_safe PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_bridge_capture_lines  PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_bridge_multiword      PROPERTIES PASS_REGULAR_EXPRESSION "3")
  # E4-S4 tests
  set_tests_properties(arksh_capture_text_first    PROPERTIES PASS_REGULAR_EXPRESSION "hello"
                                                             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(arksh_capture_text_count    PROPERTIES PASS_REGULAR_EXPRESSION "2"
                                                             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  if(NOT WIN32)
    set_tests_properties(
      arksh_fd_redirect_output_dup
      arksh_fd_redirect_input_dup
      PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
  else()
    set_tests_properties(
      arksh_fd_redirect_output_dup
      arksh_fd_redirect_input_dup
      PROPERTIES
        DISABLED TRUE
    )
  endif()

  # E6-S1: namespace resolvers
  add_test(NAME arksh_ns_fs_cwd       COMMAND arksh -c "fs() -> cwd")
  add_test(NAME arksh_ns_fs_home      COMMAND arksh -c "fs() -> home")
  add_test(NAME arksh_ns_fs_temp      COMMAND arksh -c "fs() -> temp")
  add_test(NAME arksh_ns_fs_separator COMMAND arksh -c "fs() -> separator")
  add_test(NAME arksh_ns_user_name    COMMAND arksh -c "user() -> name")
  add_test(NAME arksh_ns_user_home    COMMAND arksh -c "user() -> home")
  add_test(NAME arksh_ns_sys_os       COMMAND arksh -c "sys() -> os")
  add_test(NAME arksh_ns_sys_arch     COMMAND arksh -c "sys() -> arch")
  add_test(NAME arksh_ns_sys_cpus     COMMAND arksh -c "sys() -> cpu_count")
  add_test(NAME arksh_ns_time_iso     COMMAND arksh -c "time() -> iso")
  add_test(NAME arksh_ns_time_year    COMMAND arksh -c "time() -> year")
  add_test(NAME arksh_ns_time_epoch   COMMAND arksh -c "time() -> epoch")
  set_tests_properties(
    arksh_ns_fs_cwd arksh_ns_fs_home arksh_ns_fs_temp arksh_ns_fs_separator
    arksh_ns_user_name arksh_ns_user_home
    arksh_ns_sys_os arksh_ns_sys_arch arksh_ns_sys_cpus
    arksh_ns_time_iso arksh_ns_time_year arksh_ns_time_epoch
    PROPERTIES PASS_REGULAR_EXPRESSION ".+" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  if(NOT WIN32)
    add_test(NAME arksh_ns_user_uid COMMAND arksh -c "user() -> uid")
    add_test(NAME arksh_ns_user_gid COMMAND arksh -c "user() -> gid")
    set_tests_properties(arksh_ns_user_uid arksh_ns_user_gid
      PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
  endif()

  # E6-S2-T3: point plugin custom type tests
  add_test(NAME arksh_point_type
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; point(3, 4) -> type")
  add_test(NAME arksh_point_x
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; point(3, 4) -> x")
  add_test(NAME arksh_point_y
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; point(3, 4) -> y")
  add_test(NAME arksh_point_distance
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; point(3, 4) -> distance()")
  add_test(NAME arksh_point_translate
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; point(1, 2) -> translate(2, 3) -> type")
  add_test(NAME arksh_point_stage
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_point_plugin> ; text(\"6,8\") |> as_point")
  set_tests_properties(
    arksh_point_type arksh_point_x arksh_point_y arksh_point_distance
    arksh_point_translate arksh_point_stage
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(arksh_point_type      PROPERTIES PASS_REGULAR_EXPRESSION "Point")
  set_tests_properties(arksh_point_x         PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_point_y         PROPERTIES PASS_REGULAR_EXPRESSION "4")
  set_tests_properties(arksh_point_distance  PROPERTIES PASS_REGULAR_EXPRESSION "5")
  set_tests_properties(arksh_point_translate PROPERTIES PASS_REGULAR_EXPRESSION "Point")
  set_tests_properties(arksh_point_stage     PROPERTIES PASS_REGULAR_EXPRESSION "Point")

  # E6-S9: trash plugin tests
  add_test(NAME arksh_trash_plugin_load
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_trash_plugin> ; trash() -> type"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_trash_ns_count
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_trash_plugin> ; let t = trash() ; let n = t -> count ; n -> print()"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_trash_method_exists
    COMMAND arksh -c "plugin load $<TARGET_FILE_NAME:arksh_trash_plugin> ; help methods object"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_trash_plugin_load arksh_trash_ns_count arksh_trash_method_exists
    PROPERTIES TIMEOUT 10
  )
  set_tests_properties(arksh_trash_plugin_load    PROPERTIES PASS_REGULAR_EXPRESSION "trash_ns")
  set_tests_properties(arksh_trash_ns_count       PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")
  set_tests_properties(arksh_trash_method_exists  PROPERTIES PASS_REGULAR_EXPRESSION "trash")

  # E6-S3: pipeline object-aware stages
  add_test(NAME arksh_map_block
    COMMAND arksh -c "list(1, 2, 3) |> map([:it | it]) |> count()")
  add_test(NAME arksh_filter_alias
    COMMAND arksh -c ". -> children() |> filter(type == \"file\") |> count()")
  add_test(NAME arksh_flat_map_block
    COMMAND arksh -c "list(1, 2) |> flat_map([:it | list(it, it)]) |> count()")
  add_test(NAME arksh_group_by_property
    COMMAND arksh -c ". -> children() |> group_by(type) |> count()")
  add_test(NAME arksh_group_by_block
    COMMAND arksh -c "list(\"a\", \"b\", \"a\") |> group_by([:it | it]) |> count()")
  add_test(NAME arksh_sum_numbers
    COMMAND arksh -c "list(1, 2, 3) |> sum()")
  add_test(NAME arksh_min_numbers
    COMMAND arksh -c "list(3, 1, 4, 1, 5) |> min()")
  add_test(NAME arksh_max_numbers
    COMMAND arksh -c "list(3, 1, 4, 1, 5) |> max()")
  add_test(NAME arksh_sum_property
    COMMAND arksh -c ". -> children() |> where(type == \"file\") |> sum(size)")
  set_tests_properties(
    arksh_map_block arksh_filter_alias arksh_flat_map_block
    arksh_group_by_property arksh_group_by_block
    arksh_sum_numbers arksh_min_numbers arksh_max_numbers arksh_sum_property
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_pipeline_sort_numbers arksh_pipeline_sort_property
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(arksh_pipeline_sort_numbers PROPERTIES PASS_REGULAR_EXPRESSION "20")
  set_tests_properties(arksh_map_block       PROPERTIES PASS_REGULAR_EXPRESSION "3")
  set_tests_properties(arksh_filter_alias    PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")
  set_tests_properties(arksh_flat_map_block  PROPERTIES PASS_REGULAR_EXPRESSION "4")
  set_tests_properties(arksh_group_by_property PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")
  set_tests_properties(arksh_group_by_block  PROPERTIES PASS_REGULAR_EXPRESSION "2")
  set_tests_properties(arksh_sum_numbers     PROPERTIES PASS_REGULAR_EXPRESSION "6")
  set_tests_properties(arksh_min_numbers     PROPERTIES PASS_REGULAR_EXPRESSION "1")
  set_tests_properties(arksh_max_numbers     PROPERTIES PASS_REGULAR_EXPRESSION "5")
  set_tests_properties(arksh_sum_property    PROPERTIES PASS_REGULAR_EXPRESSION "[0-9]+")

  # E1-S6: POSIX compatibility tests
  add_test(NAME arksh_trap_posix_all
    COMMAND arksh -c "trap 'echo caught' TERM; trap -p"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_trap_err
    COMMAND arksh -c "trap 'echo err' ERR; /usr/bin/false; echo after"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_set_errexit
    COMMAND arksh -c "set -e; /usr/bin/false; echo after"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_set_nounset
    COMMAND arksh -c "set -u; echo \${UNSET_VAR_12345:-ok}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_printf_basic
    COMMAND arksh -c "printf '%s %d' hello 42"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_param_subst_single
    COMMAND arksh -c "set x hello; echo \${x/ell/ELL}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_param_subst_global
    COMMAND arksh -c "set x aabbcc; echo \${x//b/X}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_test_builtin_string
    COMMAND arksh -c "test -n hello && echo yes"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_test_bracket_numeric
    COMMAND arksh -c "if [ 5 -gt 3 ]; then echo yes; fi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_double_bracket_glob
    COMMAND arksh -c "set s demo.txt ; if [[ \"$s\" == *.txt ]]; then echo yes; fi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_double_bracket_regex
    COMMAND arksh -c "set s 12345 ; if [[ \"$s\" =~ ^[0-9]+$ ]]; then echo yes; fi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_double_bracket_logic
    COMMAND arksh -c "set s demo.txt ; set n 42 ; if [[ \"$s\" == *.txt && \"$n\" =~ ^[0-9]+$ ]]; then echo yes; fi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_double_bracket_unquoted_var
    COMMAND arksh -c "set s demo.txt ; if [[ $s == *.txt ]]; then echo yes; fi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_cmd_subst_file
    COMMAND arksh -c "echo hello > /tmp/arksh_cs_test.txt; set contents \$(< /tmp/arksh_cs_test.txt); echo \$contents"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_trap_posix_all arksh_trap_err arksh_set_errexit arksh_set_nounset
    arksh_printf_basic arksh_param_subst_single arksh_param_subst_global
    arksh_test_builtin_string arksh_test_bracket_numeric
    arksh_double_bracket_glob arksh_double_bracket_regex
    arksh_double_bracket_logic arksh_double_bracket_unquoted_var
    arksh_cmd_subst_file
    PROPERTIES TIMEOUT 10
  )
  set_tests_properties(arksh_trap_posix_all    PROPERTIES PASS_REGULAR_EXPRESSION "TERM")
  set_tests_properties(arksh_trap_err          PROPERTIES PASS_REGULAR_EXPRESSION "err")
  set_tests_properties(arksh_set_errexit       PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(arksh_set_nounset       PROPERTIES PASS_REGULAR_EXPRESSION "ok")
  set_tests_properties(arksh_printf_basic      PROPERTIES PASS_REGULAR_EXPRESSION "hello 42")
  set_tests_properties(arksh_param_subst_single PROPERTIES PASS_REGULAR_EXPRESSION "hELLo")
  set_tests_properties(arksh_param_subst_global PROPERTIES PASS_REGULAR_EXPRESSION "aaXXcc")
  set_tests_properties(arksh_test_builtin_string PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_test_bracket_numeric PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_double_bracket_glob PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_double_bracket_regex PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_double_bracket_logic PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_double_bracket_unquoted_var PROPERTIES PASS_REGULAR_EXPRESSION "yes")
  set_tests_properties(arksh_cmd_subst_file    PROPERTIES PASS_REGULAR_EXPRESSION "hello")

  # E1-S7: POSIX script syntax compatibility
  add_test(NAME arksh_posix_assign_single
    COMMAND arksh -c "X=hello; echo \$X"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_posix_assign_multi
    COMMAND arksh -c "A=foo B=bar; echo \$A \$B"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_posix_assign_expand
    COMMAND arksh -c "BASE=hel; X=\${BASE}lo; echo \$X"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_posix_assign_env_prefix
    COMMAND arksh -c "GREETING=hi env"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_posix_fn_basic
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/posix-basic.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_posix_fn_positional
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/functions/posix-positional.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_posix_assign_single arksh_posix_assign_multi arksh_posix_assign_expand
    arksh_posix_assign_env_prefix arksh_posix_fn_basic arksh_posix_fn_positional
    PROPERTIES TIMEOUT 10
  )
  set_tests_properties(arksh_posix_assign_single    PROPERTIES PASS_REGULAR_EXPRESSION "hello")
  set_tests_properties(arksh_posix_assign_multi     PROPERTIES PASS_REGULAR_EXPRESSION "foo bar")
  set_tests_properties(arksh_posix_assign_expand    PROPERTIES PASS_REGULAR_EXPRESSION "hello")
  set_tests_properties(arksh_posix_assign_env_prefix PROPERTIES PASS_REGULAR_EXPRESSION "GREETING=hi")
  set_tests_properties(arksh_posix_fn_basic         PROPERTIES PASS_REGULAR_EXPRESSION "hello")
  set_tests_properties(arksh_posix_fn_positional    PROPERTIES PASS_REGULAR_EXPRESSION "foo bar")

  # E8-S1-T1: lexer unit tests
  add_test(NAME arksh_unit_lexer COMMAND arksh_unit_lexer)
  set_tests_properties(arksh_unit_lexer PROPERTIES
    PASS_REGULAR_EXPRESSION "all tests passed"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )

  # E8-S1-T2: parser unit tests
  add_test(NAME arksh_unit_parser COMMAND arksh_unit_parser)
  set_tests_properties(arksh_unit_parser PROPERTIES
    PASS_REGULAR_EXPRESSION "all tests passed"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )

  # E8-S1-T3: executor unit tests
  add_test(NAME arksh_unit_executor COMMAND arksh_unit_executor)
  set_tests_properties(arksh_unit_executor PROPERTIES
    PASS_REGULAR_EXPRESSION "all tests passed"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 30
  )

  # E8-S1-T4: object model unit tests
  add_test(NAME arksh_unit_object COMMAND arksh_unit_object)
  add_test(NAME arksh_unit_arena COMMAND arksh_unit_arena)
  set_tests_properties(arksh_unit_object PROPERTIES
    PASS_REGULAR_EXPRESSION "all tests passed"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 10
  )

  # ── E8-S2-T1: golden tests — fixture scripts with deterministic multi-step output ──

  add_test(NAME arksh_golden_pipeline_chain
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/pipeline-chain.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_reduce_block
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/reduce-and-block.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_string_transform
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/string-transform.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_if_sequence
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/if-sequence.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_mixed_shell
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/mixed-shell-and-objects.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_numeric_types
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/numeric-types.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_dict_types
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/dict-types.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_base64_stages
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/base64-stages.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_matrix_types
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/matrix-types.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_json_edge_cases
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/json-edge-cases.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  add_test(NAME arksh_golden_json_query_transform
    COMMAND arksh -c "source ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden/json-query-transform.arksh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    arksh_golden_pipeline_chain
    arksh_golden_reduce_block
    arksh_golden_string_transform
    arksh_golden_if_sequence
    arksh_golden_mixed_shell
    arksh_golden_numeric_types
    arksh_golden_dict_types
    arksh_golden_base64_stages
    arksh_golden_matrix_types
    arksh_golden_json_edge_cases
    arksh_golden_json_query_transform
    PROPERTIES TIMEOUT 15
  )
  # Each test checks a distinctive output string that only appears when the script ran fully.
  set_tests_properties(arksh_golden_pipeline_chain   PROPERTIES PASS_REGULAR_EXPRESSION "13")
  set_tests_properties(arksh_golden_reduce_block     PROPERTIES PASS_REGULAR_EXPRESSION "\\[10,20,30\\]")
  set_tests_properties(arksh_golden_string_transform PROPERTIES PASS_REGULAR_EXPRESSION "alpha beta gamma")
  set_tests_properties(arksh_golden_if_sequence      PROPERTIES PASS_REGULAR_EXPRESSION "step_three")
  set_tests_properties(arksh_golden_mixed_shell      PROPERTIES PASS_REGULAR_EXPRESSION "done")
  set_tests_properties(arksh_golden_numeric_types    PROPERTIES PASS_REGULAR_EXPRESSION "3i")
  set_tests_properties(arksh_golden_dict_types       PROPERTIES PASS_REGULAR_EXPRESSION "\\{\"nome\":\"alice\"\\}")
  set_tests_properties(arksh_golden_base64_stages    PROPERTIES PASS_REGULAR_EXPRESSION "aGVsbG8=")
  # E6-S8: Matrix — checks to_json output and transpose row/col count
  set_tests_properties(arksh_golden_matrix_types     PROPERTIES PASS_REGULAR_EXPRESSION "\"score\": 95")
  # E7-S1: JSON edge cases — unicode café round-trip and matrix JSON output
  set_tests_properties(arksh_golden_json_edge_cases  PROPERTIES PASS_REGULAR_EXPRESSION "caf")
  # E7-S3: nested get_path/set_path, pluck and file round-trip
  set_tests_properties(arksh_golden_json_query_transform PROPERTIES PASS_REGULAR_EXPRESSION "e7s3_roundtrip_ok")

  # plugin autoload tests
  file(MAKE_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_set"
    "${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_unset"
    "${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_dup"
  )
  add_test(NAME arksh_plugin_autoload_set
    COMMAND arksh -c "plugin autoload unset /tmp/arksh_test_pal.dylib ; plugin autoload set /tmp/arksh_test_pal.dylib"
  )
  add_test(NAME arksh_plugin_autoload_unset
    COMMAND arksh -c "plugin autoload unset /tmp/arksh_test_pau.dylib ; plugin autoload set /tmp/arksh_test_pau.dylib ; plugin autoload unset /tmp/arksh_test_pau.dylib"
  )
  add_test(NAME arksh_plugin_autoload_duplicate
    COMMAND arksh -c "plugin autoload unset /tmp/arksh_test_dup.dylib ; plugin autoload set /tmp/arksh_test_dup.dylib ; plugin autoload set /tmp/arksh_test_dup.dylib"
  )
  set_tests_properties(
    arksh_plugin_autoload_set arksh_plugin_autoload_unset arksh_plugin_autoload_duplicate
    PROPERTIES TIMEOUT 10
  )
  set_tests_properties(arksh_plugin_autoload_set PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_set"
  )
  set_tests_properties(arksh_plugin_autoload_unset PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_unset"
  )
  set_tests_properties(arksh_plugin_autoload_duplicate PROPERTIES
    ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/tests/autoload_home_dup"
  )
  set_tests_properties(arksh_plugin_autoload_set       PROPERTIES PASS_REGULAR_EXPRESSION "added to autoload")
  set_tests_properties(arksh_plugin_autoload_unset     PROPERTIES PASS_REGULAR_EXPRESSION "removed from autoload")
  set_tests_properties(arksh_plugin_autoload_duplicate PROPERTIES PASS_REGULAR_EXPRESSION "already configured for autoload")

  # ── E8-S2-T2: PTY REPL tests — interactive features via pseudo-terminal ──

  if(NOT WIN32)
    add_executable(arksh_pty_repl tests/pty_repl.c)
    if(APPLE)
      # forkpty is in libSystem on macOS; no extra library needed.
    else()
      target_link_libraries(arksh_pty_repl PRIVATE util)
    endif()

    add_test(NAME arksh_pty_repl COMMAND arksh_pty_repl)
    set_tests_properties(arksh_pty_repl PROPERTIES
      PASS_REGULAR_EXPRESSION "all tests passed"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      TIMEOUT 20
    )
  endif()

  # ── E8-S2-T3: job control smoke tests — repeatable, no timing dependencies ──

  # Two background jobs started concurrently; jobs lists both entries.
  add_test(NAME arksh_jobctrl_two_jobs
    COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & \"${ARKSH_TEST_PAUSE}\" 0 & jobs"
  )
  set_tests_properties(arksh_jobctrl_two_jobs PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[1\\]"
    TIMEOUT 10
  )

  # wait without arguments blocks until all background jobs complete.
  add_test(NAME arksh_jobctrl_wait_all
    COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & wait ; echo wait_all_done"
  )
  set_tests_properties(arksh_jobctrl_wait_all PROPERTIES
    PASS_REGULAR_EXPRESSION "wait_all_done"
    TIMEOUT 10
  )

  # Background job completes; done message appears, then a subsequent command runs.
  add_test(NAME arksh_jobctrl_done_then_cmd
    COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & wait %1 ; echo after_wait"
  )
  set_tests_properties(arksh_jobctrl_done_then_cmd PROPERTIES
    PASS_REGULAR_EXPRESSION "done;after_wait"
    TIMEOUT 10
  )

  # Jobs command shows current-job marker '+' when a job is running.
  add_test(NAME arksh_jobctrl_current_marker2
    COMMAND arksh -c "\"${ARKSH_TEST_PAUSE}\" 0 & \"${ARKSH_TEST_PAUSE}\" 0 & jobs"
  )
  set_tests_properties(arksh_jobctrl_current_marker2 PROPERTIES
    PASS_REGULAR_EXPRESSION "\\+"
    TIMEOUT 10
  )

  # ────────────────────────────────────────────────────────────────────────────

  # E6-S4: introspection and typed help
  add_test(NAME arksh_help_stages
    COMMAND arksh -c "help stages")
  set_tests_properties(arksh_help_stages PROPERTIES
    PASS_REGULAR_EXPRESSION "where.*filter"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(NAME arksh_help_resolvers
    COMMAND arksh -c "help resolvers")
  set_tests_properties(arksh_help_resolvers PROPERTIES
    PASS_REGULAR_EXPRESSION "env.*environment"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(NAME arksh_help_named_lookup
    COMMAND arksh -c "help where")
  set_tests_properties(arksh_help_named_lookup PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[stage\\]"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
endif()
