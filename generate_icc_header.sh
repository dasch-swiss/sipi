#!/bin/bash

# Generate ICC profile header files
# Arguments: input_file output_file array_name
# 
# This script detects the xxd version and handles both GNU xxd and Toybox xxd:
# - GNU xxd (Linux): supports output file as second argument
# - Toybox xxd (macOS): requires output redirection

set -e  # Exit on any error

if [ $# -ne 3 ]; then
    echo "Usage: $0 <input_file> <output_file> <array_name>" >&2
    exit 1
fi

input_file="$1"
output_file="$2"
array_name="$3"

# Check if input file exists
if [ ! -f "$input_file" ]; then
    echo "Error: Input file '$input_file' not found" >&2
    exit 1
fi

# Detect xxd version
xxd_version=$(xxd --help 2>&1 | head -1)

if echo "$xxd_version" | grep -q "Toybox"; then
    # Toybox xxd (macOS) - doesn't support output file argument, needs redirection
    echo "unsigned char ${array_name}[] = {" > "${output_file}"
    xxd -i "${input_file}" >> "${output_file}"
    echo "};" >> "${output_file}"
    echo "unsigned int ${array_name}_len = sizeof(${array_name});" >> "${output_file}"
else
    # GNU xxd (Linux) - supports output file as second argument and generates proper C format
    xxd -i "${input_file}" "${output_file}"
fi

echo "Generated ${output_file} with array name ${array_name}"