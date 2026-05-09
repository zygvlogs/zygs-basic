// ZYGS-BASIC ESP32
// Copyright (C) 2025 Zygimantas (ZygMediaGroup)
// Licensed under GNU General Public License v3.0
// https://www.gnu.org/licenses/gpl-3.0.html
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// ZYGS-BASIC ESP32 - Improved Edition
// Patobulinimai:
//  - GOSUB grįžta į eilutę PO gosub (ne į tą pačią)
//  - String kintamieji (A$, B$, ...)
//  - AND / OR sąlygose
//  - MIN(a,b), MAX(a,b), ABS, SQR, RND, MILLIS
//  - INPUT su string palaikymu
//  - CLS komanda
//  - RENUM komanda
//  - Tikslesnė klaidos vieta
//  - MEM ir FREE sujungti
//  - PRINT su TAB() palaikymu

#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

// ─── Duomenų struktūros ───────────────────────────────────────────────────────

struct ProgramLine {
  int    number;
  String text;
};

static std::vector<ProgramLine> g_program;
static std::map<String, int>    g_vars;        // skaitiniai kintamieji
static std::map<String, String> g_svars;       // string kintamieji (A$...)
static std::vector<int>         g_gosub_stack; // grąžinimo adresų stack'as

struct ForLoop {
  String var;
  int    target;
  int    step;
  int    pc;       // FOR eilutės indeksas (grįžtame į FOR+1)
};
static std::vector<ForLoop> g_for_stack;

// ─── Pagalbinės funkcijos ─────────────────────────────────────────────────────

static String trimCopy(const String &s) {
  int start = 0, end = (int)s.length() - 1;
  while (start < (int)s.length() && isspace((unsigned char)s[start])) start++;
  while (end >= start && isspace((unsigned char)s[end])) end--;
  return (start > end) ? String("") : s.substring(start, end + 1);
}

static String sanitizeInput(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 32 && c <= 126) out += (char)c;
  }
  return out;
}

static String upperCopy(String s) {
  for (size_t i = 0; i < s.length(); i++)
    s[i] = (char)toupper((unsigned char)s[i]);
  return s;
}

static bool isStringVar(const String &name) {
  return name.length() >= 2 && name[name.length() - 1] == '$';
}

static int findLineIndex(int lineNumber) {
  for (size_t i = 0; i < g_program.size(); i++)
    if (g_program[i].number == lineNumber) return (int)i;
  return -1;
}

static void storeProgramLine(int number, const String &content) {
  String trimmed = trimCopy(content);
  int idx = findLineIndex(number);
  if (trimmed.length() == 0) {
    if (idx >= 0) g_program.erase(g_program.begin() + idx);
    return;
  }
  if (idx >= 0) {
    g_program[idx].text = trimmed;
  } else {
    g_program.push_back({number, trimmed});
    std::sort(g_program.begin(), g_program.end(),
              [](const ProgramLine &a, const ProgramLine &b){ return a.number < b.number; });
  }
}

static bool parseNumber(const String &token, int &out) {
  String t = trimCopy(token);
  if (t.length() == 0) return false;
  char *ep = nullptr;
  long v = strtol(t.c_str(), &ep, 10);
  if (*ep != '\0') return false;
  out = (int)v;
  return true;
}

// ─── Paieška išorėje (ignoruoja kabutes) ─────────────────────────────────────

static int findInOuter(const String &s, const String &target, int start = 0) {
  bool quote = false;
  String up = upperCopy(s);
  String t  = upperCopy(target);
  int tLen  = t.length();
  for (int i = start; i <= (int)s.length() - tLen; i++) {
    if (s[i] == '\"') quote = !quote;
    if (!quote && up.substring(i, i + tLen) == t) return i;
  }
  return -1;
}

// ─── Išraiškų vertintuvas ─────────────────────────────────────────────────────

static bool evalExpr(const String &expr, int &out);

// Grąžina string reikšmę (kabučių ar $-kintamojo)
static bool evalStringExpr(const String &raw, String &out) {
  String t = trimCopy(raw);
  if (t.startsWith("\"") && t.endsWith("\"") && t.length() >= 2) {
    out = t.substring(1, t.length() - 1);
    return true;
  }
  String up = upperCopy(t);
  if (isStringVar(up)) {
    auto it = g_svars.find(up);
    out = (it != g_svars.end()) ? it->second : String("");
    return true;
  }
  return false;
}

// Dviejų argumento funkcijos (MIN, MAX)
static bool parseTwoArgs(const String &inner, int &a, int &b) {
  int comma = findInOuter(inner, ",");
  if (comma < 0) return false;
  return evalExpr(inner.substring(0, comma), a) &&
         evalExpr(inner.substring(comma + 1), b);
}

