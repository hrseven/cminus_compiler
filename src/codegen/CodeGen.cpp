#include "CodeGen.hpp"

#include "CodeGenUtil.hpp"

#include <unordered_map>

void CodeGen::allocate() {
    // 备份 $ra $fp
    unsigned offset = PROLOGUE_OFFSET_BASE;

    // 为每个参数分配栈空间
    for (auto &arg : context.func->get_args()) {
        auto size = arg.get_type()->get_size();
        offset = offset + size;
        context.offset_map[&arg] = -static_cast<int>(offset);
    }

    // 为指令结果分配栈空间
    for (auto &bb : context.func->get_basic_blocks()) {
        for (auto &instr : bb.get_instructions()) {
            // 每个非 void 的定值都分配栈空间
            if (not instr.is_void()) {
                auto size = instr.get_type()->get_size();
                offset = offset + size;
                context.offset_map[&instr] = -static_cast<int>(offset);
            }
            // alloca 的副作用：分配额外空间
            if (instr.is_alloca()) {
                auto *alloca_inst = static_cast<AllocaInst *>(&instr);
                auto alloc_size = alloca_inst->get_alloca_type()->get_size();
                offset += alloc_size;
            }
        }
    }

    // 分配栈空间，需要是 16 的整数倍
    context.frame_size = ALIGN(offset, PROLOGUE_ALIGN);
}

void CodeGen::copy_stmt() {
    for (auto &succ : context.bb->get_succ_basic_blocks()) {
        for (auto &inst : succ->get_instructions()) {
            if (inst.is_phi()) {
                // 遍历后继块中 phi 的定值 bb
                for (unsigned i = 1; i < inst.get_operands().size(); i += 2) {
                    // phi 的定值 bb 是当前翻译块
                    if (inst.get_operand(i) == context.bb) {
                        auto *lvalue = inst.get_operand(i - 1);
                        if (lvalue->get_type()->is_float_type()) {
                            load_to_freg(lvalue, FReg::fa(0));
                            store_from_freg(&inst, FReg::fa(0));
                        } else {
                            load_to_greg(lvalue, Reg::a(0));
                            store_from_greg(&inst, Reg::a(0));
                        }
                        break;
                    }
                    // 如果没有找到当前翻译块，说明是 undef，无事可做
                }
            } else {
                break;
            }
        }
    }
}

void CodeGen::load_to_greg(Value *val, const Reg &reg) {
    assert(val->get_type()->is_integer_type() ||
           val->get_type()->is_pointer_type());

    if (auto *constant = dynamic_cast<ConstantInt *>(val)) {
        int32_t val = constant->get_value();
        if (IS_IMM_12(val)) {
            append_inst(ADDI WORD, {reg.print(), "$zero", std::to_string(val)});
        } else {
            load_large_int32(val, reg);
        }
    } else if (auto *global = dynamic_cast<GlobalVariable *>(val)) {
        append_inst(LOAD_ADDR, {reg.print(), global->get_name()});
    } else {
        load_from_stack_to_greg(val, reg);
    }
}

void CodeGen::load_large_int32(int32_t val, const Reg &reg) {
    int32_t high_20 = val >> 12; // si20
    uint32_t low_12 = val & LOW_12_MASK;
    append_inst(LU12I_W, {reg.print(), std::to_string(high_20)});
    append_inst(ORI, {reg.print(), reg.print(), std::to_string(low_12)});
}

void CodeGen::load_large_int64(int64_t val, const Reg &reg) {
    auto low_32 = static_cast<int32_t>(val & LOW_32_MASK);
    load_large_int32(low_32, reg);

    auto high_32 = static_cast<int32_t>(val >> 32);
    int32_t high_32_low_20 = (high_32 << 12) >> 12; // si20
    int32_t high_32_high_12 = high_32 >> 20;        // si12
    append_inst(LU32I_D, {reg.print(), std::to_string(high_32_low_20)});
    append_inst(LU52I_D,
                {reg.print(), reg.print(), std::to_string(high_32_high_12)});
}

