#! /bin/bash

for file in $(ls trace*)
do
    ./sdriver.pl -t $file -s ./tshref > tshref.txt
    ./sdriver.pl -t $file -s ./tsh > tsh.txt

    echo $file " :"
    diff tsh.txt tshref.txt
    echo -e "-------------------------------------\n"
    rm tshref.txt tsh.txt
done 
