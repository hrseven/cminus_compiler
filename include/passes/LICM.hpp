#include "FuncInfo.hpp"
#include "LoopDetection.hpp"
#include "PassManager.hpp"
#include <memory>
#include <unordered_map>

class LoopInvariantCodeMotion : public Pass {
  public:
    LoopInvariantCodeMotion(Module *m) : Pass(m) {}
    ~LoopInvariantCodeMotion() = default;

    void run() override;

  private:
    std::unordered_map<std::shared_ptr<Loop>, bool> is_loop_done_;
    std::unique_ptr<LoopDetection> loop_detection_;
    std::unique_ptr<FuncInfo> func_info_;
    void traverse_loop(std::shared_ptr<Loop> loop);
    void run_on_loop(std::shared_ptr<Loop> loop);
    void collect_loop_info(std::shared_ptr<Loop> loop,
                          std::set<Value *> &loop_instructions,
                          std::set<Value *> &updated_global,
                          bool &contains_impure_call);
};