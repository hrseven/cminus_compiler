#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "GlobalVariable.hpp"
#include "IRBuilder.hpp"
#include "Instruction.hpp"
#include "LICM.hpp"
#include "PassManager.hpp"
#include <cstddef>
#include <memory>
#include <vector>

/**
 * @brief 循环不变式外提Pass的主入口函数
 * 
 */
void LoopInvariantCodeMotion::run() {

    loop_detection_ = std::make_unique<LoopDetection>(m_);
    loop_detection_->run();
    func_info_ = std::make_unique<FuncInfo>(m_);
    func_info_->run();
    for (auto &loop : loop_detection_->get_loops()) {
        is_loop_done_[loop] = false;
    }

    for (auto &loop : loop_detection_->get_loops()) {
        traverse_loop(loop);
    }
}

/**
 * @brief 遍历循环及其子循环
 * @param loop 当前要处理的循环
 * 
 */
void LoopInvariantCodeMotion::traverse_loop(std::shared_ptr<Loop> loop) {
    if (is_loop_done_[loop]) {
        return;
    }
    is_loop_done_[loop] = true;
    for (auto &sub_loop : loop->get_sub_loops()) {
        traverse_loop(sub_loop);
    }
    run_on_loop(loop);
}

// TODO: 实现collect_loop_info函数
// 1. 遍历当前循环及其子循环的所有指令
// 2. 收集所有指令到loop_instructions中
// 3. 检查store指令是否修改了全局变量，如果是则添加到updated_global中
// 4. 检查是否包含非纯函数调用，如果有则设置contains_impure_call为true
void LoopInvariantCodeMotion::collect_loop_info(
    std::shared_ptr<Loop> loop,
    std::set<Value *> &loop_instructions,
    std::set<Value *> &updated_global,
    bool &contains_impure_call) {
    
    for(auto &bb : loop->get_blocks())
    {
        for(auto &instr: bb->get_instructions())
        {
            loop_instructions.insert(&instr);
            if(instr.is_store())
            {
                
                auto *store = static_cast<StoreInst*>(&instr);
                auto &globals=m_->get_global_variable();
                for(auto &global_1: globals)
                {
                    auto global = static_cast<Value*>(&global_1);
                    if(store->get_operand(1)==global)
                    {
                        updated_global.insert(store->get_operand(1));
                    }
                }
            }
            else if(instr.is_call())
            {
                auto *call= static_cast<CallInst*>(&instr);
                if(!func_info_->is_pure_function(static_cast<Function*>(call->get_operand(0))))
                {
                    contains_impure_call = true;
                }
            }
        }
    }
}

/**
 * @brief 对单个循环执行不变式外提优化
 * @param loop 要优化的循环
 * 
 */
