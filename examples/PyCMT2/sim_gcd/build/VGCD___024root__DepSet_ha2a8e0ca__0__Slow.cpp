// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See VGCD.h for the primary calling header

#include "VGCD__pch.h"
#include "VGCD___024root.h"

VL_ATTR_COLD void VGCD___024root___eval_static(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_static\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
}

VL_ATTR_COLD void VGCD___024root___eval_initial__TOP(VGCD___024root* vlSelf);

VL_ATTR_COLD void VGCD___024root___eval_initial(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_initial\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    VGCD___024root___eval_initial__TOP(vlSelf);
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
}

VL_ATTR_COLD void VGCD___024root___eval_initial__TOP(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_initial__TOP\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.load_ready = 1U;
}

VL_ATTR_COLD void VGCD___024root___eval_final(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_final\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__stl(VGCD___024root* vlSelf);
#endif  // VL_DEBUG
VL_ATTR_COLD bool VGCD___024root___eval_phase__stl(VGCD___024root* vlSelf);

VL_ATTR_COLD void VGCD___024root___eval_settle(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_settle\n"); );
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
            VGCD___024root___dump_triggers__stl(vlSelf);
#endif
            VL_FATAL_MT("rtl/GCD.sv", 2, "", "Settle region did not converge.");
        }
        __VstlIterCount = ((IData)(1U) + __VstlIterCount);
        __VstlContinue = 0U;
        if (VGCD___024root___eval_phase__stl(vlSelf)) {
            __VstlContinue = 1U;
        }
        vlSelfRef.__VstlFirstIteration = 0U;
    }
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__stl(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___dump_triggers__stl\n"); );
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

VL_ATTR_COLD void VGCD___024root___stl_sequent__TOP__0(VGCD___024root* vlSelf);

VL_ATTR_COLD void VGCD___024root___eval_stl(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_stl\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VstlTriggered.word(0U))) {
        VGCD___024root___stl_sequent__TOP__0(vlSelf);
    }
}

VL_ATTR_COLD void VGCD___024root___stl_sequent__TOP__0(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___stl_sequent__TOP__0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.result_ready = (0U == vlSelfRef.GCD__DOT__reg_b__DOT__value);
    vlSelfRef.result_res0 = vlSelfRef.GCD__DOT__reg_a__DOT__value;
    vlSelfRef.GCD__DOT___GEN_1 = ((~ (IData)(vlSelfRef.load_enable)) 
                                  & (0U != vlSelfRef.GCD__DOT__reg_b__DOT__value));
    vlSelfRef.GCD__DOT___GEN_2 = (vlSelfRef.GCD__DOT__reg_a__DOT__value 
                                  > vlSelfRef.GCD__DOT__reg_b__DOT__value);
    if (vlSelfRef.GCD__DOT___GEN_2) {
        vlSelfRef.GCD__DOT___GEN_3 = (0x1ffffffffULL 
                                      & ((QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value)) 
                                         - (QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value))));
        vlSelfRef.GCD__DOT___GEN_4 = (0x1ffffffffULL 
                                      & (QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value)));
    } else {
        vlSelfRef.GCD__DOT___GEN_3 = (0x1ffffffffULL 
                                      & (QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value)));
        vlSelfRef.GCD__DOT___GEN_4 = (0x1ffffffffULL 
                                      & ((QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value)) 
                                         - (QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value))));
    }
}

VL_ATTR_COLD void VGCD___024root___eval_triggers__stl(VGCD___024root* vlSelf);

VL_ATTR_COLD bool VGCD___024root___eval_phase__stl(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_phase__stl\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    CData/*0:0*/ __VstlExecute;
    // Body
    VGCD___024root___eval_triggers__stl(vlSelf);
    __VstlExecute = vlSelfRef.__VstlTriggered.any();
    if (__VstlExecute) {
        VGCD___024root___eval_stl(vlSelf);
    }
    return (__VstlExecute);
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__ico(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___dump_triggers__ico\n"); );
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
VL_ATTR_COLD void VGCD___024root___dump_triggers__act(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___dump_triggers__act\n"); );
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
VL_ATTR_COLD void VGCD___024root___dump_triggers__nba(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___dump_triggers__nba\n"); );
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

VL_ATTR_COLD void VGCD___024root___ctor_var_reset(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___ctor_var_reset\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelf->clk = VL_RAND_RESET_I(1);
    vlSelf->rst = VL_RAND_RESET_I(1);
    vlSelf->load_enable = VL_RAND_RESET_I(1);
    vlSelf->load_ready = VL_RAND_RESET_I(1);
    vlSelf->load_a = VL_RAND_RESET_I(32);
    vlSelf->load_b = VL_RAND_RESET_I(32);
    vlSelf->result_ready = VL_RAND_RESET_I(1);
    vlSelf->result_res0 = VL_RAND_RESET_I(32);
    vlSelf->GCD__DOT___GEN_1 = VL_RAND_RESET_I(1);
    vlSelf->GCD__DOT___GEN_2 = VL_RAND_RESET_I(1);
    vlSelf->GCD__DOT___GEN_3 = VL_RAND_RESET_Q(33);
    vlSelf->GCD__DOT___GEN_4 = VL_RAND_RESET_Q(33);
    vlSelf->GCD__DOT__reg_a__DOT__value = VL_RAND_RESET_I(32);
    vlSelf->GCD__DOT__reg_b__DOT__value = VL_RAND_RESET_I(32);
    vlSelf->__Vtrigprevexpr___TOP__clk__0 = VL_RAND_RESET_I(1);
}
