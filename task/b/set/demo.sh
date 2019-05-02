#!/bin/bash
for ((i=1;i<10;i++))
do
{
	cp ./src/blackscholes.c $i.c
}
done
	make -f ./src/Makefile period=100 deadline=100 cost=10 a=1 
	make -f ./src/Makefile period=50 deadline=100 cost=10 a=2
	make -f ./src/Makefile period=200 deadline=200 cost=20 a=3
	make -f ./src/Makefile period=150 deadline=200 cost=10 a=4
	make -f ./src/Makefile period=70 deadline=300 cost=5 a=5
	make -f ./src/Makefile period=180 deadline=200 cost=30 a=6
	make -f ./src/Makefile period=300 deadline=300 cost=30 a=7
	make -f ./src/Makefile period=250 deadline=500 cost=20 a=8
	make -f ./src/Makefile period=210 deadline=250 cost=10 a=9
	
for ((i=1;i<10;i++))
do
{
         rm $i.*

}
done
for ((i=1;i<10;i++))
do
{

#        sudo ./$i 1 ../input/in_dev.txt ../output/out_dev.txt &
        sudo ./$i 1 ../input/in_small.txt ../output/out_s.txt &
#	sleep 1
}   
done
#wait
#sudo release_ts
wait

 