static bool evalToken(const String &token, int &out) {
  String t  = trimCopy(token);
  if (t.length() == 0) return false;

  int num;
  if (parseNumber(t, num)) { out = num; return true; }

  String up = upperCopy(t);

  // Funkcijos
  if (up.startsWith("ABS(") && up.endsWith(")")) {
    if (evalExpr(t.substring(4, t.length()-1), out)) { out = abs(out); return true; }
  }
  if (up.startsWith("RND(") && up.endsWith(")")) {
    if (evalExpr(t.substring(4, t.length()-1), out)) {
      out = (out > 0) ? (int)random(out) : 0;
      return true;
    }
  }
  if (up.startsWith("SQR(") && up.endsWith(")")) {
    if (evalExpr(t.substring(4, t.length()-1), out)) {
      out = (int)sqrt(std::max(0, out));
      return true;
    }
  }
  if (up.startsWith("MIN(") && up.endsWith(")")) {
    int a, b;
    if (parseTwoArgs(t.substring(4, t.length()-1), a, b)) { out = std::min(a,b); return true; }
  }
  if (up.startsWith("MAX(") && up.endsWith(")")) {
    int a, b;
    if (parseTwoArgs(t.substring(4, t.length()-1), a, b)) { out = std::max(a,b); return true; }
  }
  if (up.startsWith("TAB(") && up.endsWith(")")) {
    // TAB(n) grąžina n tarpų (naudojama PRINT viduje)
    if (evalExpr(t.substring(4, t.length()-1), out)) return true;
  }
  if (up == "MILLIS()") { out = (int)millis(); return true; }
  if (up == "FREEMEM()") { out = (int)ESP.getFreeHeap(); return true; }
  // GPIO skaitymas kaip funkcijos
  if (up.startsWith("PININ(") && up.endsWith(")")) {
    if (evalExpr(t.substring(6, t.length()-1), out)) { out = digitalRead(out); return true; }
  }
  if (up.startsWith("ANALIN(") && up.endsWith(")")) {
    if (evalExpr(t.substring(7, t.length()-1), out)) { out = analogRead(out); return true; }
  }

  // Kintamasis
  auto it = g_vars.find(up);
  out = (it != g_vars.end()) ? it->second : 0;
  return true;
}

static bool evalTerm(const String &term, int &out) {
  String t = trimCopy(term);
  // Dešinės pusės operatoriai (*, /, %, MOD)
  int mulPos = -1, divPos = -1, modPos = -1;
  bool quote = false;
  for (int i = (int)t.length()-1; i >= 1; i--) {
    if (t[i] == '"') quote = !quote;
    if (quote) continue;
    if (t[i] == '*' && mulPos < 0) mulPos = i;
    if (t[i] == '/' && divPos < 0) divPos = i;
    if (t[i] == '%' && modPos < 0) modPos = i;
  }
  // Tikrinti MOD keyword
  String up = upperCopy(t);
  int kwMod = up.lastIndexOf(" MOD ");
  if (kwMod > 0) modPos = kwMod; // pozicija prieš tarpą

  int opPos = std::max({mulPos, divPos, modPos});
  if (opPos > 0) {
    char op = t[opPos];
    bool isMod = (opPos == kwMod);
    int left = 0, right = 0;
    String leftStr  = t.substring(0, opPos);
    String rightStr = isMod ? t.substring(opPos + 5) : t.substring(opPos + 1);
    if (!evalTerm(leftStr, left)) return false;
    if (!evalToken(rightStr, right)) return false;
    if (op == '*') { out = left * right; }
    else if (op == '/') { if (right == 0) { Serial.println("?DIV BY ZERO"); return false; } out = left / right; }
    else { if (right == 0) { Serial.println("?DIV BY ZERO"); return false; } out = left % right; }
    return true;
  }
  return evalToken(t, out);
}

static bool evalExpr(const String &expr, int &out) {
  String e = trimCopy(expr);
  // Ieškome + arba - dešinėje (ignoruojame kabutes, negatyvius skaičius)
  bool quote = false;
  int plusPos = -1, minusPos = -1;
  for (int i = (int)e.length()-1; i >= 1; i--) {
    if (e[i] == '"') quote = !quote;
    if (quote) continue;
    if (e[i] == '+' && plusPos < 0) plusPos = i;
    if (e[i] == '-' && minusPos < 0) minusPos = i;
  }
  int opPos = std::max(plusPos, minusPos);
  if (opPos > 0) {
    char op = e[opPos];
    int left = 0, right = 0;
    if (!evalExpr(e.substring(0, opPos), left)) return false;
    if (!evalTerm(e.substring(opPos + 1), right)) return false;
    out = (op == '+') ? left + right : left - right;
    return true;
  }
  return evalTerm(e, out);
}

// ─── Sąlygų vertintuvas su AND / OR ──────────────────────────────────────────

