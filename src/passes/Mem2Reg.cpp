#include "Mem2Reg.hpp"
#include "IRBuilder.hpp"
#include "Value.hpp"

#include <iostream>
#include <locale>
#include <memory>

#define IS_GLOBAL_VARIABLE(l_val) dynamic_cast<GlobalVariable *>(l_val)
#define IS_GEP_INSTR(l_val) dynamic_cast<GetElementPtrInst *>(l_val)
/**
 * @brief Mem2Reg Pass的主入口函数
 * 
 * 该函数执行内存到寄存器的提升过程，将栈上的局部变量提升到SSA格式。
 * 主要步骤：
 * 1. 创建并运行支配树分析
 * 2. 对每个非声明函数：
 *    - 清空相关数据结构
 *    - 插入必要的phi指令
 *    - 执行变量重命名
 * 
 * 注意：函数执行后，冗余的局部变量分配指令将由后续的死代码删除Pass处理
 */
void Mem2Reg::run() {
    // 创建支配树分析 Pass 的实例
    dominators_ = std::make_unique<Dominators>(m_);
    // 建立支配树
    dominators_->run();
    // 以函数为单元遍历实现 Mem2Reg 算法
    
    for (auto &f : m_->get_functions()) {
        if (f.is_declaration())
            continue;
        func_ = &f;
        var_val_stack.clear();
        phi_lval.clear();

        if (func_->get_basic_blocks().size() >= 1) {
            // 对应伪代码中 phi 指令插入的阶段
            generate_phi();
            // 对应伪代码中重命名阶段
            rename(func_->get_entry_block());
        }

        // 后续 DeadCode 将移除冗余的局部变量的分配空间
    }
    
    
}

/**
 * @brief 在必要的位置插入phi指令
 * 
 * 该函数实现了经典的phi节点插入算法：
 * 1. 收集全局活跃变量：
 *    - 扫描所有store指令
 *    - 识别在多个基本块中被赋值的变量
 * 
 * 2. 插入phi指令：
 *    - 对每个全局活跃变量
 *    - 在其定值点的支配边界处插入phi指令
 *    - 使用工作表法处理迭代式的phi插入
 * 
 * phi指令的插入遵循最小化原则，只在必要的位置插入phi节点
 */
void Mem2Reg::generate_phi() {
    // global_live_var_name 是全局名字集合，以 alloca 出的局部变量来统计。
    // 步骤一：找到活跃在多个 block 的全局名字集合，以及它们所属的 bb 块
    std::set<Value *> global_live_var_name;
    std::map<Value *, std::set<BasicBlock *>> live_var_2blocks;
    for (auto &bb : func_->get_basic_blocks()) {
        std::set<Value *> var_is_killed;
        for (auto &instr : bb.get_instructions()) {
            if (instr.is_store()) {
                // store i32 a, i32 *b
                // a is r_val, b is l_val
                auto l_val = static_cast<StoreInst *>(&instr)->get_lval();
                if (is_valid_ptr(l_val)) {
                    global_live_var_name.insert(l_val);
                    live_var_2blocks[l_val].insert(&bb);
                }
            }
        }
    }

    // 步骤二：从支配树获取支配边界信息，并在对应位置插入 phi 指令
    std::map<std::pair<BasicBlock *, Value *>, bool>
        bb_has_var_phi; // bb has phi for var
    for (auto var : global_live_var_name) {
        std::vector<BasicBlock *> work_list;
        work_list.assign(live_var_2blocks[var].begin(),
                         live_var_2blocks[var].end());
        for (unsigned i = 0; i < work_list.size(); i++) {
            auto bb = work_list[i];
            for (auto bb_dominance_frontier_bb :
                 dominators_->get_dominance_frontier(bb)) {
                if (bb_has_var_phi.find({bb_dominance_frontier_bb, var}) ==
                    bb_has_var_phi.end()) {
                    // generate phi for bb_dominance_frontier_bb & add
                    // bb_dominance_frontier_bb to work list
                    auto phi = PhiInst::create_phi(
                        var->get_type()->get_pointer_element_type(),
                        bb_dominance_frontier_bb);
                    phi_lval.emplace(phi, var);
                    bb_dominance_frontier_bb->add_instr_begin(phi);
                    work_list.push_back(bb_dominance_frontier_bb);
                    bb_has_var_phi[{bb_dominance_frontier_bb, var}] = true;
                }
            }
        }
    }
}

