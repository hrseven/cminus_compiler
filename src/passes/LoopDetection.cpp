#include "LoopDetection.hpp"
#include "Dominators.hpp"
#include <memory>

/**
 * @brief 循环检测Pass的主入口函数
 *
 * 该函数执行以下步骤：
 * 1. 创建支配树分析实例
 * 2. 遍历模块中的所有函数
 * 3. 对每个非声明函数执行循环检测
 * 4. 最后打印检测结果
 */
void LoopDetection::run() {
    dominators_ = std::make_unique<Dominators>(m_);
    for (auto &f1 : m_->get_functions()) {
        auto f = &f1;
        if (f->is_declaration())
            continue;
        func_ = f;
        run_on_func(f);
    }
    print();
}

/**
 * @brief 发现循环及其子循环
 * @param bb 循环的header块
 * @param latches 循环的回边终点(latch)集合
 * @param loop 当前正在处理的循环对象
 */
void LoopDetection::discover_loop_and_sub_loops(BasicBlock *bb, BBset &latches,
                                                std::shared_ptr<Loop> loop) {
    // TODO List:
    // 1. 初始化工作表，将所有latch块加入
    // 2. 实现主循环逻辑
    // 3. 处理未分配给任何循环的节点
    // 4. 处理已属于其他循环的节点
    // 5. 建立正确的循环嵌套关系

    BBvec work_list = {latches.begin(), latches.end()}; // 初始化工作表

    while (!work_list.empty()) { // 当工作表非空时继续处理
        auto bb = work_list.back();
        work_list.pop_back();

        // TODO-1: 处理未分配给任何循环的节点
        if (bb_to_loop_.find(bb) == bb_to_loop_.end()) {
            /* 在此添加代码：
             * 1. 使用loop->add_block将bb加入当前循环
             * 2. 更新bb_to_loop_映射
             * 3. 将bb的所有前驱加入工作表
             */
            loop->add_block(bb);
            bb_to_loop_[bb]=loop;
            for(auto &pred:bb->get_pre_basic_blocks()){
                work_list.push_back(pred);
            }
            


        
        }
        // TODO-2: 处理已属于其他循环的节点
        else if (bb_to_loop_[bb] != loop) {
            /* 在此添加代码：
             * 1. 获取bb当前所属的循环sub_loop
             * 2. 找到sub_loop的最顶层父循环
             * 3. 检查是否需要继续处理
             * 4. 建立循环嵌套关系：
             *    - 设置父循环
             *    - 添加子循环
             * 5. 将子循环的所有基本快加入到父循环中
             * 6. 将子循环header的前驱加入工作表
             */

            auto sub_loop=bb_to_loop_[bb];
            auto top_sub_loop=sub_loop;
            while(top_sub_loop->get_parent()!=nullptr){
                top_sub_loop=top_sub_loop->get_parent();
            }
            if(top_sub_loop==loop){
                continue;
            }
            top_sub_loop->set_parent(loop);
            loop->add_sub_loop(top_sub_loop);
            for(auto &bb1:top_sub_loop->get_blocks()){
                loop->add_block(bb1);
            }
            for(auto &pre:top_sub_loop->get_header()->get_pre_basic_blocks()){
                work_list.push_back(pre);
            }

        }
    }
}

/**
 * @brief 对单个函数执行循环检测
 * @param f 要分析的函数
 *
 * 该函数通过以下步骤检测循环：
 * 1. 运行支配树分析
 * 2. 按支配树后序遍历所有基本块
 * 3. 对每个块，检查其前驱是否存在回边
 * 4. 如果存在回边，创建新的循环并：
 *    - 设置循环header
 *    - 添加latch节点
 *    - 发现循环体和子循环
 */
void LoopDetection::run_on_func(Function *f) {
    dominators_->run_on_func(f);
    for (auto &bb1 : dominators_->get_dom_post_order()) {
        auto bb = bb1;
        BBset latches;
        for (auto &pred : bb->get_pre_basic_blocks()) {
            if (dominators_->is_dominate(bb, pred)) {
                // pred is a back edge
                // pred -> bb , pred is the latch node
                latches.insert(pred);
            }
        }
        if (latches.empty()) {
            continue;
        }
        // create loop
        auto loop = std::make_shared<Loop>(bb);
        bb_to_loop_[bb] = loop;
        // add latch nodes
        for (auto &latch : latches) {
            loop->add_latch(latch);
        }
        loops_.push_back(loop);
        discover_loop_and_sub_loops(bb, latches, loop);
    }
}

/**
 * @brief 打印循环检测的结果
 *
 * 为每个检测到的循环打印：
 * 1. 循环的header块
 * 2. 循环包含的所有基本块
 * 3. 循环的所有子循环
 */
void LoopDetection::print() {
    m_->set_print_name();
    std::cerr << "Loop Detection Result:" << std::endl;
    for (auto &loop : loops_) {
        std::cerr << "Loop header: " << loop->get_header()->get_name()
                  << std::endl;
        std::cerr << "Loop blocks: ";
        for (auto &bb : loop->get_blocks()) {
            std::cerr << bb->get_name() << " ";
        }
        std::cerr << std::endl;
        std::cerr << "Sub loops: ";
        for (auto &sub_loop : loop->get_sub_loops()) {
            std::cerr << sub_loop->get_header()->get_name() << " ";
        }
        std::cerr << std::endl;
    }
}