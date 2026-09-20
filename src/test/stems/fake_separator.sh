#!/bin/sh
# A separator for the stem conversion test. It needs no model.
# Usage: fake_separator.sh <mode> <model> <output dir> <input file> [stem dir]
# Modes: ok copies the input file four times, from copies the four files of
# the stem directory, fail stops with an error, hang waits.
set -e

mode="$1"
model="$2"
output_dir="$3"
input="$4"
stem_dir="$5"

if [ "$mode" = "fail" ]; then
    echo "the model $model did not load" >&2
    exit 3
fi

if [ "$mode" = "hang" ]; then
    sleep 60
    exit 0
fi

mkdir -p "$output_dir/$model"
for stem in drums bass other vocals; do
    if [ "$mode" = "from" ]; then
        cp "$stem_dir/$stem.wav" "$output_dir/$model/$stem.wav"
    else
        cp "$input" "$output_dir/$model/$stem.wav"
    fi
done
echo "done 100%" >&2
