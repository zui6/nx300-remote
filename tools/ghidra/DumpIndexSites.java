// DumpIndexSites.java —— 找出所有 "带常量索引" 调用设置访问器的地方
//
// UI_Get_Value_Capture_Menu(index) / UI_Set_Value_Capture_Menu(index, value)
// 的每个调用点都会传入一个常量 eUI_VALUE_ITEM，把它和所在函数名一起打印出来，
// 就能得到「函数名 -> 索引」的对应关系，从而给 203 个字段定语义。

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class DumpIndexSites extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "index_sites.txt";
        // 反编译这些函数，提取对访问器的调用行
        String[] targets = { "1547e8", "154834", "15623c" };

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        StringBuilder sb = new StringBuilder();
        Set<Function> callers = new TreeSet<>(Comparator.comparing(
                f -> f.getEntryPoint().toString()));

        for (String t : targets) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.parseLong(t, 16));
            Function fn = getFunctionContaining(a);
            if (fn == null) continue;
            sb.append("# target ").append(fn.getName())
              .append(" @ ").append(fn.getEntryPoint()).append("\n");
            for (Reference r : getReferencesTo(fn.getEntryPoint())) {
                if (!r.getReferenceType().isCall()) continue;
                Function c = getFunctionContaining(r.getFromAddress());
                if (c != null) callers.add(c);
            }
        }
        sb.append("# distinct caller functions: ").append(callers.size()).append("\n\n");

        int n = 0;
        for (Function f : callers) {
            n++;
            String code;
            try {
                DecompileResults res = dec.decompileFunction(f, 90, monitor);
                if (res == null || !res.decompileCompleted()) continue;
                code = res.getDecompiledFunction().getC();
            } catch (Exception e) {
                continue;
            }
            // 只保留含访问器调用的行 + 函数头
            List<String> keep = new ArrayList<>();
            String[] lines = code.split("\n");
            boolean any = false;
            for (String ln : lines) {
                if (ln.contains("UI_Get_Value_Capture_Menu")
                        || ln.contains("UI_Set_Value_Capture_Menu")
                        || ln.contains("SetTemporaryUserData")
                        || ln.contains("UI_Get_DB_ID_Capture_Menu")) {
                    keep.add(ln.trim());
                    any = true;
                }
            }
            if (!any) continue;
            sb.append("--- [").append(n).append("] ").append(f.getName())
              .append(" @ ").append(f.getEntryPoint()).append("\n");
            for (String ln : keep) sb.append("    ").append(ln).append("\n");
            sb.append("\n");
            if (n > 1200) { sb.append("(truncated)\n"); break; }
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.print(sb.toString());
        }
        println("written: " + outPath + " (" + sb.length() + " chars)");
        dec.dispose();
    }
}
