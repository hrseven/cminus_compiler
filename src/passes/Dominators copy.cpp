#include "Dominators.hpp"
#include "Function.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include<map>
#include<unordered_map>
#include<algorithm>


/**
 * @brief 支配器分析的入口函数
 * 
 * 遍历模块中的所有函数，对每个非声明的函数执行支配关系分析。
 */
void Dominators::run() {
    for(auto &f1 : m_->get_functions()) {
        auto f = &f1;
        if(f->is_declaration())
            continue;
        std::cout<<f->print()<<std::endl;
        run_on_func(f);
    }
}

/**
 * @brief 对单个函数执行支配关系分析
 * @param f 要分析的函数
 * 
 * 该函数执行完整的支配关系分析流程：
 * 1. 初始化数据结构
 * 2. 创建反向后序遍历序列
 * 3. 计算直接支配者(idom)
 * 4. 计算支配边界
 * 5. 构建支配树的后继关系
 * 6. 创建支配树的DFS序
 */
void Dominators::run_on_func(Function *f) {
    dom_post_order_.clear();
    dom_dfs_order_.clear();
    for(auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;
        idom_.insert({bb, nullptr});
        dom_frontier_.insert({bb, {}});
        dom_tree_succ_blocks_.insert({bb, {}});
    }
    create_reverse_post_order(f);
    create_dom_dfs_order(f);
    
    create_idom(f);
    /*
    print_idom(f);
    */

    create_dominance_frontier(f);
    create_dom_tree_succ(f);
    
    dump_cfg(f);
    dump_dominator_tree(f);/*
    */
}

/**
 * @brief 计算两个基本块的支配关系交集
 * @param b1 第一个基本块
 * @param b2 第二个基本块
 * @return 返回在支配树上最深的同时支配b1和b2的节点
 * 
 * 该函数使用后序号来查找两个节点的最近公共支配者。
 * 通过在支配树上向上遍历直到找到交点。
 */
BasicBlock *Dominators::intersect(BasicBlock *b1, BasicBlock *b2) {
    while (b1 != b2) {

        while (get_post_order(b1) < get_post_order(b2)) {

            b1 = idoms[b1];
            
        }
        while (get_post_order(b2) < get_post_order(b1)) {
            b2 = idoms[b2];
            
        }
    }
    return b1;
}

/**
 * @brief 创建函数的反向后序遍历序列
 * @param f 要处理的函数
 * 
 * 通过DFS遍历CFG来构建基本块的后序遍历序列。
 * 这个序列用于后续的支配关系分析。
 */
void Dominators::create_reverse_post_order(Function *f) {
    BBSet visited;
    dfs(f->get_entry_block(), visited);
}

/**
 * @brief 深度优先搜索辅助函数
 * @param bb 当前遍历的基本块
 * @param visited 已访问的基本块集合
 * 
 * 执行DFS遍历，维护后序遍历序列和每个基本块的后序号。
 */
void Dominators::dfs(BasicBlock *bb, std::set<BasicBlock *> &visited) {
    visited.insert(bb);
    for (auto &succ : bb->get_succ_basic_blocks()) {
        if (visited.find(succ) == visited.end()) {
            dfs(succ, visited);
        }
    }
    post_order_vec_.push_back(bb);
    post_order_.insert({bb, post_order_.size()});
}

/**
 * @brief 计算所有基本块的直接支配者(immediate dominator)
 * @param f 要分析的函数
 * 
 * 使用迭代算法计算每个基本块的直接支配者：
 * 1. 将入口块的直接支配者设置为自身
 * 2. 重复遍历所有基本块，更新它们的直接支配者
 * 3. 当没有变化时算法终止
 */
void Dominators::create_idom(Function *f) {
    // TODO 分析得到 f 中各个基本块的 idom
    // 初始化
    for(auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;

        idoms[bb] = nullptr;
    }
    idoms[f->get_entry_block()] = f->get_entry_block();
    bool changed = true;
    std::vector<BasicBlock *> reverse_post_order_vec = post_order_vec_;
    std::reverse(reverse_post_order_vec.begin(), reverse_post_order_vec.end());
    while(changed) {
        changed = false;
        for(auto &bb : reverse_post_order_vec) {
            if(bb == f->get_entry_block()) continue;
            auto pres= bb->get_pre_basic_blocks();
            if(pres.empty()) {
                continue; // 前驱列表为空，跳过该基本块
            }
            auto new_idom=pres.front();
            for(auto &pre : pres) {
                if(pre==new_idom) continue;
                if(idoms[pre]!=nullptr){
                    new_idom=intersect(pre,new_idom );
                }
            }
            if(idoms[bb]!=new_idom) {
                idoms[bb]=new_idom;
                changed = true;
            }
        }
    }
    idom_ = idoms;
    


}

