#!/bin/sh

# generates a core file and gdb commands to add symbol files from a provided minidump file
# usage: $ ./generate_core.sh <minidump file>

set -e

# check argument count
if [ $# -ne 1 ]; then
    echo "invalid parameter count"
    echo "usage: ./generate_core.sh <minidump file>"
    exit 1
fi

# check if minidump-2-core is available
if ! command -v minidump-2-core >/dev/null 2>&1; then
    echo "minidump-2-core could not be found"
    exit 1
fi

# check if perl is available
if ! command -v perl >/dev/null 2>&1; then
    echo "perl could not be found"
    exit 1
fi

DUMP="$1"

CORE_FILE="minidump.core"
OUTPUT_FILE="$CORE_FILE.out"
GDB_FILE="minidump.gdb"
EXECUTABLE="ModOrganizer"

minidump-2-core -v "$DUMP" > $CORE_FILE 2> $OUTPUT_FILE

# open output file and get all symbol files
# convert e.g.
# 0x7fcef1023000-0x7fcef119f000, ChkSum: 0x00000000, GUID: 7EFAC761-FB86-46DD-0C43-BF1C48B430B8,  "/usr/lib64/libc.so.6"
# to
# /usr/lib64/libc.so.6
FILES=$(grep -w GUID "$OUTPUT_FILE" | grep / | sed -e 's/.*GUID: .*,  //' | sed -e 's/\"//' | sed -e 's/\"//')

# source: https://stackoverflow.com/a/20460402
stringContain() { case $2 in *$1* ) return 0;; *) return 1;; esac ;}

# iterate over symbol files
for file in $FILES; do
    # only use .so files and ModOrganizer executable
    if stringContain ".so" "$file" || stringContain "$EXECUTABLE" "$file"; then
        # get base address
        t1=$(grep -w GUID "$OUTPUT_FILE" | grep "$file" | sed -e 's/-.*//')
        # get address of .text section
        t2=$(objdump -x "$file" | grep '\.text' | head -n 1 | tr -s ' ' | cut -d' ' -f 5)
        # calculate address to use with gdb
        addr=$(perl -e 'die unless $ARGV[0] && $ARGV[1]; printf("%#x\n", hex($ARGV[0]) + hex($ARGV[1]))' "$t1" "$t2")
        # add gdb command
        echo "add-symbol-file $file $addr" >> $GDB_FILE
    fi
done