void CodeGen::load_from_stack_to_greg(Value *val, const Reg &reg) {
    auto offset = context.offset_map.at(val);
    auto offset_str = std::to_string(offset);
    auto *type = val->get_type();
    if (IS_IMM_12(offset)) {
        if (type->is_int1_type()) {
            append_inst(LOAD BYTE, {reg.print(), "$fp", offset_str});
        } else if (type->is_int32_type()) {
            append_inst(LOAD WORD, {reg.print(), "$fp", offset_str});
        } else { // Pointer
            append_inst(LOAD DOUBLE, {reg.print(), "$fp", offset_str});
        }
    } else {
        load_large_int64(offset, reg);
        append_inst(ADD DOUBLE, {reg.print(), "$fp", reg.print()});
        if (type->is_int1_type()) {
            append_inst(LOAD BYTE, {reg.print(), reg.print(), "0"});
        } else if (type->is_int32_type()) {
            append_inst(LOAD WORD, {reg.print(), reg.print(), "0"});
        } else { // Pointer
            append_inst(LOAD DOUBLE, {reg.print(), reg.print(), "0"});
        }
    }
}

void CodeGen::store_from_greg(Value *val, const Reg &reg) {
    auto offset = context.offset_map.at(val);
    auto offset_str = std::to_string(offset);
    auto *type = val->get_type();
    if (IS_IMM_12(offset)) {
        if (type->is_int1_type()) {
            append_inst(STORE BYTE, {reg.print(), "$fp", offset_str});
        } else if (type->is_int32_type()) {
            append_inst(STORE WORD, {reg.print(), "$fp", offset_str});
        } else { // Pointer
            append_inst(STORE DOUBLE, {reg.print(), "$fp", offset_str});
        }
    } else {
        auto addr = Reg::t(8);
        load_large_int64(offset, addr);
        append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
        if (type->is_int1_type()) {
            append_inst(STORE BYTE, {reg.print(), addr.print(), "0"});
        } else if (type->is_int32_type()) {
            append_inst(STORE WORD, {reg.print(), addr.print(), "0"});
        } else { // Pointer
            append_inst(STORE DOUBLE, {reg.print(), addr.print(), "0"});
        }
    }
}

void CodeGen::load_to_freg(Value *val, const FReg &freg) {
    assert(val->get_type()->is_float_type());
    if (auto *constant = dynamic_cast<ConstantFP *>(val)) {
        float val = constant->get_value();
        load_float_imm(val, freg);
    } else {
        auto offset = context.offset_map.at(val);
        auto offset_str = std::to_string(offset);
        if (IS_IMM_12(offset)) {
            append_inst(FLOAD SINGLE, {freg.print(), "$fp", offset_str});
        } else {
            auto addr = Reg::t(8);
            load_large_int64(offset, addr);
            append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
            append_inst(FLOAD SINGLE, {freg.print(), addr.print(), "0"});
        }
    }
}

void CodeGen::load_float_imm(float val, const FReg &r) {
    int32_t bytes = *reinterpret_cast<int32_t *>(&val);
    load_large_int32(bytes, Reg::t(8));
    append_inst(GR2FR WORD, {r.print(), Reg::t(8).print()});
}

void CodeGen::store_from_freg(Value *val, const FReg &r) {
    auto offset = context.offset_map.at(val);
    if (IS_IMM_12(offset)) {
        auto offset_str = std::to_string(offset);
        append_inst(FSTORE SINGLE, {r.print(), "$fp", offset_str});
    } else {
        auto addr = Reg::t(8);
        load_large_int64(offset, addr);
        append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
        append_inst(FSTORE SINGLE, {r.print(), addr.print(), "0"});
    }
}

void CodeGen::gen_prologue() {
    if (IS_IMM_12(-static_cast<int>(context.frame_size))) {
        append_inst("st.d $ra, $sp, -8");
        append_inst("st.d $fp, $sp, -16");
        append_inst("addi.d $fp, $sp, 0");
        append_inst("addi.d $sp, $sp, " +
                    std::to_string(-static_cast<int>(context.frame_size)));
    } else {
        load_large_int64(context.frame_size, Reg::t(0));
        append_inst("st.d $ra, $sp, -8");
        append_inst("st.d $fp, $sp, -16");
        append_inst("sub.d $sp, $sp, $t0");
        append_inst("add.d $fp, $sp, $t0");
    }

    int garg_cnt = 0;
    int farg_cnt = 0;
    for (auto &arg : context.func->get_args()) {
        if (arg.get_type()->is_float_type()) {
            store_from_freg(&arg, FReg::fa(farg_cnt++));
        } else { // int or pointer
            store_from_greg(&arg, Reg::a(garg_cnt++));
        }
    }
}

