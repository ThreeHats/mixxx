#!/bin/sh
# A separator for the stem conversion test. It needs no model.
# Usage: fake_separator.sh <mode> <model> <output dir> <input file>
# Modes: ok writes four stem files, fail stops with an error, hang waits.
set -e

mode="$1"
model="$2"
output_dir="$3"
input="$4"

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
    cp "$input" "$output_dir/$model/$stem.wav"
done
echo "done 100%" >&2
