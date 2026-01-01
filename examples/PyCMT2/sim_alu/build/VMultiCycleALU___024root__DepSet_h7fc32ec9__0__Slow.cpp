// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See VMultiCycleALU.h for the primary calling header

#include "VMultiCycleALU__pch.h"
#include "VMultiCycleALU___024root.h"

VL_ATTR_COLD void VMultiCycleALU___024root___eval_static(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_static\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
}

VL_ATTR_COLD void VMultiCycleALU___024root___eval_initial__TOP(VMultiCycleALU___024root* vlSelf);

VL_ATTR_COLD void VMultiCycleALU___024root___eval_initial(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_initial\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    VMultiCycleALU___024root___eval_initial__TOP(vlSelf);
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
}

VL_ATTR_COLD void VMultiCycleALU___024root___eval_initial__TOP(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_initial__TOP\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.is_ready_ready = 1U;
}

VL_ATTR_COLD void VMultiCycleALU___024root___eval_final(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_final\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VMultiCycleALU___024root___dump_triggers__stl(VMultiCycleALU___024root* vlSelf);
#endif  // VL_DEBUG
VL_ATTR_COLD bool VMultiCycleALU___024root___eval_phase__stl(VMultiCycleALU___024root* vlSelf);

VL_ATTR_COLD void VMultiCycleALU___024root___eval_settle(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_settle\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    IData/*31:0*/ __VstlIterCount;
    CData/*0:0*/ __VstlContinue;
    // Body
    __VstlIterCount = 0U;
    vlSelfRef.__VstlFirstIteration = 1U;
    __VstlContinue = 1U;
    while (__VstlContinue) {
        if (VL_UNLIKELY((0x64U < __VstlIterCount))) {
#ifdef VL_DEBUG
            VMultiCycleALU___024root___dump_triggers__stl(vlSelf);
#endif
            VL_FATAL_MT("rtl/MultiCycleALU.sv", 2, "", "Settle region did not converge.");
        }
        __VstlIterCount = ((IData)(1U) + __VstlIterCount);
        __VstlContinue = 0U;
        if (VMultiCycleALU___024root___eval_phase__stl(vlSelf)) {
            __VstlContinue = 1U;
        }
        vlSelfRef.__VstlFirstIteration = 0U;
    }
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VMultiCycleALU___024root___dump_triggers__stl(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___dump_triggers__stl\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1U & (~ vlSelfRef.__VstlTriggered.any()))) {
        VL_DBG_MSGF("         No triggers active\n");
    }
    if ((1ULL & vlSelfRef.__VstlTriggered.word(0U))) {
        VL_DBG_MSGF("         'stl' region trigger index 0 is active: Internal 'stl' trigger - first iteration\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void VMultiCycleALU___024root___stl_sequent__TOP__0(VMultiCycleALU___024root* vlSelf);

VL_ATTR_COLD void VMultiCycleALU___024root___eval_stl(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_stl\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VstlTriggered.word(0U))) {
        VMultiCycleALU___024root___stl_sequent__TOP__0(vlSelf);
    }
}

VL_ATTR_COLD void VMultiCycleALU___024root___stl_sequent__TOP__0(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___stl_sequent__TOP__0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.get_result_res0 = vlSelfRef.MultiCycleALU__DOT__reg_result__DOT__value;
    vlSelfRef.start_ready = (1U & (~ (IData)(vlSelfRef.MultiCycleALU__DOT__busy__DOT__value)));
    vlSelfRef.get_result_ready = vlSelfRef.start_ready;
    vlSelfRef.is_ready_res0 = vlSelfRef.start_ready;
    vlSelfRef.MultiCycleALU__DOT___GEN_2 = ((IData)(vlSelfRef.start_ready) 
                                            & (IData)(vlSelfRef.start_enable));
    vlSelfRef.MultiCycleALU__DOT___GEN = ((~ ((~ (IData)(vlSelfRef.MultiCycleALU__DOT___GEN_2)) 
                                              & (IData)(vlSelfRef.MultiCycleALU__DOT__busy__DOT__value))) 
                                          & (IData)(vlSelfRef.MultiCycleALU__DOT___GEN_2));
}

VL_ATTR_COLD void VMultiCycleALU___024root___eval_triggers__stl(VMultiCycleALU___024root* vlSelf);

VL_ATTR_COLD bool VMultiCycleALU___024root___eval_phase__stl(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___eval_phase__stl\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    CData/*0:0*/ __VstlExecute;
    // Body
    VMultiCycleALU___024root___eval_triggers__stl(vlSelf);
    __VstlExecute = vlSelfRef.__VstlTriggered.any();
    if (__VstlExecute) {
        VMultiCycleALU___024root___eval_stl(vlSelf);
    }
    return (__VstlExecute);
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VMultiCycleALU___024root___dump_triggers__ico(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___dump_triggers__ico\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1U & (~ vlSelfRef.__VicoTriggered.any()))) {
        VL_DBG_MSGF("         No triggers active\n");
    }
    if ((1ULL & vlSelfRef.__VicoTriggered.word(0U))) {
        VL_DBG_MSGF("         'ico' region trigger index 0 is active: Internal 'ico' trigger - first iteration\n");
    }
}
#endif  // VL_DEBUG

