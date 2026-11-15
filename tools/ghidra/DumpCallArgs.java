// DumpCallArgs.java —— 扫描所有对 UI_Get_Value / UI_Set_Value 的调用，
// 提取调用前的 r0 常量（即 eUI_VALUE_ITEM 索引）和所在函数名。
//
// 这是把「索引 -> 语义」对上号的关键：每个调用点都对应一个具体设置。

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class DumpCallArgs extends GhidraScript {

    private static final Pattern R0_CONST =
            Pattern.compile("r0,\\s*(?:#)?0x([0-9a-fA-F]+)");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "call_args.txt";
        String[] targets = { "168618", "168758" };   // UI_Get_Value / UI_Set_Value

        StringBuilder sb = new StringBuilder();
        int total = 0;

        for (String t : targets) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.parseLong(t, 16));
            Function fn = getFunctionContaining(a);
            sb.append("# target ").append(fn == null ? t : fn.getName())
              .append(" @ ").append(a).append("\n");

            for (Reference r : getReferencesTo(a)) {
                if (!r.getReferenceType().isCall()) continue;
                Address from = r.getFromAddress();
                Instruction ins = getInstructionAt(from);
                if (ins == null) continue;

                // 往前找 r0 的常量赋值
                long val = -1;
                boolean viaMovt = false;
                Instruction cur = ins;
                for (int i = 0; i < 10 && cur != null; i++) {
                    String s = cur.toString();
                    Matcher m = R0_CONST.matcher(s);
                    if (m.find()) {
                        val = Long.parseLong(m.group(1), 16);
                        break;
                    }
                    if (s.contains("movt") && s.contains("r0")) viaMovt = true;
                    cur = cur.getPrevious();
                }

                Function owner = getFunctionContaining(from);
                String ownerName = owner != null ? owner.getName() : "(nofunc)";
                sb.append(String.format("0x%s  idx=0x%x%s  %s%n",
                        from.toString(), val, viaMovt ? "(movt)" : "", ownerName));
                total++;
            }
            sb.append("\n");
        }
        sb.insert(0, "# total call sites: " + total + "\n\n");

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.print(sb.toString());
        }
        println("written: " + outPath + " (" + sb.length() + " chars, " + total + " sites)");
    }
}
