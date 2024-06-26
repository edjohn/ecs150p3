#!/bin/bash

set -o pipefail
#set -xv # debug

#
# Logging helpers
#
log() {
    echo -e "${*}"
}

inf() {
    log "Info: ${*}"
}
warning() {
    log "Warning: ${*}"
}
error() {
    log "Error: ${*}"
}
die() {
    error "${*}"
    exit 1
}

#
# Scoring helpers
#
select_line() {
    # 1: string
    # 2: line to select
    echo $(echo "${1}" | sed "${2}q;d")
}

fail() {
    # 1: got
    # 2: expected
    log "Fail: got '${1}' but expected '${2}'"
}

pass() {
    # got
    log "Pass: ${1}"
}

compare_lines() {
    # 1: output
    # 2: expected
    # 3: score (output)
    declare -a output_lines=("${!1}")
    declare -a expect_lines=("${!2}")
    local __score=$3
    local partial="0"

    # Amount of partial credit for each correct output line
    local step=$(bc -l <<< "1.0 / ${#expect_lines[@]}")

    # Compare lines, two by two
    for i in ${!output_lines[*]}; do
        if [[ "${output_lines[${i}]}" =~ "${expect_lines[${i}]}" ]]; then
            pass "${output_lines[${i}]}"
            partial=$(bc <<< "${partial} + ${step}")
        else
            fail "${output_lines[${i}]}" "${expect_lines[${i}]}" ]]
        fi
    done

    # Return final score
    eval ${__score}="'${partial}'"
}

#
# Generic function for running FS tests
#
run_test() {
    # These are global variables after the test has run so clear them out now
    unset STDOUT STDERR RET

    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    RET="${?}"
    STDOUT=$(cat "${outfile}")
    STDERR=$(cat "${errfile}")

    # Deal with the possible timeout errors
    [[ ${RET} -eq 127 ]] && warning "Something is wrong (the executable probably doesn't exists)"
    [[ ${RET} -eq 124 ]] && warning "Command timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Generic function for capturing output of non-test programs
#
run_tool() {
    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    local ret="${?}"
    local stdout=$(cat "${outfile}")
    local stderr=$(cat "${errfile}")

    # Log the output
    [[ ! -z ${stdout} ]] && inf "${stdout}"
    [[ ! -z ${stderr} ]] && inf "${stderr}"

    # Deal with the possible timeout errors
    [[ ${ret} -eq 127 ]] && warning "Tool execution failed..."
    [[ ${ret} -eq 124 ]] && warning "Tool execution timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Phase 1
#

# Info on empty disk
info() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 100
    run_test ./test_fs.x info test.fs
    rm -f test.fs

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "1")")
    line_array+=("$(select_line "${STDOUT}" "2")")
    line_array+=("$(select_line "${STDOUT}" "3")")
    line_array+=("$(select_line "${STDOUT}" "4")")
    line_array+=("$(select_line "${STDOUT}" "5")")
    line_array+=("$(select_line "${STDOUT}" "6")")
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")
    local corr_array=()
    corr_array+=("FS Info:")
    corr_array+=("total_blk_count=103")
    corr_array+=("fat_blk_count=1")
    corr_array+=("rdir_blk=2")
    corr_array+=("data_blk=3")
    corr_array+=("data_blk_count=100")
    corr_array+=("fat_free_ratio=99/100")
    corr_array+=("rdir_free_ratio=128/128")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}




# Info with files
info_full() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 100
    run_tool dd if=/dev/urandom of=test-file-1 bs=2048 count=1
    run_tool dd if=/dev/urandom of=test-file-2 bs=2048 count=2
    run_tool dd if=/dev/urandom of=test-file-3 bs=2048 count=4
    run_tool ./fs_ref.x add test.fs test-file-1
    run_tool ./fs_ref.x add test.fs test-file-2
    run_tool ./fs_ref.x add test.fs test-file-3

    run_test ./test_fs.x info test.fs
    rm -f test-file-1 test-file-2 test-file-3 test.fs

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")
    local corr_array=()
    corr_array+=("fat_free_ratio=95/100")
    corr_array+=("rdir_free_ratio=125/128")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

#
# Phase 2
#

# make fs with fs_make.x, add empty file with test_fs.x, ls with fs_ref.x
create_simple() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 10
    run_tool touch test-file-1
    run_tool timeout 2 ./test_fs.x add test.fs test-file-1
    run_test ./fs_ref.x ls test.fs
    rm -f test.fs test-file-1

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "2")")
    local corr_array=()
    corr_array+=("file: test-file-1, size: 0, data_blk: 65535")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# create, delete, and ls
