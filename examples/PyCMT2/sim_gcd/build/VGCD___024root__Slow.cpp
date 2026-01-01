// Verilated -*- C++ -*-
// DESCRIPTION: Verilator output: Design implementation internals
// See VGCD.h for the primary calling header

#include "VGCD__pch.h"
#include "VGCD__Syms.h"
#include "VGCD___024root.h"

void VGCD___024root___ctor_var_reset(VGCD___024root* vlSelf);

VGCD___024root::VGCD___024root(VGCD__Syms* symsp, const char* v__name)
    : VerilatedModule{v__name}
    , vlSymsp{symsp}
 {
    // Reset structure values
    VGCD___024root___ctor_var_reset(this);
}

void VGCD___024root::__Vconfigure(bool first) {
    (void)first;  // Prevent unused variable warning
}

VGCD___024root::~VGCD___024root() {
}
