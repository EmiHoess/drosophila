#!/bin/bash

## This is a simple script to run Drosophila on all files within a directory
# usage: ./run.sh directory

#DIR= $1

for file in `ls $1/*.MOV`
do
	if [ $# -eq 0 ]
	then
    	echo "No argument supplied!"
		echo "Usage: ./run.sh folder start_w end_w start_h end_h"
	fi
	echo "####################################"
	echo "$file is about to be analyzed"
	echo "./drosophila $file $2 $3 $4 $5 0"
	./drosophila $file $2 $3 $4 $5 0
	echo "---- finished $file ----"
	filename= ${file/?\//}
	echo "saving $filename flying time..."
	echo "cat f_t_${file#*$1/}.txt >> ${1/\//}.txt"



	
done
