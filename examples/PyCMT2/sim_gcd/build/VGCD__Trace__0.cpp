// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Tracing implementation internals
#include "verilated_vcd_c.h"
#include "VGCD__Syms.h"


void VGCD___024root__trace_chg_0_sub_0(VGCD___024root* vlSelf, VerilatedVcd::Buffer* bufp);

void VGCD___024root__trace_chg_0(void* voidSelf, VerilatedVcd::Buffer* bufp) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root__trace_chg_0\n"); );
    // Init
    VGCD___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VGCD___024root*>(voidSelf);
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    if (VL_UNLIKELY(!vlSymsp->__Vm_activity)) return;
    // Body
    VGCD___024root__trace_chg_0_sub_0((&vlSymsp->TOP), bufp);
}

void VGCD___024root__trace_chg_0_sub_0(VGCD___024root* vlSelf, VerilatedVcd::Buffer* bufp) {
    (void)vlSelf;  // Prevent unused variable warning
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root__trace_chg_0_sub_0\n"); );
    auto &vlSelfRef = std::ref(*vlSelf).get();
    // Init
    uint32_t* const oldp VL_ATTR_UNUSED = bufp->oldp(vlSymsp->__Vm_baseCode + 1);
    // Body
    bufp->chgBit(oldp+0,(vlSelfRef.clk));
    bufp->chgBit(oldp+1,(vlSelfRef.rst));
    bufp->chgBit(oldp+2,(vlSelfRef.load_enable));
    bufp->chgBit(oldp+3,(vlSelfRef.load_ready));
    bufp->chgIData(oldp+4,(vlSelfRef.load_a),32);
    bufp->chgIData(oldp+5,(vlSelfRef.load_b),32);
    bufp->chgBit(oldp+6,(vlSelfRef.result_ready));
    bufp->chgIData(oldp+7,(vlSelfRef.result_res0),32);
    bufp->chgIData(oldp+8,(vlSelfRef.GCD__DOT__reg_a__DOT__value),32);
    bufp->chgIData(oldp+9,(((IData)(vlSelfRef.GCD__DOT___GEN_1)
                             ? (IData)((0x1ffffffffULL 
                                        & ((IData)(vlSelfRef.GCD__DOT___GEN_2)
                                            ? ((QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value)) 
                                               - (QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value)))
                                            : (QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value)))))
                             : vlSelfRef.load_a)),32);
    bufp->chgIData(oldp+10,(vlSelfRef.GCD__DOT__reg_b__DOT__value),32);
    bufp->chgIData(oldp+11,(((IData)(vlSelfRef.GCD__DOT___GEN_1)
                              ? (IData)((0x1ffffffffULL 
                                         & ((IData)(vlSelfRef.GCD__DOT___GEN_2)
                                             ? (QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value))
                                             : ((QData)((IData)(vlSelfRef.GCD__DOT__reg_b__DOT__value)) 
                                                - (QData)((IData)(vlSelfRef.GCD__DOT__reg_a__DOT__value))))))
                              : vlSelfRef.load_b)),32);
}

void VGCD___024root__trace_cleanup(void* voidSelf, VerilatedVcd* /*unused*/) {
    VL_DEBUG_IF(VL_DBG_MSGF("+    VGCD___024root__trace_cleanup\n"); );
    // Init
    VGCD___024root* const __restrict vlSelf VL_ATTR_UNUSED = static_cast<VGCD___024root*>(voidSelf);
    VGCD__Syms* const __restrict vlSymsp VL_ATTR_UNUSED = vlSelf->vlSymsp;
    VlUnpacked<CData/*0:0*/, 1> __Vm_traceActivity;
    for (int __Vi0 = 0; __Vi0 < 1; ++__Vi0) {
        __Vm_traceActivity[__Vi0] = 0;
    }
    // Body
    vlSymsp->__Vm_activity = false;
    __Vm_traceActivity[0U] = 0U;
}