#ifdef VL_DEBUG
VL_ATTR_COLD void VMultiCycleALU___024root___dump_triggers__act(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___dump_triggers__act\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1U & (~ vlSelfRef.__VactTriggered.any()))) {
        VL_DBG_MSGF("         No triggers active\n");
    }
    if ((1ULL & vlSelfRef.__VactTriggered.word(0U))) {
        VL_DBG_MSGF("         'act' region trigger index 0 is active: @(posedge clk)\n");
    }
}
#endif  // VL_DEBUG

#ifdef VL_DEBUG
VL_ATTR_COLD void VMultiCycleALU___024root___dump_triggers__nba(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___dump_triggers__nba\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1U & (~ vlSelfRef.__VnbaTriggered.any()))) {
        VL_DBG_MSGF("         No triggers active\n");
    }
    if ((1ULL & vlSelfRef.__VnbaTriggered.word(0U))) {
        VL_DBG_MSGF("         'nba' region trigger index 0 is active: @(posedge clk)\n");
    }
}
#endif  // VL_DEBUG

VL_ATTR_COLD void VMultiCycleALU___024root___ctor_var_reset(VMultiCycleALU___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VMultiCycleALU__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VMultiCycleALU___024root___ctor_var_reset\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelf->clk = VL_RAND_RESET_I(1);
    vlSelf->rst = VL_RAND_RESET_I(1);
    vlSelf->start_enable = VL_RAND_RESET_I(1);
    vlSelf->start_ready = VL_RAND_RESET_I(1);
    vlSelf->start_a = VL_RAND_RESET_I(32);
    vlSelf->start_b = VL_RAND_RESET_I(32);
    vlSelf->get_result_ready = VL_RAND_RESET_I(1);
    vlSelf->get_result_res0 = VL_RAND_RESET_I(32);
    vlSelf->is_ready_ready = VL_RAND_RESET_I(1);
    vlSelf->is_ready_res0 = VL_RAND_RESET_I(1);
    vlSelf->MultiCycleALU__DOT___GEN = VL_RAND_RESET_I(1);
    vlSelf->MultiCycleALU__DOT___GEN_2 = VL_RAND_RESET_I(1);
    vlSelf->MultiCycleALU__DOT__reg_a__DOT__value = VL_RAND_RESET_I(32);
    vlSelf->MultiCycleALU__DOT__reg_b__DOT__value = VL_RAND_RESET_I(32);
    vlSelf->MultiCycleALU__DOT__reg_result__DOT__value = VL_RAND_RESET_I(32);
    vlSelf->MultiCycleALU__DOT__busy__DOT__value = VL_RAND_RESET_I(1);
    vlSelf->__Vtrigprevexpr___TOP__clk__0 = VL_RAND_RESET_I(1);
    for (int __Vi0 = 0; __Vi0 < 2; ++__Vi0) {
        vlSelf->__Vm_traceActivity[__Vi0] = 0;
    }
}
