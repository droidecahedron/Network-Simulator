# Network-Simulator
c based network simulator in linux

Johnny Nguyen

topo1.txt is the custom topology file with transfers you want

if youre not used to running c executables in linux, run sudo make before you try to execute the program to use the compiler to generate relevant files and make it... well. run.

transfProg is the program for the assignment
It receives the file name from the cmd arg, not redirect
so it must be used as 
./transfProg input.txt 10 0.3
where 10 is the thread num, and input.txt is the file, and 0.3 can be any value between 0 and 1 to vary the loss of the generated links.
It outputs to stdio. 


If redirected to a text file via 
./transfProg input.txt 10 > output.txt

then test_op program can be used as such
./test_op output.txt comparison_output.txt


