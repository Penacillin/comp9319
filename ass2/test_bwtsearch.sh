#!/bin/bash

REFERENCE_SOLUTION=./bwtsearch
MY_SOL=./mysol/bwtsearch

for file in ./data/*.bwt
do
    file_basename=$(basename -- $file)
    echo $file
    time ./$REFERENCE_SOLUTION $file < ./inputs/dna-tiny.input > ./outputs/$file_basename.output.ref
    time ./$MY_SOL $file < ./inputs/dna-tiny.input > ./outputs/$file_basename.output
    res=$(diff ./outputs/$file_basename.output.ref ./outputs/$file_basename.output)
    if [[ $res != "" ]]; then
        echo "MISMAATCH"
    fi
done

echo "RANDOM INPUT"
rm -f ./inputs/random.input
for i in 1 5 10 12 20 25 30 40 50 55 60 70 90 100; do
    tr -dc "ACGT" < /dev/urandom | head -c$i >> ./inputs/random.input
    printf "\n" >> ./inputs/random.input
done
time ./$REFERENCE_SOLUTION ./data/dna-100MB.bwt < ./inputs/random.input > ./outputs/random.output.ref
time ./$MY_SOL ./data/dna-100MB.bwt < ./inputs/random.input > ./outputs/random.output
res=$(diff ./outputs/random.output.ref ./outputs/random.output)
if [[ $res != "" ]]; then
    echo "MISMAATCH"
fi
