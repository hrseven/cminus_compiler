#pragma once

#include "ASMInstruction.hpp"
#include "Module.hpp"
#include "Register.hpp"

class CodeGen {
  public:
    explicit CodeGen(Module *module) : m(module) {}

    std::string print() const;

    void run();

    template <class... Args> void append_inst(Args... arg) {
        output.emplace_back(arg...);
    }

    void
    append_inst(const char *inst, std::initializer_list<std::string> args,
                ASMInstruction::InstType ty = ASMInstruction::Instruction) {
        auto content = std::string(inst) + " ";
        for (const auto &arg : args) {
            content += arg + ", ";
        }
        content.pop_back();
        content.pop_back();
        output.emplace_back(content, ty);
    }

  private:
    void allocate();
    void copy_stmt(); // for phi copy

    // 向寄存器中装载数据
    void load_to_greg(Value *, const Reg &);
    void load_to_freg(Value *, const FReg &);
    void load_from_stack_to_greg(Value *, const Reg &);

    // 向寄存器中加载立即数
    void load_large_int32(int32_t, const Reg &);
    void load_large_int64(int64_t, const Reg &);
    void load_float_imm(float, const FReg &);

    // 将寄存器中的数据保存回栈上
    void store_from_greg(Value *, const Reg &);
    void store_from_freg(Value *, const FReg &);

    void gen_prologue();
    void gen_ret();
    void gen_br();
    void gen_binary();
    void gen_float_binary();
    void gen_alloca();
    void gen_load();
    void gen_store();
    void gen_icmp();
    void gen_fcmp();
    void gen_zext();
    void gen_call();
    void gen_gep();
    void gen_sitofp();
    void gen_fptosi();
    void gen_epilogue();

    static std::string label_name(BasicBlock *bb) {
        return "." + bb->get_parent()->get_name() + "_" + bb->get_name();
    }

    static std::string func_exit_label_name(Function *func) {
        return func->get_name() + "_exit";
    }

    static std::string fcmp_label_name(BasicBlock *bb, unsigned cnt) {
        return label_name(bb) + "_fcmp_" + std::to_string(cnt);
    }

    struct {
        /* 随着ir遍历设置 */
        Function *func{nullptr};    // 当前函数
        BasicBlock *bb{nullptr};    // 当前基本块
        Instruction *inst{nullptr}; // 当前指令
        int num{0};
        /* 在allocate()中设置 */
        unsigned frame_size{0}; // 当前函数的栈帧大小
        std::unordered_map<Value *, int> offset_map{}; // 指针相对 fp 的偏移
        unsigned fcmp_cnt{0}; // fcmp 的计数器, 用于创建 fcmp 需要的 label

        void clear() {
            func = nullptr;
            bb = nullptr;
            inst = nullptr;
            frame_size = 0;
            fcmp_cnt = 0;
            offset_map.clear();
        }

    } context;

    Module *m;
    std::list<ASMInstruction> output;
};
