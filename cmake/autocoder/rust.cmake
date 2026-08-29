####
# cmake/autocoder/rust.cmake:
#
# fprime-rs Phase 4 autocoder: runs tools/fprime-rs/fpp_to_rust.py over a
# module's FPP models and checks the result against the generated Rust that is
# checked in under Rust/<crate>/src/generated/. A drift fails the build.
#
# Why verify rather than feed cargo directly:
#
#   The Rust workspace is built by a single cargo invocation over the source
#   tree (Rust/enable.cmake). Having per-module autocoder output land in the
#   build tree and feed that one cargo build would create a build-ordering
#   dependency — cargo must not start until every module's generator has run —
#   that CMake cannot express cleanly through the autocoder plugin API, since
#   AUTOCODER_GENERATED_OTHER outputs are tracked but never built for you.
#
#   Checking in the generated code (the pattern fprime-config already uses)
#   keeps the Rust build self-contained and the generated code reviewable in
#   diffs, while this autocoder provides the build-time guarantee that the
#   checked-in copy still matches the model. CI enforces the same invariant.
#
# Opt in with -DFPRIME_RUST_AUTOCODE=ON (default OFF: the generator needs
# fpp-to-json on PATH, which only the fprime venv provides).
####
include_guard()
include(utilities)
include(autocoder/helpers)

autocoder_setup_for_multiple_sources()

# Models that have generated Rust checked in, and where it lives. Extend this
# as more models gain generated types.
set(FPRIME_RUST_GENERATED_MAP
    "Svc/FprimeProtocol/FprimeProtocol.fpp:Rust/fprime-svc/src/generated/fprime_protocol.rs"
)

function(rust_is_supported AC_INPUT_FILE)
    autocoder_support_by_suffix(".fpp" "${AC_INPUT_FILE}" TRUE)
endfunction(rust_is_supported)

function(rust_setup_autocode MODULE_NAME AC_INPUT_FILES)
    set(STAMPS)
    foreach(AC_INPUT IN LISTS AC_INPUT_FILES)
        # Match this input against the models that have generated Rust
        file(RELATIVE_PATH REL_INPUT "${FPRIME_FRAMEWORK_PATH}" "${AC_INPUT}")
        set(CHECKED_IN "")
        foreach(ENTRY IN LISTS FPRIME_RUST_GENERATED_MAP)
            string(REPLACE ":" ";" PARTS "${ENTRY}")
            list(GET PARTS 0 ENTRY_MODEL)
            list(GET PARTS 1 ENTRY_OUTPUT)
            if (REL_INPUT STREQUAL ENTRY_MODEL)
                set(CHECKED_IN "${FPRIME_FRAMEWORK_PATH}/${ENTRY_OUTPUT}")
            endif()
        endforeach()
        if (NOT CHECKED_IN)
            continue()
        endif()

        get_filename_component(BASENAME "${AC_INPUT}" NAME_WE)
        set(STAMP "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.rust-verified")
        add_custom_command(
            OUTPUT "${STAMP}"
            COMMAND "${PYTHON}" "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/fpp_to_rust.py"
                    "--module-name" "${REL_INPUT}"
                    "--out" "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.rs"
                    "${AC_INPUT}"
            COMMAND "${CMAKE_COMMAND}" "-E" "compare_files"
                    "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.rs" "${CHECKED_IN}"
            COMMAND "${CMAKE_COMMAND}" "-E" "touch" "${STAMP}"
            DEPENDS "${AC_INPUT}" "${CHECKED_IN}"
                    "${FPRIME_FRAMEWORK_PATH}/tools/fprime-rs/fpp_to_rust.py"
            COMMENT "[rust] Verifying generated Rust for ${BASENAME} matches the model"
            VERBATIM
        )
        list(APPEND STAMPS "${STAMP}")
    endforeach()

    if (STAMPS)
        # AUTOCODER_GENERATED_OTHER outputs are tracked but never built for us,
        # so drive them with a target the module depends on.
        add_custom_target("${MODULE_NAME}_rust_verify" DEPENDS ${STAMPS})
        add_dependencies("${MODULE_NAME}" "${MODULE_NAME}_rust_verify")
        set(AUTOCODER_GENERATED_OTHER "${STAMPS}" PARENT_SCOPE)
    else()
        # Nothing to verify for this module; report no outputs of any kind by
        # setting the OTHER list empty (the contract requires one of the three
        # generated-file variables to be defined).
        set(AUTOCODER_GENERATED_OTHER "" PARENT_SCOPE)
    endif()
endfunction(rust_setup_autocode)
