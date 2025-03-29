#pragma once

#include "BasicBlock.hpp"
#include "PassManager.hpp"

#include <map>
#include <set>

class Dominators : public Pass {
  public:
    using BBSet = std::set<BasicBlock *>;

    explicit Dominators(Module *m) : Pass(m) {}
    ~Dominators() = default;
    void run() override;
    void run_on_func(Function *f);

    // functions for getting information
    BasicBlock *get_idom(BasicBlock *bb) { return idom_.at(bb); }
    const BBSet &get_dominance_frontier(BasicBlock *bb) {
        return dom_frontier_.at(bb);
    }
    const BBSet &get_dom_tree_succ_blocks(BasicBlock *bb) {
        return dom_tree_succ_blocks_.at(bb);
    }

    // print cfg or dominance tree
    void dump_cfg(Function *f);
    void dump_dominator_tree(Function *f);

    // functions for dominance tree
    const bool is_dominate(BasicBlock *bb1, BasicBlock *bb2) {
        return dom_tree_L_.at(bb1) <= dom_tree_L_.at(bb2) &&
               dom_tree_R_.at(bb1) >= dom_tree_L_.at(bb2);
    }

    const std::vector<BasicBlock *> &get_dom_dfs_order() {
        return dom_dfs_order_;
    }

    const std::vector<BasicBlock *> &get_dom_post_order() {
        return dom_post_order_;
    }
    std::map<BasicBlock *, BBSet> dom_frontier_{}; // 支配边界集合
  private:

    void dfs(BasicBlock *bb, std::set<BasicBlock *> &visited);
    void create_idom(Function *f);
    void create_dominance_frontier(Function *f);
    void create_dom_tree_succ(Function *f);
    void create_dom_dfs_order(Function *f);

    BasicBlock * intersect(BasicBlock *b1, BasicBlock *b2);

    void create_reverse_post_order(Function *f);
    void set_idom(BasicBlock *bb, BasicBlock *idom) { idom_[bb] = idom; }
    void set_dominance_frontier(BasicBlock *bb, BBSet &df) {
        dom_frontier_[bb].clear();
        dom_frontier_[bb].insert(df.begin(), df.end());
    }
    void add_dom_tree_succ_block(BasicBlock *bb, BasicBlock *dom_tree_succ_bb) {
        dom_tree_succ_blocks_[bb].insert(dom_tree_succ_bb);
    }
    unsigned int get_post_order(BasicBlock *bb) {
        return post_order_.at(bb);
    }
    // for debug
    void print_idom(Function *f);
    void print_dominance_frontier(Function *f);

    // TODO 补充需要的函数
    std::list<BasicBlock *> reverse_post_order_{};
    std::map<BasicBlock *, int> post_order_id_{}; // the root has highest ID

    std::vector<BasicBlock *> post_order_vec_{}; // 后序
    std::map<BasicBlock *, unsigned int> post_order_{}; // 后序
    std::map<BasicBlock *, BasicBlock *> idom_{}; 
     // 直接支配
    std::map<BasicBlock *, BasicBlock *> idoms{};
    
    std::map<BasicBlock *, BBSet> dom_tree_succ_blocks_{}; // 支配树中的后继节点

    // 支配树上的dfs序L,R
    std::map<BasicBlock *, unsigned int> dom_tree_L_;
    std::map<BasicBlock *, unsigned int> dom_tree_R_;

    std::vector<BasicBlock *> dom_dfs_order_;
    std::vector<BasicBlock *> dom_post_order_;

};
