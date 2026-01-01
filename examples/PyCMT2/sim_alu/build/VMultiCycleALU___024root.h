// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design internal header
// See VMultiCycleALU.h for the primary calling header

#ifndef VERILATED_VMULTICYCLEALU___024ROOT_H_
#define VERILATED_VMULTICYCLEALU___024ROOT_H_  // guard

#include "verilated.h"


class VMultiCycleALU__Syms;

class alignas(VL_CACHE_LINE_BYTES) VMultiCycleALU___024root final : public VerilatedModule {
  public:

    // DESIGN SPECIFIC STATE
    VL_IN8(clk,0,0);
    VL_IN8(rst,0,0);
    VL_IN8(start_enable,0,0);
    VL_OUT8(start_ready,0,0);
    VL_OUT8(get_result_ready,0,0);
    VL_OUT8(is_ready_ready,0,0);
    VL_OUT8(is_ready_res0,0,0);
    CData/*0:0*/ MultiCycleALU__DOT___GEN;
    CData/*0:0*/ MultiCycleALU__DOT___GEN_2;
    CData/*0:0*/ MultiCycleALU__DOT__busy__DOT__value;
    CData/*0:0*/ __VstlFirstIteration;
    CData/*0:0*/ __VicoFirstIteration;
    CData/*0:0*/ __Vtrigprevexpr___TOP__clk__0;
    CData/*0:0*/ __VactContinue;
    VL_IN(start_a,31,0);
    VL_IN(start_b,31,0);
    VL_OUT(get_result_res0,31,0);
    IData/*31:0*/ MultiCycleALU__DOT__reg_a__DOT__value;
    IData/*31:0*/ MultiCycleALU__DOT__reg_b__DOT__value;
    IData/*31:0*/ MultiCycleALU__DOT__reg_result__DOT__value;
    IData/*31:0*/ __VactIterCount;
    VlUnpacked<CData/*0:0*/, 2> __Vm_traceActivity;
    VlTriggerVec<1> __VstlTriggered;
    VlTriggerVec<1> __VicoTriggered;
    VlTriggerVec<1> __VactTriggered;
    VlTriggerVec<1> __VnbaTriggered;

    // INTERNAL VARIABLES
    VMultiCycleALU__Syms* const vlSymsp;

    // CONSTRUCTORS
    VMultiCycleALU___024root(VMultiCycleALU__Syms* symsp, const char* v__name);
    ~VMultiCycleALU___024root();
    VL_UNCOPYABLE(VMultiCycleALU___024root);

    // INTERNAL METHODS
    void __Vconfigure(bool first);
};


#endif  // guard
