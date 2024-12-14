[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/JzJROed1)
Refer 'Assignment 5 Readme.pdf' for instructions


to test run
make clean
make 
start the jbod_server in another terminal with ./jbod_server -v -p 3330
and in another terminal run something like the following 
./tester -w traces/random-input -s 1024 >x  


you can compare the diff like diff x traces/random-expected-output
