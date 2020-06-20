#!/bin/bash

for f in inputs/*.txt
do
    outfile=$(basename $f)
    outfile=$outfile.out
    echo $f ">" "outputs/$outfile"
    cat $f | ./aencode | ./adecode > outputs/$outfile
    diff $f outputs/$outfile
    if [ $? -eq 0 ]; then
        rm outputs/$outfile
    fi
done

while [ 0 -le 1 ]
do
    echo "Testing C..."
    tr -dc a-zA-Z0-9 < /dev/urandom | head -c256 > inputs/c.txt
    cat inputs/c.txt | ./aencode | ./adecode > outputs/c.txt.out
    diff inputs/c.txt outputs/c.txt.out
    if [ $? -eq 0 ]; then
        rm outputs/$outfile
    else
        exit 1
    fi
    sleep 1
done