void CodeGen::gen_epilogue() {
    // TODO 根据你的理解设定函数的 epilogue
    if (IS_IMM_12(-static_cast<int>(context.frame_size))) {
        append_inst("addi.d $sp, $sp, " +
                    std::to_string(static_cast<int>(context.frame_size)));
        append_inst("ld.d $ra, $sp, -8");
        append_inst("ld.d $fp, $sp, -16");
        append_inst("jr $ra");
    } else {
        load_large_int64(context.frame_size, Reg::t(0));
        append_inst("addi.d $sp, $sp, $t0");
        append_inst("ld.d $ra, $sp, -8");
        append_inst("ld.d $fp, $sp, -16");
        append_inst("jr $ra");
        

    }
}


void CodeGen::gen_ret() {
    // TODO 函数返回，思考如何处理返回值、寄存器备份，如何返回调用者地址
    auto *retInst = static_cast<ReturnInst *>(context.inst);
    if (retInst->is_void_ret()) {
        append_inst("addi.d $a0, $zero, 0");
    }
    else 
    {
        // TODO 返回值是空的情况
        if( retInst->get_operand(0)->get_type()->is_float_type()){
            load_to_freg(retInst->get_operand(0), FReg::fa(0));
        }
        else {
            load_to_greg(retInst->get_operand(0), Reg::a(0));
        }
    }
    append_inst("b " + func_exit_label_name(context.func));

}

void CodeGen::gen_br() {
    auto *branchInst = static_cast<BranchInst *>(context.inst);
    if (branchInst->is_cond_br()) {
        // TODO 补全条件跳转的情况
        load_to_greg(branchInst->get_operand(0), Reg::t(0));
        auto *truebb = static_cast<BasicBlock *>(branchInst->get_operand(1));
        auto *falsebb = static_cast<BasicBlock *>(branchInst->get_operand(2));
        append_inst("bstrpick.d $t0, $t0, 0, 0");
        append_inst("bnez $t0, " + label_name(truebb));
        append_inst("b "+label_name(falsebb));

    } else {
        auto *branchbb = static_cast<BasicBlock *>(branchInst->get_operand(0));
        append_inst("b " + label_name(branchbb));
    }
}

void CodeGen::gen_binary() {
    load_to_greg(context.inst->get_operand(0), Reg::t(0));
    load_to_greg(context.inst->get_operand(1), Reg::t(1));
    switch (context.inst->get_instr_type()) {
    case Instruction::add:
        output.emplace_back("add.w $t2, $t0, $t1");
        break;
    case Instruction::sub:
        output.emplace_back("sub.w $t2, $t0, $t1");
        break;
    case Instruction::mul:
        output.emplace_back("mul.w $t2, $t0, $t1");
        break;
    case Instruction::sdiv:
        output.emplace_back("div.w $t2, $t0, $t1");
        break;
    default:
        assert(false);
    }
    store_from_greg(context.inst, Reg::t(2));
}

