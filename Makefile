# to run this, type $make 
# the input should be processed with input/output redirection:
#			./email_filter < input.txt
#			./calendar_filter < ${output_from_email_filter}.txt
#			./location_updater < input.txt
# These are not recommended:
#		./email_filter input.txt
#		fopen("input.txt","r"); // in ${email_filter calendar_filter location_updater}.c 
# BTW: fwrite is totally fine as we need it to generate output.txt file.

all:	transfProg test_op

#-g lets us use gdb
transfProg: transfProg.c
	gcc -g -pthread -o transfProg transfProg.c
test_op: test_op.c
	gcc -o test_op test_op.c
clean:
	rm -rf transfProg test_op