create_delete_ls() {
    log "\n--- Running ${FUNCNAME} ---"

    # Create a filesystem
    run_tool ./fs_make.x test.fs 10

    # Test create
    run_test ./test_fs.x create test.fs test-file-1
    # Check if file was created
    run_test ./test_fs.x ls test.fs
    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "2")") # Assuming "file: test-file-1" is in the second line
    local corr_array=()
    corr_array+=("file: test-file-1, size: 0, data_blk: 65535")
    local score
    compare_lines line_array[@] corr_array[@] score
    log "Create Score: ${score}"

    # Test delete
    run_test ./test_fs.x delete test.fs test-file-1
    # Check if file was deleted
    run_test ./test_fs.x ls test.fs
    line_array=()
    line_array+=("$(select_line "${STDOUT}" "2")") # Assuming the second line contains the next file after deletion
    corr_array=()
    corr_array+=("file: <next-file>, size: <size>, data_blk: <data-block>") # Update placeholders with the actual next file info
    compare_lines line_array[@] corr_array[@] score
    log "Delete Score: ${score}"

    # Clean up
    rm -f test.fs

    log "Create, Delete, and LS tests completed."
}

# create, delete, and ls
create_delete_ls() {
    log "\n--- Running ${FUNCNAME} ---"

    # Create a filesystem
    run_tool ./fs_make.x test.fs 10

    # Test create
    run_test ./test_fs.x add test.fs simple_reader.c
    # Check if file was created
    run_test ./test_fs.x ls test.fs
    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "2")") # Assuming "file: test-file-1" is in the second line
    local corr_array=()
    corr_array+=("file: simple_reader.c, size: 0, data_blk: 65535")
    local create_score
    compare_lines line_array[@] corr_array[@] create_score
    log "Create Score: ${create_score}"

    # Test delete
    run_test ./test_fs.x rm test.fs simple_reader.c
    # Check if file was deleted
    run_test ./test_fs.x ls test.fs
    local delete_score
    if [[ "${STDOUT}" == "FS Ls:" ]]; then
        delete_score=1
    else
        delete_score=0
    fi
    log "Delete Score: ${delete_score}"

    # Clean up
    rm -f test.fs

    log "Create, Delete, and LS tests completed."
}



# Phase 3 + 4
#

# read one block
read_block() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(4096): print('a', end='')" > test-file-1
    run_tool ./fs_ref.x add test.fs test-file-1
    cat <<END_SCRIPT > read_block.script
MOUNT
OPEN	test-file-1
READ	4096	FILE	test-file-1
CLOSE
UMOUNT
END_SCRIPT
    run_test ./test_fs.x script test.fs read_block.script

    rm -f test.fs test-file-1 read_block.script

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
    corr_array+=("Read 4096 bytes from file. Compared 4096 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# overwrite block in mid-file
overwrite_block() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(12288): print('a', end='')" > test-file-1
    python3 -c "for i in range(4096): print('b', end='')" > test-file-2

    run_tool ./fs_ref.x add test.fs test-file-1

    local line_array=()
    local corr_array=()

    cat <<END_SCRIPT > overwrite_block.script
MOUNT
OPEN	test-file-1
SEEK	4096
WRITE	FILE	test-file-2
SEEK	4096
READ	4096	FILE	test-file-2
CLOSE
UMOUNT
END_SCRIPT
    run_test ./test_fs.x script test.fs overwrite_block.script

    line_array+=("$(select_line "${STDOUT}" "6")")
    corr_array+=("Read 4096 bytes from file. Compared 4096 correct.")

    run_test ./fs_ref.x info test.fs

    line_array+=("$(select_line "${STDOUT}" "7")")
    corr_array+=("fat_free_ratio=6/10")

    rm -f test.fs test-file-1 test-file-2 overwrite_block.script

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

#
# Run tests
#
run_tests() {
    # Phase 1
    info
    info_full
    # Phase 2
    create_simple
    create_delete_ls
    # Phase 3+4
    read_block
    overwrite_block
}

make_fs() {
    # Compile
    make > /dev/null 2>&1 ||
        die "Compilation failed"

    local execs=("test_fs.x" "fs_make.x" "fs_ref.x")

    # Make sure executables were properly created
    local x
    for x in "${execs[@]}"; do
        if [[ ! -x "${x}" ]]; then
            die "Can't find executable ${x}"
        fi
    done
}

make_fs
run_tests