void CodeGen::gen_float_binary() {
    // TODO 浮点类型的二元指令
    load_to_freg(context.inst->get_operand(0), FReg::ft(0));
    load_to_freg(context.inst->get_operand(1), FReg::ft(1));
    switch (context.inst->get_instr_type()) {
    case Instruction::fadd:
        output.emplace_back("fadd.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fsub:
        output.emplace_back("fsub.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fmul:
        output.emplace_back("fmul.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fdiv:
        output.emplace_back("fdiv.s $ft2, $ft0, $ft1");
        break;
    default:
        assert(false);
    
    }
    store_from_freg(context.inst, FReg::ft(2));
}

void CodeGen::gen_alloca() {
    /* 我们已经为 alloca 的内容分配空间，在此我们还需保存 alloca
     * 指令自身产生的定值，即指向 alloca 空间起始地址的指针
     */
    // TODO 将 alloca 出空间的起始地址保存在栈帧上
    auto offset = context.offset_map.at(context.inst);
    auto *alloca_inst=static_cast<AllocaInst*>(context.inst);
    auto size= static_cast<int>(alloca_inst->get_alloca_type()->get_size());
    auto alloca_addr=offset-size;
    append_inst("addi.d $t1, $fp , " + std::to_string(alloca_addr));
    append_inst("st.d $t1 ,$fp , "+std::to_string(offset));
    

}

void CodeGen::gen_load() {
    auto *ptr = context.inst->get_operand(0);
    auto *type = context.inst->get_type();
    load_to_greg(ptr, Reg::t(0));

    if (type->is_float_type()) {
        append_inst("fld.s $ft0, $t0, 0");
        store_from_freg(context.inst, FReg::ft(0));
    } else {
        // TODO load 整数类型的数据
        if(type->is_int1_type()){
            append_inst("ld.b $t1, $t0, 0");
            store_from_greg(context.inst, Reg::t(1));
        }
        else if (type->is_int32_type())
        {
            append_inst("ld.w $t1, $t0, 0");
            store_from_greg(context.inst, Reg::t(1));
        }
        else{
            append_inst("ld.d $t1, $t0, 0");
            store_from_greg(context.inst, Reg::t(1));
        }
    }
}

void CodeGen::gen_store() {
    // TODO 翻译 store 指令
    load_to_greg(context.inst->get_operand(1), Reg::t(0));
    auto *type= context.inst->get_operand(0)->get_type();
    if (type->is_float_type()) {
        load_to_freg(context.inst->get_operand(0), FReg::ft(0));
        append_inst("fst.s $ft0, $t0, 0");
    }
    else {
        if(type->is_int1_type()){
            load_to_greg(context.inst->get_operand(0), Reg::t(1));
            append_inst("st.b $t1, $t0, 0");
        }
        else if(type->is_int32_type()){
            {
                load_to_greg(context.inst->get_operand(0), Reg::t(1));
                append_inst("st.w $t1, $t0, 0");
            }
        }
        else
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(1));
            append_inst("st.d $t1, $t0, 0");
        }
    }

}

void CodeGen::gen_icmp() {
    // TODO 处理各种整数比较的情况
    switch (context.inst->get_instr_type()) {
    {
    case Instruction::ge:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t0, $t1");
            append_inst("orn $t3, $zero, $t2");
            store_from_greg(context.inst, Reg::t(3));
            break;
        }
    case Instruction::gt:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t1, $t0");
            store_from_greg(context.inst, Reg::t(2));
            break;
        }
    case Instruction::le:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t1, $t0");
            append_inst("orn $t3, $zero, $t2");
            store_from_greg(context.inst, Reg::t(3));
            break;
        }
    case Instruction::lt:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t0, $t1");
            store_from_greg(context.inst, Reg::t(2));
            break;
        }
    case Instruction::eq:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t0, $t1");
            append_inst("slt $t3, $t1, $t0");
            append_inst("nor $t4, $t2, $t3");
            store_from_greg(context.inst, Reg::t(4));
            break;
        }
    case Instruction::ne:
        {
            load_to_greg(context.inst->get_operand(0), Reg::t(0));
            load_to_greg(context.inst->get_operand(1), Reg::t(1));
            append_inst("slt $t2, $t0, $t1");
            append_inst("slt $t3, $t1, $t0");
            append_inst("or $t4, $t2, $t3");
            store_from_greg(context.inst, Reg::t(4));
            break;
        }
        
    
    default:
        assert(false);
        
    }
    }
}