void LoopInvariantCodeMotion::run_on_loop(std::shared_ptr<Loop> loop) 
{
    std::set<Value *> loop_instructions;
    std::set<Value *> updated_global;
    bool contains_impure_call = false;
    collect_loop_info(loop, loop_instructions, updated_global, contains_impure_call);

    std::vector<Value *> loop_invariant;
    bool is_invar=true;

    // TODO: 识别循环不变式指令
    //
    // - 如果指令已被标记为不变式则跳过
    // - 跳过 store、ret、br、phi 等指令与非纯函数调用
    // - 特殊处理全局变量的 load 指令
    // - 检查指令的所有操作数是否都是循环不变的
    // - 如果有新的不变式被添加则注意更新 changed 标志，继续迭代

    bool changed;
    do 
    {
        changed = false;

        for(auto &ins: loop_instructions)
        {
            auto instr=static_cast<Instruction*>(ins);
            if(std::find(loop_invariant.begin(), loop_invariant.end(), instr) != loop_invariant.end())
            {
                continue;
            }
            if(instr->is_store() || instr->is_ret() || instr->is_br() || instr->is_phi())
            {
                continue;
            }
            if(instr->is_call())
            {
                auto *call= static_cast<CallInst*>(instr);
                if(!func_info_->is_pure_function(static_cast<Function*>(call->get_operand(0))))
                {
                    continue;
                }
                else 
                {
                    auto func=static_cast<Function*>(call->get_operand(0));
                    for(auto &arg:func->get_args())
                    {
                        if( arg.is<Constant>() ||
                            std::find(loop_instructions.begin(), loop_instructions.end(), &arg)==loop_instructions.end()||
                            std::find(loop_invariant.begin(), loop_invariant.end(), &arg)!=loop_invariant.end())
                        {
                            continue;
                        }
                        else {
                            is_invar=false;
                            break;
                        }
                    }
                    if(is_invar)
                    {
                        loop_invariant.push_back(instr);
                        changed=true;
                    }

                }
            }
            if(instr->is_load())
            {
                auto *load= static_cast<LoadInst*>(instr);
                if(std::find(updated_global.begin(),updated_global.end(),load->get_operand(0))!=updated_global.end())
                {
                    continue;
                }
                else 
                {
                    loop_invariant.push_back(instr);
                    changed=true;
                }
            }
            else
            {
                for(auto &arg_1:instr->get_operands())
                {
                    auto arg=static_cast<Value*>(arg_1);
                    if(arg->is<Constant>()||
                        std::find(loop_instructions.begin(), loop_instructions.end(), arg)==loop_instructions.end()||
                        std::find(loop_invariant.begin(), loop_invariant.end(), arg)!=loop_invariant.end())
                    {
                        continue;
                    }
                    else 
                    {
                        is_invar=false;
                        break;
                    }
                }
                if(is_invar)
                {
                    loop_invariant.push_back(instr);
                    changed=true;
                    
                }
            }

        }

    
    } while (changed);

    if (loop->get_preheader() == nullptr) {
        loop->set_preheader(
            BasicBlock::create(m_, "", loop->get_header()->get_parent()));
    }

    if (loop_invariant.empty())
        return;

    // insert preheader
    auto preheader = loop->get_preheader();
    std::vector<BasicBlock*> outside_preds;
    for (auto &pred : loop->get_header()->get_pre_basic_blocks()) {
        auto blocks=loop->get_blocks();
        if(std::find(blocks.begin(),blocks.end(),pred)==blocks.end())
        {
            outside_preds.push_back(pred);
        }
    }
    // TODO: 更新 phi 指令

    for (auto &phi_inst_ : loop->get_header()->get_instructions()) {
        if (phi_inst_.get_instr_type() != Instruction::phi)
            continue;
        else
        {
            auto phi=static_cast<PhiInst*>(&phi_inst_);
            for(auto &phi_pair:phi->get_phi_pairs())
            {
                if(std::find(outside_preds.begin(),outside_preds.end(),phi_pair.second)!=outside_preds.end())
                {
                    std::vector<Value *> vals = {phi_pair.first};
                    std::vector<BasicBlock *> val_bbs = {phi_pair.second};
                    auto preheader_phi=PhiInst::create_phi(phi_pair.first->get_type(),preheader,vals,val_bbs);
                    preheader->add_instruction(preheader_phi);
                    phi->remove_phi_operand(phi_pair.second);
                    phi->add_phi_pair_operand(preheader_phi, preheader);
                }
            }
            

        }

    }

    // TODO: 用跳转指令重构控制流图 
    // 将所有非 latch 的 header 前驱块的跳转指向 preheader
    // 并将 preheader 的跳转指向 header
    // 注意这里需要更新前驱块的后继和后继的前驱
    std::vector<BasicBlock *> pred_to_remove;
    for (auto &pred : loop->get_header()->get_pre_basic_blocks()) {
        if(std::find(outside_preds.begin(),outside_preds.end(),pred)!=outside_preds.end())
        {
            pred_to_remove.push_back(pred);
            pred->remove_succ_basic_block(loop->get_header());
            pred->add_succ_basic_block(preheader);
            preheader->add_pre_basic_block(pred);
            for(auto &instr:pred->get_instructions())
            {
                if(instr.is_br()&&instr.get_operand(0)==loop->get_header())
                {
                    instr.set_operand(0, preheader);
                }
            }
        }
    }
    preheader->add_succ_basic_block(loop->get_header());
    loop->get_header()->add_pre_basic_block(preheader);



    for (auto &pred : pred_to_remove) {
        loop->get_header()->remove_pre_basic_block(pred);
    }

    // TODO: 外提循环不变指令
    for(auto &invar_1: loop_invariant)
    {
        auto invar=static_cast<Instruction*>(invar_1);
        preheader->add_instr_begin(invar);
        auto parent_block=invar->get_parent();
        parent_block->remove_instr(invar);
    }

    // insert preheader br to header
    BranchInst::create_br(loop->get_header(), preheader);

    // insert preheader to parent loop
    if (loop->get_parent() != nullptr) {
        loop->get_parent()->add_block(preheader);
    }
}

