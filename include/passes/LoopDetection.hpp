#pragma once
#include "Dominators.hpp"
#include "PassManager.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;
class Dominators;
class Function;
class Module;

using BBset = std::set<BasicBlock *>;
using BBvec = std::vector<BasicBlock *>;
class Loop {
  private:
    // attribute:
    // preheader, header, blocks, parent, sub_loops, latches
    BasicBlock *preheader_ = nullptr;
    BasicBlock *header_;

    std::shared_ptr<Loop> parent_ = nullptr;
    BBvec blocks_;
    std::vector<std::shared_ptr<Loop>> sub_loops_;
    
    std::unordered_set<BasicBlock *> latches_;
  public:
    Loop(BasicBlock *header) : header_(header) {
        blocks_.push_back(header);
    }
    ~Loop() = default;
    void add_block(BasicBlock *bb) { blocks_.push_back(bb); }
    BasicBlock *get_header() { return header_; }
    BasicBlock *get_preheader() { return preheader_; }
    std::shared_ptr<Loop> get_parent() { return parent_; }
    void set_parent(std::shared_ptr<Loop> parent) { parent_ = parent; }
    void set_preheader(BasicBlock *bb) { preheader_ = bb; }
    void add_sub_loop(std::shared_ptr<Loop> loop) { sub_loops_.push_back(loop); }
    const BBvec& get_blocks() { return blocks_; }
    const std::vector<std::shared_ptr<Loop>>& get_sub_loops() { return sub_loops_; }
    const std::unordered_set<BasicBlock *>& get_latches() { return latches_; }
    void add_latch(BasicBlock *bb) { latches_.insert(bb); }
};

class LoopDetection : public Pass {
  private:
    Function *func_;
    std::unique_ptr<Dominators> dominators_;
    std::vector<std::shared_ptr<Loop>> loops_;
    // map from header to loop
    std::unordered_map<BasicBlock *, std::shared_ptr<Loop>> bb_to_loop_;
    void discover_loop_and_sub_loops(BasicBlock *bb, BBset &latches,
                                     std::shared_ptr<Loop> loop);

  public:
    LoopDetection(Module *m) : Pass(m) {}
    ~LoopDetection() = default;

    void run() override;
    void run_on_func(Function *f);
    void print() ;
    std::vector<std::shared_ptr<Loop>> &get_loops() { return loops_; }
};