void CodeGen::gen_fcmp() {
    // TODO 处理各种浮点数比较的情况
    load_to_freg(context.inst->get_operand(0), FReg::ft(0));
    load_to_freg(context.inst->get_operand(1), FReg::ft(1));
    switch(context.inst->get_instr_type()) {
    case Instruction::ge:
        append_inst("fcmp.slt.s $fcc0, $ft0, $ft1");
        append_inst("bcnez $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    case Instruction::gt:
        append_inst("fcmp.sle.s $fcc0, $ft0, $ft1");
        append_inst("bcnez $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    case Instruction::le:
        append_inst("fcmp.sle.s $fcc0, $ft0, $ft1");
        append_inst("bceqz $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    case Instruction::lt:
        append_inst("fcmp.slt.s $fcc0, $ft0, $ft1");
        append_inst("bceqz $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    case Instruction::eq:
        append_inst("fcmp.seq.s $fcc0, $ft0, $ft1");
        append_inst("bceqz $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    case Instruction::ne:
        append_inst("fcmp.sne.s $fcc0, $ft0, $ft1");
        append_inst("bceqz $fcc0,.false_label");
        append_inst(".true_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 1");
        store_from_greg(context.inst, Reg::t(0));
        append_inst("b",{".exit"+std::to_string(context.num)+":"});
        append_inst(".false_label"+std::to_string(context.num)+":",ASMInstruction::Label);
        append_inst("addi.w $t0, $zero, 0");
        store_from_greg(context.inst, Reg::t(0));
        append_inst(".exit"+std::to_string(context.num)+":",ASMInstruction::Label);
        context.num++;
        break;
    default:
        assert(false);
    }
    
}

void CodeGen::gen_zext() {
    // TODO 将窄位宽的整数数据进行零扩展
    load_to_greg(context.inst->get_operand(0), Reg::t(0));
    append_inst("bstrpick.w $t1, $t0, 0, 0");
    store_from_greg(context.inst, Reg::t(1));
    
}

void CodeGen::gen_call() {
    // TODO 函数调用，注意我们只需要通过寄存器传递参数，即不需考虑栈上传参的情况
    const std::vector<Value *> args=context.inst->get_operands();
    for (int i=1;i<args.size();i++){
        if(args[i]->get_type()->is_float_type()){
            load_to_freg(args[i],FReg::fa(i-1));
        }
        else{
            load_to_greg(args[i],Reg::a(i-1));
        }
        
    }
    auto *func=static_cast<Function *>(args[0]);
    append_inst("bl "+func->get_name());
    if (func->get_return_type()->is_float_type()){
        store_from_freg(context.inst, FReg::fa(0));
    }
    else if (func->get_return_type()->is_int32_type()||func->get_return_type()->is_int1_type()){
        store_from_greg(context.inst, Reg::a(0));
    }
    
}

/*
 * %op = getelementptr [10 x i32], [10 x i32]* %op, i32 0, i32 %op
 * %op = getelementptr        i32,        i32* %op, i32 %op
 *
 * Memory layout
 *       -            ^
 * +-----------+      | Smaller address
 * |  arg ptr  |---+  |
 * +-----------+   |  |
 * |           |   |  |
 * +-----------+   /  |
 * |           |<--   |
 * |           |   \  |
 * |           |   |  |
 * |   Array   |   |  |
 * |           |   |  |
 * |           |   |  |
 * |           |   |  |
 * +-----------+   |  |
 * |  Pointer  |---+  |
 * +-----------+      |
 * |           |      |
 * +-----------+      |
 * |           |      |
 * +-----------+      |
 * |           |      |
 * +-----------+      | Larger address
 *       +
 */
void CodeGen::gen_gep() {
    // TODO 计算内存地址
    auto *gep_inst=static_cast<GetElementPtrInst *>(context.inst);
    if(context.inst->get_operands().size()==3){
        load_to_greg(context.inst->get_operand(0),Reg::t(0));
        load_to_greg(context.inst->get_operand(2),Reg::t(1));
        auto size=ConstantInt::get(static_cast<int>(gep_inst->get_element_type()->get_size()),m);
        load_to_greg(size,Reg::t(2));
        append_inst("mul.w $t3,$t1,$t2");
        append_inst("add.d $t3,$t0,$t3");
        store_from_greg(context.inst,Reg::t(3));
    }
    else{
        load_to_greg(context.inst->get_operand(0),Reg::t(0));
        load_to_greg(context.inst->get_operand(1),Reg::t(1));
        auto size=ConstantInt::get(static_cast<int>(gep_inst->get_element_type()->get_size()),m);
        load_to_greg(size,Reg::t(2));
        append_inst("mul.w $t3,$t1,$t2");
        append_inst("add.d $t3,$t0,$t3");
        store_from_greg(context.inst,Reg::t(3));
    }
}

void CodeGen::gen_sitofp() {
    // TODO 整数转向浮点数
    load_to_greg(context.inst->get_operand(0),Reg::t(0));
    append_inst("movgr2fr.w $ft0,$t0");
    append_inst("ffint.s.w $ft1, $ft0");
    store_from_freg(context.inst,FReg::ft(1));
}

void CodeGen::gen_fptosi() {
    // TODO 浮点数转向整数，注意向下取整(round to zero)
    load_to_freg(context.inst->get_operand(0),FReg::ft(0));
    append_inst("ftintrz.w.s $ft1, $ft0");
    append_inst("movfr2gr.s $t0,$ft1");
    store_from_greg(context.inst,Reg::t(0));
}

void CodeGen::run() {
    // 确保每个函数中基本块的名字都被设置好
    m->set_print_name();

    /* 使用 GNU 伪指令为全局变量分配空间
     * 你可以使用 `la.local` 指令将标签 (全局变量) 的地址载入寄存器中, 比如
     * 要将 `a` 的地址载入 $t0, 只需要 `la.local $t0, a`
     */
    if (!m->get_global_variable().empty()) {
        append_inst("Global variables", ASMInstruction::Comment);
        /* 虽然下面两条伪指令可以简化为一条 `.bss` 伪指令, 但是我们还是选择使用
         * `.section` 将全局变量放到可执行文件的 BSS 段, 原因如下:
         * - 尽可能对齐交叉编译器 loongarch64-unknown-linux-gnu-gcc 的行为
         * - 支持更旧版本的 GNU 汇编器, 因为 `.bss` 伪指令是应该相对较新的指令,
         *   GNU 汇编器在 2023 年 2 月的 2.37 版本才将其引入
         */
        append_inst(".text", ASMInstruction::Atrribute);
        append_inst(".section", {".bss", "\"aw\"", "@nobits"},
                    ASMInstruction::Atrribute);
        for (auto &global : m->get_global_variable()) {
            auto size =
                global.get_type()->get_pointer_element_type()->get_size();
            append_inst(".globl", {global.get_name()},
                        ASMInstruction::Atrribute);
            append_inst(".type", {global.get_name(), "@object"},
                        ASMInstruction::Atrribute);
            append_inst(".size", {global.get_name(), std::to_string(size)},
                        ASMInstruction::Atrribute);
            append_inst(global.get_name(), ASMInstruction::Label);
            append_inst(".space", {std::to_string(size)},
                        ASMInstruction::Atrribute);
        }
    }

    // 函数代码段
    output.emplace_back(".text", ASMInstruction::Atrribute);
    for (auto &func : m->get_functions()) {
        if (not func.is_declaration()) {
            // 更新 context
            context.clear();
            context.func = &func;

            // 函数信息
            append_inst(".globl", {func.get_name()}, ASMInstruction::Atrribute);
            append_inst(".type", {func.get_name(), "@function"},
                        ASMInstruction::Atrribute);
            append_inst(func.get_name(), ASMInstruction::Label);

            // 分配函数栈帧
            allocate();
            // 生成 prologue
            gen_prologue();

            for (auto &bb : func.get_basic_blocks()) {
                context.bb = &bb;
                append_inst(label_name(context.bb), ASMInstruction::Label);
                for (auto &instr : bb.get_instructions()) {
                    // For debug
                    append_inst(instr.print(), ASMInstruction::Comment);
                    context.inst = &instr; // 更新 context
                    switch (instr.get_instr_type()) {
                    case Instruction::ret:
                        gen_ret();
                        break;
                    case Instruction::br:
                        copy_stmt();
                        gen_br();
                        break;
                    case Instruction::add:
                    case Instruction::sub:
                    case Instruction::mul:
                    case Instruction::sdiv:
                        gen_binary();
                        break;
                    case Instruction::fadd:
                    case Instruction::fsub:
                    case Instruction::fmul:
                    case Instruction::fdiv:
                        gen_float_binary();
                        break;
                    case Instruction::alloca:
                        /* 对于 alloca 指令，我们已经为 alloca
                         * 的内容分配空间，在此我们还需保存 alloca
                         * 指令自身产生的定值，即指向 alloca 空间起始地址的指针
                         */
                        gen_alloca();
                        break;
                    case Instruction::load:
                        gen_load();
                        break;
                    case Instruction::store:
                        gen_store();
                        break;
                    case Instruction::ge:
                    case Instruction::gt:
                    case Instruction::le:
                    case Instruction::lt:
                    case Instruction::eq:
                    case Instruction::ne:
                        gen_icmp();
                        break;
                    case Instruction::fge:
                    case Instruction::fgt:
                    case Instruction::fle:
                    case Instruction::flt:
                    case Instruction::feq:
                    case Instruction::fne:
                        gen_fcmp();
                        break;
                    case Instruction::phi:
                        /* for phi, just convert to a series of
                         * copy-stmts */
                        /* we can collect all phi and deal them at
                         * the end */
                        break;
                    case Instruction::call:
                        gen_call();
                        break;
                    case Instruction::getelementptr:
                        gen_gep();
                        break;
                    case Instruction::zext:
                        gen_zext();
                        break;
                    case Instruction::fptosi:
                        gen_fptosi();
                        break;
                    case Instruction::sitofp:
                        gen_sitofp();
                        break;
                    }
                }
            }
            // 生成 epilogue
            append_inst(func_exit_label_name(context.func), ASMInstruction::Label);
            gen_epilogue();
        }
    }
}

std::string CodeGen::print() const {
    std::string result;
    for (const auto &inst : output) {
        result += inst.format();
    }
    return result;
}
