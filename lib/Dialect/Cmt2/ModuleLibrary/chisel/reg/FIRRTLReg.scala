//===- FIRRTLReg.scala - Parametric Register in Chisel --------*- Scala -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This module implements a parametric register with ready-enable protocol.
//
//===----------------------------------------------------------------------===//

package cmt2.lib

import chisel3._
import chisel3.util._

/**
  * Parametric register module with ready-enable protocol
  *
  * @param width Width of the register in bits
  */
class FIRRTLReg(width: Int = 32) extends Module {
  val io = IO(new Bundle {
    // Write interface (method)
    val write_enable = Input(Bool())
    val write_data = Input(UInt(width.W))
    val write_ready = Output(Bool())

    // Read interface (value)
    val read_ready = Output(Bool())
    val read_data = Output(UInt(width.W))
  })

  // Internal register storage
  val reg = RegInit(0.U(width.W))

  // Read is always ready and returns current register value
  io.read_ready := true.B
  io.read_data := reg

  // Write is always ready
  io.write_ready := true.B

  // On write enable, update the register
  when(io.write_enable) {
    reg := io.write_data
  }
}

/**
  * Main object to generate FIRRTL
  */
object FIRRTLRegMain extends App {
  // Parse width parameter from command line (default: 32)
  val width = if (args.length > 0) args(0).toInt else 32

  // Generate FIRRTL using emitFirrtl
  val firrtl = chisel3.emitFirrtl(new FIRRTLReg(width))

  // Output filename
  val outputFile = s"Reg_width${width}.fir"

  // Write FIRRTL to file
  val writer = new java.io.PrintWriter(outputFile)
  writer.write(firrtl)
  writer.close()

  println(s"Generated $outputFile")
}
