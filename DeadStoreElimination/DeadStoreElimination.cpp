#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"

#include <set>
#include <map>
#include <vector>

using namespace std;
using namespace llvm;

// Using DFS postorder traversal to determine the reverse execution order of BasicBlocks.
void dfsPostorder(BasicBlock *node, set<BasicBlock *> &visited, vector<BasicBlock *> &order){

    visited.insert(node);

    // Visiting all blocks reachable from the current block by following the control flow.
    for(auto *succ: successors(node)){
        if(visited.count(succ) == 0)
            dfsPostorder(succ, visited, order);
    }

    order.push_back(node);
}

namespace {
  
    struct DeadStoreElimination : public FunctionPass {
        static char ID; 
        DeadStoreElimination() : FunctionPass(ID) {}

        bool runOnFunction(Function &F) override {

            // DFS postorder 
            set<BasicBlock *> visited;
            vector<BasicBlock *> order;
            BasicBlock *entry_node = &F.getEntryBlock();
            dfsPostorder(entry_node, visited, order);

            // loads_in - values that are live at the beginning of each block
            // loads_out - values that are live at the end of each block
            map<BasicBlock *, set<Value*>> loads_in;
            map<BasicBlock *, set<Value*>> loads_out;

            for(BasicBlock *BB: order){
                loads_in[BB] = {};
                loads_out[BB] = {};
            }

            // Because control flow graph may contain loops, a single traversal is not enough
            // to compute correct loads_in and loads_out sets. Information needs to propagate
            // until it reaches stable point (when we don't register any changes in loads_in and loads_out).
            // This phase is analitycal and we will not be marking any instruction for removal in it.
            bool changed = true;

            while(changed){

                changed = false;

                // Going through block in postorder.
                for(BasicBlock *BB: order){

                    // Out state of each block is calculated as union
                    // of IN states of its successor blocks.
                    set<Value*> new_out = {};
                    for(auto *succ: successors(BB)){
                        new_out.insert(loads_in[succ].begin(), loads_in[succ].end());
                    }

                    // Compare the newly computed OUT set with the one from the previous iteration.
                    // If they differ, mark that another iteration is needed and update loads_out.
                    if(new_out != loads_out[BB]){
                        changed = true;
                        loads_out[BB] = new_out;
                    }

                    // In this part we are going through current basic block from the bottom to the top.
                    // At the end of the block the currently live memory locations are stored in loads_out[BB].
                    // We use this information to compute the IN set of the block:
                    //  - If we encounter "load" instruction we add its pointer to the set,
                    //    meaning that this memory location will be needed by later instructions higher in the block.
                    //  - If we encounter "store" instruction and and its target memory location is currently
                    //    considered live (its pointer is present in the set), then we remove pointer from the set.
                    // (In this part we do not mark instruction for deletion since they might become live again through iterations.)
                    // We use temporary set to calculate IN state of current block and 
                    // then compare it with previous value stored in loads_in[BB] set.
                    set<Value*> new_in = loads_out[BB]; 

                    for(auto I = BB->rbegin(); I != BB->rend(); I++){
                        Instruction &Instr = *I;

                        if(auto *LI = dyn_cast<LoadInst>(&Instr)){
                            Value *ptr = LI->getPointerOperand();
                            new_in.insert(ptr);
                        }
                        else if(auto *SI = dyn_cast<StoreInst>(&Instr)){
                            Value *ptr = SI->getPointerOperand();
                            if(new_in.count(ptr)){
                                new_in.erase(ptr);
                            }
                        }
                    }

                    // Compare the newly computed IN set with the one from the previous iteration.
                    // If they differ, mark that another iteration is needed and update the IN set. 
                    if(new_in != loads_in[BB]){
                        changed = true;
                        loads_in[BB] = new_in;
                    }
                }
            }

            // Vector used to collect instructions that should be removed. 
            vector<Instruction*> toErase;

            // In this phase we go once through all blocks using information from previous phase.
            // We traverse each block from bottom to top and mark dead store instructions.
            for(BasicBlock *BB: order){
                for(auto I = BB->rbegin(); I != BB->rend(); I++){
                    Instruction &Instr = *I;

                    if(auto *LI = dyn_cast<LoadInst>(&Instr)){
                        Value *ptr = LI->getPointerOperand();
                        loads_out[BB].insert(ptr);
                    }
                    else if(auto *SI = dyn_cast<StoreInst>(&Instr)){
                        Value *ptr = SI->getPointerOperand();
                        if(loads_out[BB].count(ptr)){
                            loads_out[BB].erase(ptr);
                        }
                        else{
                            toErase.push_back(&Instr);
                        }
                    }
                }
            }
      
            // Safely erase all instructions that were marked as dead stores.
            for(Instruction *Instr: toErase){
                Instr->eraseFromParent();
            }

            return !toErase.empty();
        }
    };
}

char DeadStoreElimination::ID = 0;
static RegisterPass<DeadStoreElimination> X("dse-pass", "Elimination of unused store instructions.");
