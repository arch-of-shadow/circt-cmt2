// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See VGCD.h for the primary calling header

#ifndef VERILATED_VGCD___024ROOT_H_
#define VERILATED_VGCD___024ROOT_H_  // guard

#include "verilated.h"


class VGCD__Syms;

class alignas(VL_CACHE_LINE_BYTES) VGCD___024root final : public VerilatedModule {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(rst,0,0);
    VL_IN8(load_enable,0,0);
    VL_OUT8(load_ready,0,0);
    VL_OUT8(result_ready,0,0);
    CData/*0:0*/ GCD__DOT___GEN_1;
    CData/*0:0*/ GCD__DOT___GEN_2;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VicoFirstIteration;
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactContinue;
    VL_IN(load_a,31,0);
    VL_IN(load_b,31,0);
    VL_OUT(result_res0,31,0);
    IData/*31:0*/ GCD__DOT__reg_a__DOT__value;
    IData/*31:0*/ GCD__DOT__reg_b__DOT__value;
    IData/*31:0*/ __VactIterCount;
    QData/*32:0*/ GCD__DOT___GEN_3;
    QData/*32:0*/ GCD__DOT___GEN_4;
    VlTriggerVec<1> __VstlTriggered;
    VlTriggerVec<1> __VicoTriggered;
    VlTriggerVec<1> __VactTriggered;
    VlTriggerVec<1> __VnbaTriggered;

    // INTERNAL VARIABLES
    VGCD__Syms* const vlSymsp;

    // CONSTRUCTORS
    VGCD___024root(VGCD__Syms* symsp, const char* v__name);
    ~VGCD___024root();
    VL_UNCOPYABLE(VGCD___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
