# llvm-simple-dse
Simple Dead store elimination code for LLVM. 

Command for using DSE pass and saving resulting .ll:
./bin/opt -load lib/LLVMDeadStoreElimination.so -enable-new-pm=0 -dse-pass fileName.ll -S -o resultingFileName.ll
