// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See VGCD.h for the primary calling header

#include "VGCD__pch.h"
#include "VGCD___024root.h"

void VGCD___024root___ico_sequent__TOP__0(VGCD___024root* vlSelf);

void VGCD___024root___eval_ico(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_ico\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VicoTriggered.word(0U))) {
        VGCD___024root___ico_sequent__TOP__0(vlSelf);
    }
}

VL_INLINE_OPT void VGCD___024root___ico_sequent__TOP__0(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___ico_sequent__TOP__0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.GCD__DOT___GEN_1 = ((~ (IData)(vlSelfRef.load_enable)) 
                                  & (0U != vlSelfRef.GCD__DOT__reg_b__DOT__value));
}

void VGCD___024root___eval_triggers__ico(VGCD___024root* vlSelf);

bool VGCD___024root___eval_phase__ico(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_phase__ico\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    CData/*0:0*/ __VicoExecute;
    // Body
    VGCD___024root___eval_triggers__ico(vlSelf);
    __VicoExecute = vlSelfRef.__VicoTriggered.any();
    if (__VicoExecute) {
        VGCD___024root___eval_ico(vlSelf);
    }
    return (__VicoExecute);
}

void VGCD___024root___eval_act(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_act\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
}

void VGCD___024root___nba_sequent__TOP__0(VGCD___024root* vlSelf);

void VGCD___024root___eval_nba(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_nba\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if ((1ULL & vlSelfRef.__VnbaTriggered.word(0U))) {
        VGCD___024root___nba_sequent__TOP__0(vlSelf);
    }
}

VL_INLINE_OPT void VGCD___024root___nba_sequent__TOP__0(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___nba_sequent__TOP__0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (vlSelfRef.rst) {
        vlSelfRef.GCD__DOT__reg_a__DOT__value = 0U;
        vlSelfRef.GCD__DOT__reg_b__DOT__value = 0U;
    } else if (vlSelfRef.GCD__DOT___GEN_1) {
        vlSelfRef.GCD__DOT__reg_a__DOT__value = (IData)(vlSelfRef.GCD__DOT___GEN_3);
        vlSelfRef.GCD__DOT__reg_b__DOT__value = (IData)(vlSelfRef.GCD__DOT___GEN_4);
    } else {
        vlSelfRef.GCD__DOT__reg_a__DOT__value = vlSelfRef.load_a;
        vlSelfRef.GCD__DOT__reg_b__DOT__value = vlSelfRef.load_b;
    }
    vlSelfRef.result_res0 = vlSelfRef.GCD__DOT__reg_a__DOT__value;
    vlSelfRef.result_ready = (0U == vlSelfRef.GCD__DOT__reg_b__DOT__value);
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

void VGCD___024root___eval_triggers__act(VGCD___024root* vlSelf);

bool VGCD___024root___eval_phase__act(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_phase__act\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    VlTriggerVec<1> __VpreTriggered;
    CData/*0:0*/ __VactExecute;
    // Body
    VGCD___024root___eval_triggers__act(vlSelf);
    __VactExecute = vlSelfRef.__VactTriggered.any();
    if (__VactExecute) {
        __VpreTriggered.andNot(vlSelfRef.__VactTriggered, vlSelfRef.__VnbaTriggered);
        vlSelfRef.__VnbaTriggered.thisOr(vlSelfRef.__VactTriggered);
        VGCD___024root___eval_act(vlSelf);
    }
    return (__VactExecute);
}

bool VGCD___024root___eval_phase__nba(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_phase__nba\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    CData/*0:0*/ __VnbaExecute;
    // Body
    __VnbaExecute = vlSelfRef.__VnbaTriggered.any();
    if (__VnbaExecute) {
        VGCD___024root___eval_nba(vlSelf);
        vlSelfRef.__VnbaTriggered.clear();
    }
    return (__VnbaExecute);
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__ico(VGCD___024root* vlSelf);
#endif  // VL_DEBUG
#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__nba(VGCD___024root* vlSelf);
#endif  // VL_DEBUG
#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__act(VGCD___024root* vlSelf);
#endif  // VL_DEBUG

void VGCD___024root___eval(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    IData/*31:0*/ __VicoIterCount;
    CData/*0:0*/ __VicoContinue;
    IData/*31:0*/ __VnbaIterCount;
    CData/*0:0*/ __VnbaContinue;
    // Body
    __VicoIterCount = 0U;
    vlSelfRef.__VicoFirstIteration = 1U;
    __VicoContinue = 1U;
    while (__VicoContinue) {
        if (VL_UNLIKELY((0x64U < __VicoIterCount))) {
#ifdef VL_DEBUG
            VGCD___024root___dump_triggers__ico(vlSelf);
#endif
            VL_FATAL_MT("rtl/GCD.sv", 2, "", "Input combinational region did not converge.");
        }
        __VicoIterCount = ((IData)(1U) + __VicoIterCount);
        __VicoContinue = 0U;
        if (VGCD___024root___eval_phase__ico(vlSelf)) {
            __VicoContinue = 1U;
        }
        vlSelfRef.__VicoFirstIteration = 0U;
    }
    __VnbaIterCount = 0U;
    __VnbaContinue = 1U;
    while (__VnbaContinue) {
        if (VL_UNLIKELY((0x64U < __VnbaIterCount))) {
#ifdef VL_DEBUG
            VGCD___024root___dump_triggers__nba(vlSelf);
#endif
            VL_FATAL_MT("rtl/GCD.sv", 2, "", "NBA region did not converge.");
        }
        __VnbaIterCount = ((IData)(1U) + __VnbaIterCount);
        __VnbaContinue = 0U;
        vlSelfRef.__VactIterCount = 0U;
        vlSelfRef.__VactContinue = 1U;
        while (vlSelfRef.__VactContinue) {
            if (VL_UNLIKELY((0x64U < vlSelfRef.__VactIterCount))) {
#ifdef VL_DEBUG
                VGCD___024root___dump_triggers__act(vlSelf);
#endif
                VL_FATAL_MT("rtl/GCD.sv", 2, "", "Active region did not converge.");
            }
            vlSelfRef.__VactIterCount = ((IData)(1U) 
                                         + vlSelfRef.__VactIterCount);
            vlSelfRef.__VactContinue = 0U;
            if (VGCD___024root___eval_phase__act(vlSelf)) {
                vlSelfRef.__VactContinue = 1U;
            }
        }
        if (VGCD___024root___eval_phase__nba(vlSelf)) {
            __VnbaContinue = 1U;
        }
    }
}

#ifdef VL_DEBUG
void VGCD___024root___eval_debug_assertions(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_debug_assertions\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    if (VL_UNLIKELY((vlSelfRef.clk & 0xfeU))) {
        Verilated::overWidthError("clk");}
    if (VL_UNLIKELY((vlSelfRef.rst & 0xfeU))) {
        Verilated::overWidthError("rst");}
    if (VL_UNLIKELY((vlSelfRef.load_enable & 0xfeU))) {
        Verilated::overWidthError("load_enable");}
}
#endif  // VL_DEBUG