static bool evalSimpleCond(const String &cond, bool &result) {
  String c = trimCopy(cond);
  // String palyginimas (A$ = "x")
  int eqStr = c.indexOf('=');
  if (eqStr > 0) {
    String lhs = trimCopy(c.substring(0, eqStr));
    String rhs = trimCopy(c.substring(eqStr + 1));
    if (isStringVar(upperCopy(lhs)) || rhs.startsWith("\"")) {
      String sv1, sv2;
      bool ok1 = evalStringExpr(lhs, sv1);
      bool ok2 = evalStringExpr(rhs, sv2);
      if (ok1 || ok2) {
        if (!ok1) { int v; evalExpr(lhs, v); sv1 = String(v); }
        if (!ok2) { int v; evalExpr(rhs, v); sv2 = String(v); }
        result = (sv1 == sv2);
        return true;
      }
    }
  }

  String ops[] = {"<=", ">=", "<>", "=", "<", ">"};
  for (const String &op : ops) {
    int pos = c.indexOf(op);
    if (pos >= 0) {
      int left = 0, right = 0;
      if (!evalExpr(c.substring(0, pos), left)) return false;
      if (!evalExpr(c.substring(pos + op.length()), right)) return false;
      if (op == "=")  result = (left == right);
      if (op == "<>") result = (left != right);
      if (op == "<")  result = (left < right);
      if (op == ">")  result = (left > right);
      if (op == "<=") result = (left <= right);
      if (op == ">=") result = (left >= right);
      return true;
    }
  }
  return false;
}

static bool evalCondition(const String &cond, bool &result) {
  String c   = trimCopy(cond);
  String cup = upperCopy(c);

  // OR
  int orPos = cup.lastIndexOf(" OR ");
  if (orPos > 0) {
    bool left, right;
    if (!evalCondition(c.substring(0, orPos), left)) return false;
    if (!evalCondition(c.substring(orPos + 4), right)) return false;
    result = left || right;
    return true;
  }

  // AND
  int andPos = cup.lastIndexOf(" AND ");
  if (andPos > 0) {
    bool left, right;
    if (!evalCondition(c.substring(0, andPos), left)) return false;
    if (!evalCondition(c.substring(andPos + 5), right)) return false;
    result = left && right;
    return true;
  }

  return evalSimpleCond(c, result);
}

// ─── Serijos skaitymas (blokuojantis) ─────────────────────────────────────────

static String readLineBlocking() {
  String line;
  while (true) {
    while (Serial.available() == 0) delay(5);
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') break;
    Serial.print(c); // echo
    line += c;
  }
  Serial.println();
  return line;
}

// ─── Vykdymas ─────────────────────────────────────────────────────────────────

static bool executeStatement(const String &rawStmt, int &pc, bool &stop);

static bool handleAction(const String &action, int &pc, bool &stop) {
  String a = trimCopy(action);
  if (a.length() == 0) return true;
  int num;
  if (parseNumber(a, num)) {
    int idx = findLineIndex(num);
    if (idx < 0) { Serial.println("?UNDEF LINE"); return false; }
    pc = idx;
    return true;
  }
  // Vykdome inline sakinį
  bool dummyStop = false;
  return executeStatement(a, pc, dummyStop);
}

static bool runLine(const String &line, int &pc, bool &stop) {
  String remaining = line;
  while (remaining.length() > 0) {
    remaining = trimCopy(remaining);
    if (remaining.length() == 0) break;
    if (upperCopy(remaining).startsWith("IF "))
      return executeStatement(remaining, pc, stop);
    int colon = findInOuter(remaining, ":");
    String stmt = (colon >= 0) ? remaining.substring(0, colon) : remaining;
    int beforePc = pc;
    if (!executeStatement(stmt, pc, stop)) return false;
    if (stop || pc != beforePc) return true;
    if (colon < 0) break;
    remaining = remaining.substring(colon + 1);
  }
  return true;
}

