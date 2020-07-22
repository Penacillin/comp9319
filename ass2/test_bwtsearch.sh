#!/bin/bash

REFERENCE_SOLUTION=./bwtsearch
MY_SOL=./mysol/bwtsearch

for file in ./data/*.bwt
do
    file_basename=$(basename -- $file)
    echo $file
    ./$REFERENCE_SOLUTION $file < ./inputs/dna-tiny.input > ./outputs/$file_basename.output.ref
    ./$MY_SOL $file < ./inputs/dna-tiny.input > ./outputs/$file_basename.output
    diff ./outputs/$file_basename.output.ref ./outputs/$file_basename.output | wc
done