/**
 * @brief 计算所有基本块的支配边界(dominance frontier)
 * @param f 要分析的函数
 * 
 * 对于每个有多个前驱的基本块B：
 * 从每个前驱P开始，沿着支配树向上遍历直到遇到B的直接支配者，
 * 将B加入路径上所有节点的支配边界中。
 */
void Dominators::create_dominance_frontier(Function *f) {
    // TODO 分析得到 f 中各个基本块的支配边界集合
    for(auto &bb1: f->get_basic_blocks()){
        auto bb = &bb1;
        std::vector<BasicBlock *> points;
        if(bb->get_pre_basic_blocks().size()<=1)continue;
        for(auto &pre : bb->get_pre_basic_blocks()){
            auto point=pre;
            while(point!=idom_[bb]){
                points.push_back(point);
                point=idom_[point];
            }
        }
        for(auto &point : points){
            dom_frontier_[point].insert(bb);
        }
    }

}

/**
 * @brief 构建支配树的后继关系
 * @param f 要处理的函数
 * 
 * 基于已计算的直接支配者关系，构建支配树的子节点关系。
 * 如果A是B的直接支配者，则B是A在支配树上的后继。
 */
void Dominators::create_dom_tree_succ(Function *f) {
    // TODO 分析得到 f 中各个基本块的支配树后继
    for(auto &bb1 : f->get_basic_blocks()){
        auto bb = &bb1;
        if(bb==f->get_entry_block()) continue;
        auto idom= idom_[bb];
        dom_tree_succ_blocks_[idom].insert(bb);
    }
}

/**
 * @brief 为支配树创建深度优先搜索序
 * @param f 要处理的函数
 * 
 * 该函数通过深度优先搜索遍历支配树，为每个基本块分配两个序号：
 * 1. dom_tree_L_：记录DFS首次访问该节点的时间戳
 * 2. dom_tree_R_：记录DFS完成访问该节点子树的时间戳
 * 
 * 同时维护：
 * - dom_dfs_order_：按DFS访问顺序记录基本块
 * - dom_post_order_：dom_dfs_order_的逆序
 * 
 * 这些序号和顺序可用于快速判断支配关系：
 * 如果节点A支配节点B，则A的L值小于B的L值，且A的R值大于B的R值
 */
void Dominators::create_dom_dfs_order(Function *f) {
    // 分析得到 f 中各个基本块的支配树上的dfs序L,R
    unsigned int order = 0;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        dom_tree_L_[bb] = ++ order;
        dom_dfs_order_.push_back(bb);
        for (auto &succ : dom_tree_succ_blocks_[bb]) {
            dfs(succ);
        }
        dom_tree_R_[bb] = order;
    };
    dfs(f->get_entry_block());
    dom_post_order_ =
        std::vector<BasicBlock*>(dom_dfs_order_.rbegin(), dom_dfs_order_.rend());
}

/**
 * @brief 打印函数的直接支配关系
 * @param f 要打印的函数
 * 
 * 该函数以可读格式打印函数中所有基本块的直接支配者(immediate dominator)。
 * 输出格式为：
 * 基本块名: 其直接支配者名
 * 如果基本块没有直接支配者(如入口块)，则显示"null"。
 */
void Dominators::print_idom(Function *f) {
    f->get_parent()->set_print_name();
    int counter = 0;
    std::map<BasicBlock *, std::string> bb_id;
    for (auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;
        if (bb->get_name().empty())
            bb_id[bb] = "bb" + std::to_string(counter);
        else
            bb_id[bb] = bb->get_name();
        counter++;
    }
    printf("Immediate dominance of function %s:\n", f->get_name().c_str());
    for (auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;
        std::string output;
        output = bb_id[bb] + ": ";
        if (get_idom(bb)) {
            output += bb_id[get_idom(bb)];
        } else {
            output += "null";
        }
        printf("%s\n", output.c_str());
    }
}

/**
 * @brief 打印函数的支配边界信息
 * @param f 要打印的函数
 * 
 * 该函数以可读格式打印函数中所有基本块的支配边界(dominance frontier)。
 * 输出格式为：
 * 基本块名: 支配边界中的基本块列表
 * 如果基本块没有支配边界，则显示"null"。
 */