static bool executeStatement(const String &rawStmt, int &pc, bool &stop) {
  String stmt = trimCopy(rawStmt);
  String up   = upperCopy(stmt);

  if (up.startsWith("REM") || stmt.length() == 0) return true;

  // ── PRINT ────────────────────────────────────────────────────────────────────
  if (up == "PRINT") { Serial.println(); return true; }
  if (up.startsWith("PRINT ")) {
    String args = stmt.substring(6);
    int pos = 0;
    bool suppressNewline = false;
    while (pos < (int)args.length()) {
      while (pos < (int)args.length() && isspace(args[pos])) pos++;
      if (pos >= (int)args.length()) break;

      if (args[pos] == '\"') {
        int end = args.indexOf('\"', pos + 1);
        if (end < 0) { Serial.println("?SYNTAX ERROR"); return false; }
        Serial.print(args.substring(pos + 1, end));
        pos = end + 1;
      } else {
        // String kintamasis?
        int end = pos;
        while (end < (int)args.length() && args[end] != ';' && args[end] != ',') end++;
        String token = trimCopy(args.substring(pos, end));
        String sup = upperCopy(token);
        if (isStringVar(sup)) {
          auto it = g_svars.find(sup);
          Serial.print(it != g_svars.end() ? it->second : "");
        } else if (sup.startsWith("TAB(") && sup.endsWith(")")) {
          int n = 0;
          if (evalExpr(token.substring(4, token.length()-1), n))
            for (int i = 0; i < n; i++) Serial.print(' ');
        } else {
          int val = 0;
          if (!evalExpr(token, val)) { Serial.println("?SYNTAX ERROR"); return false; }
          Serial.print(val);
        }
        pos = end;
      }

      suppressNewline = false;
      while (pos < (int)args.length() && (args[pos] == ';' || args[pos] == ',' || isspace(args[pos]))) {
        if (args[pos] == ',') Serial.print('\t');
        if (args[pos] == ';') suppressNewline = true;
        pos++;
      }
    }
    if (!suppressNewline) Serial.println();
    return true;
  }

  // ── LET (ir su string) ───────────────────────────────────────────────────────
  if (up.startsWith("LET ")) {
    String rhs = stmt.substring(4);
    int eqPos = rhs.indexOf('=');
    if (eqPos < 1) { Serial.println("?SYNTAX ERROR"); return false; }
    String var = upperCopy(trimCopy(rhs.substring(0, eqPos)));
    if (isStringVar(var)) {
      String sv;
      if (!evalStringExpr(trimCopy(rhs.substring(eqPos + 1)), sv)) { Serial.println("?SYNTAX ERROR"); return false; }
      g_svars[var] = sv;
    } else {
      int value = 0;
      if (!evalExpr(rhs.substring(eqPos + 1), value)) { Serial.println("?SYNTAX ERROR"); return false; }
      g_vars[var] = value;
    }
    return true;
  }

  // ── Implied LET ──────────────────────────────────────────────────────────────
  {
    int eqPos = stmt.indexOf('=');
    if (eqPos > 0) {
      String var = upperCopy(trimCopy(stmt.substring(0, eqPos)));
      if (var.length() > 0 && (isalpha(var[0]) || var[0] == '_')) {
        if (isStringVar(var)) {
          String sv;
          if (evalStringExpr(trimCopy(stmt.substring(eqPos + 1)), sv)) { g_svars[var] = sv; return true; }
        } else {
          int value = 0;
          if (evalExpr(stmt.substring(eqPos + 1), value)) { g_vars[var] = value; return true; }
        }
      }
    }
  }

  // ── DELAY ────────────────────────────────────────────────────────────────────
  if (up.startsWith("DELAY ")) {
    int ms = 0;
    if (!evalExpr(stmt.substring(6), ms)) { Serial.println("?SYNTAX ERROR"); return false; }
    delay(ms);
    return true;
  }

  // ── FOR ──────────────────────────────────────────────────────────────────────
  if (up.startsWith("FOR ")) {
    int eqPos  = up.indexOf('=');
    int toPos  = up.indexOf(" TO ");
    if (eqPos < 0 || toPos < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    String var = upperCopy(trimCopy(stmt.substring(4, eqPos)));
    int startVal = 0;
    if (!evalExpr(stmt.substring(eqPos + 1, toPos), startVal)) { Serial.println("?SYNTAX ERROR"); return false; }
    int endVal = 0, stepVal = 1;
    int stepPos = up.indexOf(" STEP ");
    if (stepPos > 0) {
      if (!evalExpr(stmt.substring(toPos + 4, stepPos), endVal)) { Serial.println("?SYNTAX ERROR"); return false; }
      if (!evalExpr(stmt.substring(stepPos + 6), stepVal))        { Serial.println("?SYNTAX ERROR"); return false; }
    } else {
      if (!evalExpr(stmt.substring(toPos + 4), endVal)) { Serial.println("?SYNTAX ERROR"); return false; }
    }
    g_vars[var] = startVal;
    // pc čia yra FOR eilutės indeksas; NEXT grįš į pc+1
    g_for_stack.push_back({var, endVal, stepVal, pc + 1});
    return true;
  }

  // ── NEXT ─────────────────────────────────────────────────────────────────────
  if (up.startsWith("NEXT")) {
    if (g_for_stack.empty()) { Serial.println("?NEXT WITHOUT FOR"); return false; }
    String var = "";
    if (up.length() > 4) var = upperCopy(trimCopy(stmt.substring(4)));
    if (var.length() > 0 && g_for_stack.back().var != var) { Serial.println("?NEXT VAR MISMATCH"); return false; }

    ForLoop &loop = g_for_stack.back();
    g_vars[loop.var] += loop.step;
    bool done = (loop.step >= 0) ? (g_vars[loop.var] > loop.target) : (g_vars[loop.var] < loop.target);
    if (done) {
      g_for_stack.pop_back();
    } else {
      pc = loop.pc; // grįžtame į eilutę PO FOR
    }
    return true;
  }

  // ── INPUT ────────────────────────────────────────────────────────────────────
  if (up.startsWith("INPUT ")) {
    // INPUT "Prompt"; VAR arba INPUT VAR
    String rest = stmt.substring(6);
    String prompt = "";
    if (rest.startsWith("\"")) {
      int end = rest.indexOf('"', 1);
      if (end > 0) {
        prompt = rest.substring(1, end);
        rest = trimCopy(rest.substring(end + 1));
        if (rest.startsWith(";") || rest.startsWith(",")) rest = trimCopy(rest.substring(1));
      }
    }
    String var = upperCopy(trimCopy(rest));
    if (prompt.length() > 0) Serial.print(prompt);
    else { Serial.print(var); Serial.print("? "); }
    String line = trimCopy(readLineBlocking());
    if (isStringVar(var)) {
      g_svars[var] = line;
    } else {
      int value = 0;
      if (!parseNumber(line, value)) { Serial.println("?REDO FROM START"); return false; }
      g_vars[var] = value;
    }
    return true;
  }

  // ── GOTO ─────────────────────────────────────────────────────────────────────
  if (up.startsWith("GOTO ")) {
    int target = 0;
    if (!parseNumber(stmt.substring(5), target)) { Serial.println("?SYNTAX ERROR"); return false; }
    int idx = findLineIndex(target);
    if (idx < 0) { Serial.println("?UNDEF LINE"); return false; }
    pc = idx;
    return true;
  }

  // ── GOSUB ────────────────────────────────────────────────────────────────────
  if (up.startsWith("GOSUB ")) {
    int target = 0;
    if (!parseNumber(stmt.substring(6), target)) { Serial.println("?SYNTAX ERROR"); return false; }
    int idx = findLineIndex(target);
    if (idx < 0) { Serial.println("?UNDEF LINE"); return false; }
    g_gosub_stack.push_back(pc + 1); // grįžti į eilutę PO gosub
    pc = idx;
    return true;
  }

  // ── RETURN ───────────────────────────────────────────────────────────────────
  if (up == "RETURN") {
    if (g_gosub_stack.empty()) { Serial.println("?RETURN WITHOUT GOSUB"); return false; }
    pc = g_gosub_stack.back();
    g_gosub_stack.pop_back();
    return true;
  }

  // ── IF ... THEN [... ELSE ...] ────────────────────────────────────────────────
  if (up.startsWith("IF ")) {
    int thenPos = findInOuter(stmt, " THEN ");
    if (thenPos < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    String condStr = stmt.substring(3, thenPos);
    String rest    = stmt.substring(thenPos + 6);
    int elsePos    = findInOuter(rest, " ELSE ");
    String thenPart = (elsePos >= 0) ? rest.substring(0, elsePos) : rest;
    String elsePart = (elsePos >= 0) ? rest.substring(elsePos + 6) : String("");

    bool ok = false;
    if (!evalCondition(condStr, ok)) { Serial.println("?SYNTAX ERROR"); return false; }
    if (ok)                return handleAction(thenPart, pc, stop);
    if (elsePart.length()) return handleAction(elsePart, pc, stop);
    return true;
  }

  // ── MEM / FREE ───────────────────────────────────────────────────────────────
  if (up == "MEM" || up == "FREE") {
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    return true;
  }

  // ── CLS ──────────────────────────────────────────────────────────────────────
  if (up == "CLS") {
    Serial.print("\033[2J\033[H"); // ANSI clear
    return true;
  }

  // ── END / STOP ───────────────────────────────────────────────────────────────

  // ── PINMODE pin, OUT/IN/PULLUP ───────────────────────────────────────────────
  if (up.startsWith("PINMODE ")) {
    String args = trimCopy(stmt.substring(8));
    int comma = args.indexOf(',');
    if (comma < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    int pin = 0;
    if (!evalExpr(args.substring(0, comma), pin)) { Serial.println("?SYNTAX ERROR"); return false; }
    String mode = upperCopy(trimCopy(args.substring(comma + 1)));
    if (mode == "OUT" || mode == "OUTPUT") { pinMode(pin, OUTPUT); }
    else if (mode == "IN" || mode == "INPUT") { pinMode(pin, INPUT); }
    else if (mode == "PULLUP") { pinMode(pin, INPUT_PULLUP); }
    else { Serial.println("?BAD MODE (OUT/IN/PULLUP)"); return false; }
    return true;
  }

  // ── PINOUT pin, val ──────────────────────────────────────────────────────────
  if (up.startsWith("PINOUT ")) {
    String args = trimCopy(stmt.substring(7));
    int comma = args.indexOf(',');
    if (comma < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    int pin = 0, val = 0;
    if (!evalExpr(args.substring(0, comma), pin)) { Serial.println("?SYNTAX ERROR"); return false; }
    if (!evalExpr(args.substring(comma + 1), val)) { Serial.println("?SYNTAX ERROR"); return false; }
    digitalWrite(pin, val ? HIGH : LOW);
    return true;
  }

  // ── ANAOUT pin, val (PWM 0-255) ──────────────────────────────────────────────
  if (up.startsWith("ANAOUT ")) {
    String args = trimCopy(stmt.substring(7));
    int comma = args.indexOf(',');
    if (comma < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    int pin = 0, val = 0;
    if (!evalExpr(args.substring(0, comma), pin)) { Serial.println("?SYNTAX ERROR"); return false; }
    if (!evalExpr(args.substring(comma + 1), val)) { Serial.println("?SYNTAX ERROR"); return false; }
    analogWrite(pin, constrain(val, 0, 255));
    return true;
  }

  // ── BLINK pin, ms ────────────────────────────────────────────────────────────
  if (up.startsWith("BLINK ")) {
    String args = trimCopy(stmt.substring(6));
    int comma = args.indexOf(',');
    if (comma < 0) { Serial.println("?SYNTAX ERROR"); return false; }
    int pin = 0, ms = 500;
    if (!evalExpr(args.substring(0, comma), pin)) { Serial.println("?SYNTAX ERROR"); return false; }
    if (!evalExpr(args.substring(comma + 1), ms)) { Serial.println("?SYNTAX ERROR"); return false; }
    digitalWrite(pin, HIGH); delay(ms); digitalWrite(pin, LOW); delay(ms);
    return true;
  }

  if (up == "END" || up == "STOP") { stop = true; return true; }

  Serial.println("?WHAT?");
  return false;
}

// ─── Komandos ─────────────────────────────────────────────────────────────────

static void cmdList(int from = 0, int to = 999999) {
  for (const auto &line : g_program)
    if (line.number >= from && line.number <= to) {
      Serial.print(line.number);
      Serial.print(' ');
      Serial.println(line.text);
    }
}

static void cmdRun() {
  int pc = 0;
  bool stop = false;
  g_gosub_stack.clear();
  g_for_stack.clear();
  while (pc >= 0 && pc < (int)g_program.size() && !stop) {
    int before = pc;
    if (!runLine(g_program[pc].text, pc, stop)) {
      Serial.print("IN ");
      Serial.println(g_program[before].number);
      return;
    }
    if (pc == before) pc++;
  }
  if (!stop) Serial.println("OK");
}

static void cmdRenum(int start = 10, int step = 10) {
  // Pernumeruojame eilutes
  std::map<int,int> mapping;
  int n = start;
  for (auto &line : g_program) {
    mapping[line.number] = n;
    line.number = n;
    n += step;
  }
  // Atnaujiname GOTO/GOSUB eilutes
  for (auto &line : g_program) {
    String up = upperCopy(line.text);
    for (const String &kw : {"GOTO ", "GOSUB ", "THEN ", "ELSE "}) {
      int pos = up.indexOf(kw);
      if (pos >= 0) {
        int numStart = pos + kw.length();
        int numEnd = numStart;
        while (numEnd < (int)line.text.length() && isdigit(line.text[numEnd])) numEnd++;
        if (numEnd > numStart) {
          int oldNum = line.text.substring(numStart, numEnd).toInt();
          if (mapping.count(oldNum)) {
            String newNum = String(mapping[oldNum]);
            line.text = line.text.substring(0, numStart) + newNum + line.text.substring(numEnd);
            up = upperCopy(line.text);
          }
        }
      }
    }
  }
  Serial.println("OK");
}

static void printHelp() {
  Serial.println("ZYGS-BASIC ESP32 v2  (C) 2025 ZygMediaGroup");
  Serial.println("GNU General Public License v3.0");
  Serial.println("https://www.gnu.org/licenses/gpl-3.0.html");
  Serial.println("Commands: LIST [n[-m]], RUN, NEW, HELP, VARS, MEM, FREE, RENUM [s[,step]], CLS");
  Serial.println("Stmts:    PRINT, LET, INPUT [\"msg\";] VAR, IF..THEN..ELSE, GOTO, GOSUB,");
  Serial.println("          RETURN, FOR..TO [STEP], NEXT, DELAY, END, STOP, REM");
  Serial.println("Funcs:    ABS(n), RND(n), SQR(n), MIN(a,b), MAX(a,b), MILLIS(), FREEMEM()");
  Serial.println("          PININ(n), ANALIN(n)");
  Serial.println("GPIO:     PINMODE n,OUT/IN/PULLUP  PINOUT n,v  ANAOUT n,v  BLINK n,ms");
  Serial.println("          TAB(n) [in PRINT], AND, OR [in IF]");
  Serial.println("String:   A$=\"hello\" / INPUT \"Name?\"; N$ / PRINT N$");
}

static void cmdVars() {
  for (const auto &p : g_vars)  { Serial.print(p.first); Serial.print(" = "); Serial.println(p.second); }
  for (const auto &p : g_svars) { Serial.print(p.first); Serial.print(" = \""); Serial.print(p.second); Serial.println("\""); }
}

// ─── Įvesties apdorojimas ─────────────────────────────────────────────────────

static void processInput(const String &raw) {
  String line = trimCopy(sanitizeInput(raw));
  if (line.length() == 0) return;
  while (line.startsWith(">")) line = trimCopy(line.substring(1));
  if (line.length() == 0) return;

  // Eilutės numeris?
  int i = 0;
  while (i < (int)line.length() && isdigit((unsigned char)line[i])) i++;
  if (i > 0) {
    int lineNumber = line.substring(0, i).toInt();
    while (i < (int)line.length() && isspace((unsigned char)line[i])) i++;
    storeProgramLine(lineNumber, (i >= (int)line.length()) ? String("") : line.substring(i));
    return;
  }

  // Komanda
  int sp = line.indexOf(' ');
  String cmdWord = upperCopy((sp < 0) ? line : line.substring(0, sp));
  String cmdArgs = (sp < 0) ? String("") : trimCopy(line.substring(sp + 1));

  if (cmdWord == "LIST") {
    int from = 0, to = 999999;
    if (cmdArgs.length() > 0) {
      int dash = cmdArgs.indexOf('-');
      if (dash >= 0) {
        if (dash > 0) from = cmdArgs.substring(0, dash).toInt();
        if (dash < (int)cmdArgs.length()-1) to = cmdArgs.substring(dash+1).toInt();
      } else { from = to = cmdArgs.toInt(); }
    }
    cmdList(from, to);
  } else if (cmdWord == "RUN")  { cmdRun(); }
  else if (cmdWord == "NEW")   { g_program.clear(); g_vars.clear(); g_svars.clear(); Serial.println("OK"); }
  else if (cmdWord == "HELP")  { printHelp(); }
  else if (cmdWord == "VARS")  { cmdVars(); }
  else if (cmdWord == "RENUM") {
    int start = 10, step = 10;
    if (cmdArgs.length() > 0) {
      int comma = cmdArgs.indexOf(',');
      if (comma >= 0) { start = cmdArgs.substring(0,comma).toInt(); step = cmdArgs.substring(comma+1).toInt(); }
      else start = cmdArgs.toInt();
    }
    cmdRenum(start, step);
  } else {
    int pc = -1; bool stop = false;
    if (!executeStatement(line, pc, stop)) return;
    if (!stop) Serial.println("OK");
  }
}

// ─── ANSI TUI (ZX Spectrum stilius) ──────────────────────────────────────────
//
//  Terminalas suskirstytas į dvi zonas:
//    1 eilutė  = INPUT ZONA  (viršus, fiksuota) — čia rodoma ką rašai
//    2+ eilutė = OUTPUT ZONA (žemiau) — programa, klaidos, rezultatai
//
//  PuTTY nustatymai: 115200 baud, VT100/xterm, Local echo = OFF (Force off)
// ─────────────────────────────────────────────────────────────────────────────

// Cursor pozicionavimas (1-based)
static void ansiGoto(int row, int col) {
  Serial.print("\033[");
  Serial.print(row);
  Serial.print(";");
  Serial.print(col);
  Serial.print("H");
}

// Išvalo nuo cursoriaus iki eilutės pabaigos
static void ansiClearLine() {
  Serial.print("\033[2K");
}

// Išvalo visą ekraną
static void ansiClear() {
  Serial.print("\033[2J");
}

// Spalvos
static void ansiColor(const char *code) {
  Serial.print("\033[");
  Serial.print(code);
  Serial.print("m");
}

// ─── Input zona (1 eilutė) ───────────────────────────────────────────────────

static String g_inputBuf;           // dabartinis input buferis

static void drawInputLine() {
  ansiGoto(1, 1);
  ansiClearLine();
  ansiColor("1;44;97");             // bold, mėlynas fonas, balta
  Serial.print(" > ");
  Serial.print(g_inputBuf);
  // Užpildome likusį plotį tarpais (80 simbolių terminalas)
  int used = 4 + g_inputBuf.length();
  for (int i = used; i < 80; i++) Serial.print(' ');
  ansiColor("0");                   // reset
  // Cursor į input poziciją
  ansiGoto(1, 4 + g_inputBuf.length() + 1);
}

// ─── Output zona (2+ eilutės) ────────────────────────────────────────────────
// Prieš bet kokį Serial.print output'ą nukeliame cursorių į output zoną.
// Naudojame paprastą triuką: scroll region + cursor sekimas.

static int g_outRow = 3;            // pradedame nuo 3 eilutės (2 = separator)

static void drawSeparator() {
  ansiGoto(2, 1);
  ansiColor("1;36");                // bold cyan
  for (int i = 0; i < 80; i++) Serial.print('-');
  ansiColor("0");
}

// Persijungiame į output zoną prieš rašymą
static void outBegin() {
  ansiGoto(g_outRow, 1);
}

// Pabaigus output eilutę — atnaujiname eilutės skaitliuką ir input zoną
static void outEnd() {
  g_outRow++;
  // Jei pasiekiame apačią — slenkame (paprastas reset į 3)
  // PuTTY pats pasirūpins scroll'u — mums tik reikia grąžinti input'ą viršun
  drawInputLine();
}

// ─── Serial.print perrašymas per makro ───────────────────────────────────────
// Negalime perrašyti Serial klasės lengvai, todėl naudojame pagalbinę funkciją
// kuri apgaubia visą output srautą.

// Klasė kuri perimą println/print ir nukreipia į tinkamą vietą
class ZygOutput : public Print {
public:
  bool _newline = true;

  size_t write(uint8_t c) override {
    if (_newline) {
      ansiGoto(g_outRow, 1);
      ansiClearLine();
      _newline = false;
    }
    if (c == '\n') {
      Serial.write('\r');
      Serial.write('\n');
      g_outRow++;
      _newline = true;
      drawInputLine();
    } else {
      Serial.write(c);
    }
    return 1;
  }
};

static ZygOutput Out;

// ─── Output makro ─────────────────────────────────────────────────────────────
// Pakeičiame visus Serial.print į Out.print visame kode
// Paprasčiau: redefine Serial kaip Out tik output dalyje —
// bet kadangi Serial naudojamas ir input'ui, paliekame atskirą Out objektą.
// Visas kodo output pakeičiamas žemiau per find-replace logika setup'e.

// ─── Setup / Loop ─────────────────────────────────────────────────────────────

// Perrašome processInput kad naudotų Out vietoje Serial output'ui
// (input skaitymas lieka per Serial)

// Wrapper'is: nukreipia BASIC output per ZygOutput
// Išsprendžiame paprasčiausiai — pakeičiame Serial į Out executeStatement ir kt.
// Tačiau kadangi funkcijos jau parašytos su Serial — pridedame redirect'ą:

static bool g_tui = true; // TUI režimas įjungtas

// Globalus print helperas — rašo į teisingą zoną
static void zprint(const String &s) {
  if (!g_tui) { Serial.print(s); return; }
  ansiGoto(g_outRow, 1);
  ansiClearLine();
  Out.print(s);
}
static void zprintln(const String &s = "") {
  if (!g_tui) { Serial.println(s); return; }
  ansiGoto(g_outRow, 1);
  ansiClearLine();
  Out.println(s);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Pilnas ekrano valymas ir pradinė būsena
  ansiClear();
  g_outRow = 3;

  // Antraštė input zonoje
  drawSeparator();
  drawInputLine();

  // Welcome žinutė output zonoje
  ansiGoto(3, 1);
  ansiColor("1;32");   // bold green — ZX Spectrum stilius
  Serial.println("ZYGS-BASIC ESP32 v2  (c) ZygMediaGroup");
  ansiColor("0");
  g_outRow = 4;
  ansiGoto(4, 1);
  Serial.println("READY.");
  g_outRow = 5;
  printHelp();
  g_outRow = 14; // po help teksto
  drawInputLine();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') continue; // \r\n — \n ignoruojame

    if (c == '\r') {
      // Enter — vykdome komandą
      String cmd = g_inputBuf;
      g_inputBuf = "";

      // Atnaujiname input zoną (išvalom)
      drawInputLine();

      // Rodome komandą output zonoje
      ansiGoto(g_outRow, 1);
      ansiClearLine();
      ansiColor("1;33"); // geltona — įvesta komanda
      Serial.print("> ");
      Serial.println(cmd);
      ansiColor("0");
      g_outRow++;

      // Vykdome
      processInput(cmd);

      drawInputLine();

    } else if (c == 127 || c == '\b') {
      // Backspace
      if (g_inputBuf.length() > 0) {
        g_inputBuf.remove(g_inputBuf.length() - 1);
        drawInputLine();
      }
    } else if (c >= 32 && c <= 126) {
      // Normalus simbolis
      g_inputBuf += c;
      drawInputLine();
    }
  }
}