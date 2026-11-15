// DumpAccessor.java —— 反编译"字段访问器"以及它的所有调用方
//
// 目的：di-camera-app 的设置字段都通过 GetSettingPtr(index) 间接访问，
// 每个调用方会传入常量索引。把所有调用方反编译出来，
// 就能看到 "函数名 + 索引" 的对应关系，从而给字段定语义。
//
// 用法（headless）：
//   analyzeHeadless <proj> nx300 -import di-camera-app-nx300 \
//       -postScript DumpAccessor.java <accessor_addr_hex> <out.txt> \
//       -scriptPath <this dir> -deleteProject

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class DumpAccessor extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String addrStr = args.length > 0 ? args[0] : "1565c8";
        String outPath = args.length > 1 ? args[1] : "accessor_dump.txt";

        Address probe = currentProgram.getAddressFactory()
                .getDefaultAddressSpace().getAddress(Long.parseLong(addrStr, 16));
        Function accessor = getFunctionContaining(probe);
        if (accessor == null) {
            // 没有现成函数就从该地址强制创建一个
            accessor = createFunction(probe, "GetSettingPtr");
        }
        println("accessor = " + accessor.getName() + " @ " + accessor.getEntryPoint());

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        StringBuilder sb = new StringBuilder();
        sb.append("ACCESSOR ").append(accessor.getName())
          .append(" @ ").append(accessor.getEntryPoint()).append("\n");

        // 1) 访问器自身反编译
        sb.append("\n===== ACCESSOR BODY =====\n");
        sb.append(decompile(dec, accessor));

        // 2) 所有调用方
        Set<Function> callers = new LinkedHashSet<>();
        for (Reference r : getReferencesTo(accessor.getEntryPoint())) {
            if (!r.getReferenceType().isCall()) continue;
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) callers.add(f);
        }
        sb.append("\n===== CALLERS: ").append(callers.size()).append(" =====\n");

        int n = 0;
        for (Function f : callers) {
            n++;
            sb.append("\n--- [").append(n).append("] ").append(f.getName())
              .append(" @ ").append(f.getEntryPoint()).append("\n");
            sb.append(decompile(dec, f));
            if (n >= 400) {
                sb.append("\n(truncated at 400 callers)\n");
                break;
            }
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.print(sb.toString());
        }
        println("written: " + outPath + " (" + sb.length() + " chars)");
        dec.dispose();
    }

    private String decompile(DecompInterface dec, Function f) {
        try {
            DecompileResults res = dec.decompileFunction(f, 60, monitor);
            if (res == null || !res.decompileCompleted())
                return "// decompile failed\n";
            return res.getDecompiledFunction().getC();
        } catch (Exception e) {
            return "// decompile error: " + e.getMessage() + "\n";
        }
    }
}