void Dominators::print_dominance_frontier(Function *f) {
    f->get_parent()->set_print_name();
    int counter = 0;
    std::map<BasicBlock *, std::string> bb_id;
    for (auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;
        if (bb->get_name().empty())
            bb_id[bb] = "bb" + std::to_string(counter);
        else
            bb_id[bb] = bb->get_name();
        counter++;
    }
    printf("Dominance Frontier of function %s:\n", f->get_name().c_str());
    for (auto &bb1 : f->get_basic_blocks()) {
        auto bb = &bb1;
        std::string output;
        output = bb_id[bb] + ": ";
        if (get_dominance_frontier(bb).empty()) {
            output += "null";
        } else {
            bool first = true;
            for (auto df : get_dominance_frontier(bb)) {
                if (first) {
                    first = false;
                } else {
                    output += ", ";
                }
                output += bb_id[df];
            }
        }
        printf("%s\n", output.c_str());
    }
}

/**
 * @brief 将函数的控制流图(CFG)导出为图形文件
 * @param f 要导出的函数
 * 
 * 该函数生成函数的控制流图的DOT格式描述，并使用graphviz将其转换为PNG图像。
 * 生成两个文件：
 * - {函数名}_cfg.dot：DOT格式的图形描述
 * - {函数名}_cfg.png：可视化的控制流图
 */
void Dominators::dump_cfg(Function *f)
{
    f->get_parent()->set_print_name();
    if(f->is_declaration())
        return;
    std::vector<std::string> edge_set;
    bool has_edges = false;
    for (auto &bb : f->get_basic_blocks()) {
        auto succ_blocks = bb.get_succ_basic_blocks();
        if(!succ_blocks.empty())
            has_edges = true;
        for (auto succ : succ_blocks) {
            edge_set.push_back('\t' + bb.get_name() + "->" + succ->get_name() + ";\n");
        }
    }
    std::string digraph = "digraph G {\n";
    if (!has_edges && !f->get_basic_blocks().empty()) {
        // 如果没有边且至少有一个基本块，添加一个自环以显示唯一的基本块
        auto &bb = f->get_basic_blocks().front();
        digraph += '\t' + bb.get_name() + ";\n";
    } else {
        for (auto &edge : edge_set) {
            digraph += edge;
        }
    }
    digraph += "}\n";
    std::ofstream file_output;
    file_output.open(f->get_name() + "_cfg.dot", std::ios::out);
    file_output << digraph;
    file_output.close();
    std::string dot_cmd = "dot -Tpng " + f->get_name() + "_cfg.dot" + " -o " + f->get_name() + "_cfg.png";
    std::system(dot_cmd.c_str());
}

/**
 * @brief 将函数的支配树导出为图形文件
 * @param f 要导出的函数
 * 
 * 该函数生成函数的支配树的DOT格式描述，并使用graphviz将其转换为PNG图像。
 * 生成两个文件：
 * - {函数名}_dom_tree.dot：DOT格式的图形描述
 * - {函数名}_dom_tree.png：可视化的支配树
 */
void Dominators::dump_dominator_tree(Function *f)
{
    f->get_parent()->set_print_name();
    if(f->is_declaration())
        return;

    std::vector<std::string> edge_set;
    bool has_edges = false; // 用于检查是否有边存在

    for (auto &b : f->get_basic_blocks()) {
        if (idom_.find(&b) != idom_.end() && idom_[&b] != &b) {
            edge_set.push_back('\t' + idom_[&b]->get_name() + "->" + b.get_name() + ";\n");
            has_edges = true; // 如果存在支配边，标记为 true
        }
    }

    std::string digraph = "digraph G {\n";

    if (!has_edges && !f->get_basic_blocks().empty()) {
        // 如果没有边且至少有一个基本块，直接添加该块以显示它
        auto &b = f->get_basic_blocks().front();
        digraph += '\t' + b.get_name() + ";\n";
    } else {
        for (auto &edge : edge_set) {
            digraph += edge;
        }
    }

    digraph += "}\n";

    std::ofstream file_output;
    file_output.open(f->get_name() + "_dom_tree.dot", std::ios::out);
    file_output << digraph;
    file_output.close();

    std::string dot_cmd = "dot -Tpng " + f->get_name() + "_dom_tree.dot" + " -o " + f->get_name() + "_dom_tree.png";
    std::system(dot_cmd.c_str());
}