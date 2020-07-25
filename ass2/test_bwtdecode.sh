#!/bin/bash

REFERENCE_SOLUTION=./bwtdecode
MY_SOL=./mysol/bwtdecode

for file in ./data/dna-tiny.bwt ./data/dna-*KB.bwt ./data/dna-small.bwt ./data/dna-medium.bwt ./data/dna-1MB.bwt ./data/dna-2MB.bwt ./data/dna-5MB.bwt ./data/dna-15MB.bwt
do
    file_basename=$(basename -- $file)
    echo $file
    time ./$REFERENCE_SOLUTION $file ./outputs/$file_basename.out.ref
    time ./$MY_SOL $file ./outputs/$file_basename.out
    res=$(diff ./outputs/$file_basename.out.ref ./outputs/$file_basename.out)
    if [[ $res != "" ]]; then
        echo "MISMAATCH"
    fi
done
