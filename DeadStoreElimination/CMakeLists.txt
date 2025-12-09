add_llvm_library( LLVMDeadStoreElimination MODULE 
  DeadStoreElimination.cpp

  PLUGIN_TOOL
  opt
  )