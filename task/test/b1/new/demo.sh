#!/bin/bash
#for ((i=0;i<$4;i++))
#make clean
cd ./src
for ((i=1;i<$1;i++))
do
{
		
	cp ./blackscholes.c $i.c
	let period=$i*50
	let deadline=$i*100
	let cost=$i*10
	let budget=$i*50
	make period=$period deadline=$deadline cost=$cost budget=$budget a=$i 
#	make -f ./src/Makefile period=$1 deadline=$2 cost=$3 a=$i 
#	echo $i
#	sudo ./$i 1 ../input/in_test.txt ../output/out_test.txt
	rm $i.*
	sleep 1
}  
done
#for ((i=0;i<$1;i++))

for ((i=1;i<$1;i++))
do
{
	mv $i ../
	cd ../
      #  sudo ./$i 1 ../input/in_small.txt ../output/out_s.txt &
	sudo ./$i 1 ~/parsec-rt/blackscholes-rt/input/in_small.txt ~/parsec-rt/blackscholes-rt/output/out_s.txt &
#	sudo release_ts
#	sleep 1
}   
done
#wait
#sudo release_ts
wait

 

