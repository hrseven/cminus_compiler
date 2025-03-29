add_library(
    passes STATIC
    DeadCode.cpp
    Dominators.cpp
    FuncInfo.cpp
    LoopDetection.cpp
    LICM.cpp
    Mem2Reg.cpp
)