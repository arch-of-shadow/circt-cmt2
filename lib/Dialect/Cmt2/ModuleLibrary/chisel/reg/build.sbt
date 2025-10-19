// Build configuration for FIRRTLReg Chisel module

scalaVersion := "2.13.10"

libraryDependencies ++= Seq(
  "edu.berkeley.cs" %% "chisel3" % "3.6.0"
)

scalacOptions ++= Seq(
  "-language:reflectiveCalls",
  "-deprecation",
  "-feature",
  "-Xcheckinit"
)

addCompilerPlugin("edu.berkeley.cs" % "chisel3-plugin" % "3.6.0" cross CrossVersion.full)