// DumpFuncs.java —— 按地址列表反编译若干函数
// 用法: -postScript DumpFuncs.java "0x90f44,0x905ac,..." <out.txt>

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpFuncs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        java.util.List<String> addrList = new java.util.ArrayList<>();
        try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader(args[0]))) {
            String ln; while ((ln = br.readLine()) != null) { ln = ln.trim(); if (!ln.isEmpty()) addrList.add(ln); }
        }
        String[] list = addrList.toArray(new String[0]);
        String outPath = args.length > 1 ? args[1] : "funcs.txt";

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        StringBuilder sb = new StringBuilder();
        for (String s : list) {
            s = s.trim();
            if (s.isEmpty()) continue;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.parseLong(s.replace("0x", ""), 16));
            Function f = getFunctionContaining(a);
            if (f == null) {
                sb.append("\n===== ").append(s).append(" : NO FUNCTION =====\n");
                continue;
            }
            sb.append("\n===== ").append(f.getName()).append(" @ ")
              .append(f.getEntryPoint()).append(" =====\n");
            try {
                DecompileResults res = dec.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    sb.append(res.getDecompiledFunction().getC());
                } else {
                    sb.append("// decompile failed\n");
                }
            } catch (Exception e) {
                sb.append("// error: ").append(e.getMessage()).append("\n");
            }
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.print(sb.toString());
        }
        println("written: " + outPath + " (" + sb.length() + " chars)");
        dec.dispose();
    }
}