void Mem2Reg::rename(BasicBlock *bb) {
    // TODO
    // 步骤一：将 phi 指令作为 lval 的最新定值，lval 即是为局部变量 alloca 出的地址空间
    // 步骤二：用 lval 最新的定值替代对应的load指令
    // 步骤三：将 store 指令的 rval，也即被存入内存的值，作为 lval 的最新定值
    // 步骤四：为 lval 对应的 phi 指令参数补充完整
    // 步骤五：对 bb 在支配树上的所有后继节点，递归执行 re_name 操作
    // 步骤六：pop出 lval 的最新定值
    
    // 步骤七：清除冗余的指令
    std::vector<Instruction *> wait_delete;

    for (auto &instr_1 : bb->get_instructions() )
    {
        auto instr = &instr_1;
        if (instr->is_phi())
        {
            auto l_val = phi_lval[static_cast<PhiInst *>(instr)];
            var_val_stack[l_val].push(instr);
        }
    }

    for (auto &instr_1 : bb->get_instructions() )
    {
        auto instr=&instr_1;
        if ( instr->is_load() )
        {
            auto l_val = static_cast<LoadInst *>(instr)->get_lval();

            if (!IS_GLOBAL_VARIABLE(l_val) && !IS_GEP_INSTR(l_val))
            {
                if ( var_val_stack.find(l_val)!=var_val_stack.end())
                {
                    instr->replace_all_use_with(var_val_stack[l_val].top());
                    wait_delete.push_back(instr);
                }
            }
        }
        if (instr->is_store())
        {
            
            auto l_val = static_cast<StoreInst *>(instr)->get_lval();
            auto r_val = static_cast<StoreInst *>(instr)->get_rval();

            if (!IS_GLOBAL_VARIABLE(l_val) && !IS_GEP_INSTR(l_val))
            {
                var_val_stack[l_val].push(r_val);
                wait_delete.push_back(instr);
            }
            
        }
    }

    for ( auto succ_bb : bb->get_succ_basic_blocks() )
    {
        for ( auto &instr_1 : succ_bb->get_instructions() )
        {
            auto instr = &instr_1;
            if ( instr->is_phi())
            {
                auto l_val = phi_lval[static_cast<PhiInst *>(instr)];
                if (var_val_stack.find(l_val)!= var_val_stack.end())
                {
                    assert(var_val_stack[l_val].size()!=0);
                    
                    static_cast<PhiInst *>(instr)->add_phi_pair_operand( var_val_stack[l_val].top(), bb);
                }
                
            }
        }
    }

    for ( auto dom_succ_bb : dominators_->get_dom_tree_succ_blocks(bb) )
    {
        rename(dom_succ_bb);
    }

    for (auto &instr_1 : bb->get_instructions())
    {
        auto instr = &instr_1;
        // step 7: pop lval's lastest definition
        if(instr->is_store())
        {
            auto l_val = static_cast<StoreInst *>(instr)->get_lval();
            if (!IS_GLOBAL_VARIABLE(l_val) && !IS_GEP_INSTR(l_val))
            {
                var_val_stack[l_val].pop();
            }
            
        }
        else if (instr->is_phi())
        {
            auto l_val =phi_lval[static_cast<PhiInst *>(instr)];
            if ( var_val_stack.find(l_val)!=var_val_stack.end())
            {
                var_val_stack[l_val].pop();
            }
        }
    }

    for ( auto instr : wait_delete)
    {
        bb->erase_instr(instr);
    }


    
}


