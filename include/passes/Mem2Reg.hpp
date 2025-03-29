#pragma once

#include "Dominators.hpp"
#include "Instruction.hpp"
#include "Value.hpp"

#include <map>
#include <memory>
#include <stack>

class Mem2Reg : public Pass {
  private:
    Function *func_;
    std::unique_ptr<Dominators> dominators_;
    std::map<Value *, Value *> phi_map;
    // TODO 添加需要的变量

    // 变量定值栈
    std::map<Value *, std::stack<Value *>> var_val_stack;
    // phi指令对应的左值(地址)
    std::map<PhiInst *, Value *> phi_lval;
    std::vector<Instruction *> wait_delete;
    std::vector<Value *> wait_delete_val;

  public:
    Mem2Reg(Module *m) : Pass(m) {}
    ~Mem2Reg() = default;

    void run() override;

    void generate_phi();
    void rename(BasicBlock *bb);
    void erase();

    static inline bool is_global_variable(Value *l_val) {
        return dynamic_cast<GlobalVariable *>(l_val) != nullptr;
    }
    static inline bool is_gep_instr(Value *l_val) {
        return dynamic_cast<GetElementPtrInst *>(l_val) != nullptr;
    }

    static inline bool is_valid_ptr(Value *l_val) {
        return not is_global_variable(l_val) and not is_gep_instr(l_val);
    }
};
