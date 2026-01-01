// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See VGCD.h for the primary calling header

#include "VGCD__pch.h"
#include "VGCD__Syms.h"
#include "VGCD___024root.h"

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__ico(VGCD___024root* vlSelf);
#endif  // VL_DEBUG

void VGCD___024root___eval_triggers__ico(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_triggers__ico\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__VicoTriggered.set(0U, (IData)(vlSelfRef.__VicoFirstIteration));
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        VGCD___024root___dump_triggers__ico(vlSelf);
    }
#endif
}

#ifdef VL_DEBUG
VL_ATTR_COLD void VGCD___024root___dump_triggers__act(VGCD___024root* vlSelf);
#endif  // VL_DEBUG

void VGCD___024root___eval_triggers__act(VGCD___024root* vlSelf) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root___eval_triggers__act\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Body
    vlSelfRef.__VactTriggered.set(0U, ((IData)(vlSelfRef.clk) 
                                       & (~ (IData)(vlSelfRef.__Vtrigprevexpr___TOP__clk__0))));
    vlSelfRef.__Vtrigprevexpr___TOP__clk__0 = vlSelfRef.clk;
#ifdef VL_DEBUG
    if (VL_UNLIKELY(vlSymsp->_vm_contextp__->debug())) {
        VGCD___024root___dump_triggers__act(vlSelf);
    }
#endif
}
