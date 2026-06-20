# -*- coding: utf-8 -*-
"""MiniChess AI demo cheat-sheet -> portrait A4 PDF (Traditional Chinese)."""
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import cm
from reportlab.lib.colors import HexColor, white
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Preformatted, Spacer,
                                Table, TableStyle, KeepTogether, HRFlowable)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

# ---- fonts ----
pdfmetrics.registerFont(TTFont("JH", r"C:\Windows\Fonts\msjh.ttc", subfontIndex=0))
pdfmetrics.registerFont(TTFont("JHB", r"C:\Windows\Fonts\msjhbd.ttc", subfontIndex=0))
pdfmetrics.registerFontFamily("JH", normal="JH", bold="JHB", italic="JH", boldItalic="JHB")

NAVY = HexColor("#1E2761")
BLUE = HexColor("#3B5BA9")
AMBER = HexColor("#B8810A")
AMBERBG = HexColor("#FFF6E0")
TEXT = HexColor("#1A2238")
MUTED = HexColor("#5B6577")
CODEBG = HexColor("#EEF1F7")
GREEN = HexColor("#1F7A45")
RED = HexColor("#B23A2E")
GREY = HexColor("#C9D0DE")

styles = {}
styles["title"] = ParagraphStyle("title", fontName="JHB", fontSize=18, textColor=NAVY,
                                 alignment=TA_CENTER, leading=22, spaceAfter=2)
styles["sub"] = ParagraphStyle("sub", fontName="JH", fontSize=9.5, textColor=MUTED,
                               alignment=TA_CENTER, leading=13, spaceAfter=6)
styles["sec"] = ParagraphStyle("sec", fontName="JHB", fontSize=12.5, textColor=white,
                               backColor=NAVY, leading=17, spaceBefore=10, spaceAfter=5,
                               borderPadding=(4, 6, 4, 6), leftIndent=0)
styles["h"] = ParagraphStyle("h", fontName="JHB", fontSize=10.5, textColor=NAVY,
                             leading=14, spaceBefore=5, spaceAfter=2)
styles["body"] = ParagraphStyle("body", fontName="JH", fontSize=9.5, textColor=TEXT,
                                leading=13.5, spaceAfter=2)
styles["bullet"] = ParagraphStyle("bullet", fontName="JH", fontSize=9.5, textColor=TEXT,
                                  leading=13.5, spaceAfter=1, leftIndent=12, bulletIndent=2)
styles["say"] = ParagraphStyle("say", fontName="JH", fontSize=9.5, textColor=TEXT,
                               backColor=AMBERBG, leading=13.5, borderPadding=(4, 6, 4, 6),
                               spaceBefore=2, spaceAfter=4)
styles["code"] = ParagraphStyle("code", fontName="JH", fontSize=8.3, textColor=HexColor("#102A54"),
                                backColor=CODEBG, leading=11.2, borderPadding=(5, 6, 5, 6),
                                spaceBefore=2, spaceAfter=4)
styles["qa"] = ParagraphStyle("qa", fontName="JH", fontSize=9.3, textColor=TEXT,
                              leading=13, spaceAfter=3, leftIndent=2)

story = []

def sec(t):
    story.append(Paragraph(t, styles["sec"]))

def h(t):
    story.append(Paragraph(t, styles["h"]))

def p(t):
    story.append(Paragraph(t, styles["body"]))

def b(t):
    story.append(Paragraph("•&nbsp;&nbsp;" + t, styles["bullet"]))

def say(t):
    story.append(Paragraph('<font name="JHB" color="#B8810A">這樣講:</font> ' + t, styles["say"]))

def code(lines):
    story.append(Preformatted("\n".join(lines), styles["code"]))

def qa(q, a):
    story.append(Paragraph('<font name="JHB" color="#1E2761">Q:</font> ' + q, styles["qa"]))
    story.append(Paragraph('<font name="JHB" color="#3B5BA9">A:</font> ' + a, styles["qa"]))

def sp(x=4):
    story.append(Spacer(1, x))

def block(flowables):
    story.append(KeepTogether(flowables))

# ============================================================ Header
story.append(Paragraph("MiniChess AI — Demo 速查小抄", styles["title"]))
story.append(Paragraph("PVS 搜尋引擎 · 對 Weak / Strong / Boss 全勝(10:0)· "
                       "可編輯範圍:state.cpp、state.hpp、src/policy/*", styles["sub"]))
story.append(HRFlowable(width="100%", thickness=1, color=GREY, spaceAfter=4))

# ============================================================ 0 開場必講
sec("0. 開場必講(先背這兩段)")
p('<font name="JHB" color="#1E2761">一句話總結:</font>「我實作一個以 <b>negamax</b> 為基礎、'
  '採用<b>迭代加深</b>的 <b>PVS(主變例搜尋)</b>引擎;核心是 <b>alpha-beta + PVS</b>,'
  '搭配<b>置換表</b>、<b>走法排序</b>、<b>靜止搜尋</b>,以及<b>延伸/縮減/裁剪</b>等選擇性深度技術,'
  '最後餵給結合<b>材料、位置、王安全、兵形</b>的評估函數。」')
p('<font name="JHB" color="#B23A2E">最大優化(幾乎必問):</font>「把<b>根節點最佳走法存入置換表</b>。'
  '原本限時搜尋被中斷時會回報一個會輸的吃子;修正後每層都先回報真正最佳手,'
  '對 Weak 由 <b>40% 提升到 100%</b>。」')

# ============================================================ 1 架構
sec("1. 整體架構(分層強化)")
b('<b>骨架</b>:Negamax + 迭代加深(Iterative Deepening)')
b('<b>核心剪枝</b>:Alpha-Beta → PVS')
b('<b>加速結構</b>:置換表(TT)、走法排序、靜止搜尋')
b('<b>選擇性深度</b>:將軍延伸、LMR、空著裁剪、期望窗口')
b('<b>評估</b>:材料、位置表(PST)、王接近度、通路兵、車開放線、兵形、機動力')
say('「每一層都讓搜尋更快或更準,相同時間內看得更深。」')

# ============================================================ 2 Negamax
sec("2. Negamax(負極大值)— Baseline")
block([
    Paragraph("零和賽局用單一視角:每個節點取最大,並對子節點取負,一條公式處理雙方。", styles["body"]),
    Preformatted("if (game_state == WIN)\n"
                 "    return P_MAX - ply;        // 越快將死越好\n"
                 "int best = M_MAX;\n"
                 "for (auto& a : legal_actions) {\n"
                 "    State* nx = next_state(a);\n"
                 "    int score = -eval_ctx(nx, depth-1, ...);  // 取負\n"
                 "    if (score > best) best = score;           // 取最大\n"
                 "}\n"
                 "return best;", styles["code"]),
])
b('正分 = 對走子方有利;<b>score = −eval_ctx(child)</b> 是核心。')
b('終端:可吃王回傳 <b>P_MAX − ply</b>(減 ply 讓引擎偏好更快的將死)。')
qa("為什麼用 negamax 而不分開寫 min/max?",
   "零和下一方得分=另一方失分,取負即可統一兩方,程式更簡潔、較不易出錯。")

# ============================================================ 3 Alpha-Beta
sec("3. Alpha-Beta 剪枝 — Baseline")
block([
    Paragraph("用 [α, β] 窗口剪掉對手不會允許的分枝。", styles["body"]),
    Preformatted("for (auto& a : moves) {        // 已用 MVV-LVA 排序\n"
                 "  int score = -eval_ctx(nx, d-1, -beta, -alpha, ...);\n"
                 "  if (score > best)  best  = score;\n"
                 "  if (score > alpha) alpha = score;\n"
                 "  if (alpha >= beta) break;    // ← beta cutoff(關鍵)\n"
                 "}", styles["code"]),
])
b('<b>α</b>:我方已保證的下限;<b>β</b>:對手會容許的上限。')
b('<b>score ≥ β</b> → 對手不會走到此局面 → 剪枝,結果與完整 minimax 相同。')
b('最佳排序下複雜度約 <b>O(b^(d/2))</b>,等於相同時間深度可加倍。')
qa("為什麼剪枝不會改變結果?",
   "被剪掉的分枝對手本來就不會選(已 ≥ β),所以不影響根節點的最佳值。")

# ============================================================ 4 PVS
sec("4. PVS 主變例搜尋 — 實際對局用")
block([
    Paragraph("假設(排序好時)第一手最好:先算主變例,其餘手用零寬窗口快篩。", styles["body"]),
    Preformatted("if (first) {                  // 主變例:完整窗口\n"
                 "    score = -eval(nx, d-1, -beta, -alpha);\n"
                 "} else {                      // 其餘:零寬窗口\n"
                 "    score = -eval(nx, d-1, -alpha-1, -alpha);\n"
                 "    if (score > alpha && score < beta)   // 意外更優\n"
                 "        score = -eval(nx, d-1, -beta, -alpha);  // 重搜\n"
                 "}", styles["code"]),
])
b('<b>零寬窗口 [α, α+1]</b>:只做「是否比 PV 更好」的布林測試,快很多。')
b('排序良好時重搜很少,因此比一般 alpha-beta 更快。')
qa("零寬窗口是什麼?為什麼快?",
   "把搜尋窗口縮到最小,只判斷該手是否超過 α;不必算出精確分數,能更早 cutoff。")

# ============================================================ 5 走法排序
sec("5. 走法排序(讓剪枝有效)")
block([
    Paragraph("好棋先試,才會更早 cutoff。順序:<b>置換表手 → 吃子 → 殺手 → 安靜手</b>。", styles["body"]),
    Preformatted("// 吃子:MVV-LVA;殺手:-1;安靜:-10\n"
                 "int as = av ? (piece_val[av]*10 - piece_val[aa])\n"
                 "            : (a_kill ? -1 : -10);", styles["code"]),
])
b('<b>MVV-LVA</b>:吃最大子、用最小子吃(victim 大、attacker 小者優先)。')
b('<b>殺手走法</b>:每層保留 2 個曾造成 cutoff 的安靜手,優先嘗試。')
qa("走法排序為何重要?",
   "alpha-beta 的效率完全取決於好棋是否先試;先試好棋 → 更早 cutoff → 同時間看更深。")

# ============================================================ 6 靜止搜尋
sec("6. 靜止搜尋(Quiescence)")
block([
    Paragraph("葉節點只追吃子到「安靜」才評估,避免視界效應。", styles["body"]),
    Preformatted("int stand_pat = evaluate(...);\n"
                 "if (stand_pat >= beta) return beta;   // 不吃也夠好\n"
                 "if (stand_pat > alpha) alpha = stand_pat;\n"
                 "// delta 剪枝:吃了也拉不回 alpha 就跳過\n"
                 "if (stand_pat + capture_val[cap] + 50 < alpha) continue;", styles["code"]),
])
b('<b>視界效應</b>:在深度 0 直接評估可能剛好停在被吃子前而誤判佔優。')
b('<b>stand-pat</b>=「不吃」的基準;<b>delta pruning</b> 跳過無望吃子。上限 MAX_QDEPTH=8。')
qa("靜止搜尋解決什麼問題?",
   "讓葉節點在戰術上穩定——不會因為剛好停在交換中間而高估或低估。")

# ============================================================ 7 置換表
sec("7. 置換表 與 Zobrist 雜湊")
block([
    Paragraph("同一局面常被不同走序到達;以 100 萬筆雜湊表快取結果。", styles["body"]),
    Preformatted("TTEntry& e = g_tt[key & TT_MASK];          // 探測\n"
                 "if (e.key == key && e.depth >= depth) {\n"
                 "    if (e.flag == TT_EXACT) return e.score;\n"
                 "}\n"
                 "h ^= zob[p][orig][from];   // Zobrist 移出(走子時增量)\n"
                 "h ^= zob[p][moved][to];    // Zobrist 移入", styles["code"]),
])
b('每(子,格,顏色)對應一隨機 64-bit 數,盤面雜湊=各子 XOR;走子 O(1) 增量更新。')
b('存 key(驗證碰撞)、深度、分數、界限(精確/下界/上界)、最佳走法;<b>深度優先替換</b>。')
b('將死分數依距根步數 ply 正規化(存入加 ply、取出減 ply)。')
qa("怎麼處理雜湊碰撞與將死分數?",
   "entry 內存完整 key,取用前比對;將死分數做 ply 正規化,避免不同深度取用造成步數錯亂。")

# ============================================================ 8 選擇性深度
sec("8. 選擇性深度(延伸 / 縮減 / 裁剪)")
block([
    Preformatted("int ext = (ply<20 && nx->is_king_attacked()) ? 1 : 0; // 將軍延伸\n"
                 "if (!ext && depth>=3 && move_index>=3 && !capture)    // LMR\n"
                 "    reduction = (depth>=6) ? 2 : 1;\n"
                 "if (null_score >= beta) return beta;                  // 空著裁剪", styles["code"]),
])
b('<b>將軍延伸</b>:被將軍的著法多搜 1 層,把強制應對算清楚。')
b('<b>LMR</b>:排序後段、非吃子、非將軍的手先搜淺;意外 >α 才以完整深度重搜。')
b('<b>空著裁剪</b>:跳一手仍 ≥ β 就剪枝;接近將死、低子力、被將軍時跳過。')
b('<b>期望窗口</b>:用上一層分數 ±50 的窄窗口,提高剪枝率。')
qa("LMR / 空著裁剪有風險嗎?",
   "都是啟發式可能漏算戰術;故 LMR 限定後段安靜手且會重搜,空著裁剪在危險局面跳過,以控制風險。")

# ============================================================ 9 評估函數
sec("9. 評估函數(為局面打分)")
block([
    Paragraph("分數 = 我方總分 − 對方總分(走子方視角,正=有利)。", styles["body"]),
    Preformatted("self_score += kp_material[piece];        // 材料\n"
                 "self_score += pst[piece-1][row][col];    // 位置表 PST\n"
                 "self_score += king_tropism(...);         // 王接近度\n"
                 "//  + 通路兵 / 車開放線 / 兵形罰分\n"
                 "bonus += 3 * (self_mob - oppn_mob);      // 機動力\n"
                 "return self_score - oppn_score + bonus;", styles["code"]),
])
b('材料:兵20 馬60 象65 車100 后200(比例符合官方 100 步判定:兵1 車5 馬3 象3 后9)。')
b('位置表、王接近度、通路兵、車開放線、兵形(疊兵−15/孤兵−10)、機動力(步數差×3)。')
qa("100 步判定如何影響策略?",
   "100 步內未吃王則比子力,故長局需確保子力領先;引擎材料權重比例與官方判定一致。")

# ============================================================ 10 關鍵修復
sec("10. 關鍵修復:根節點存入置換表(重點!)")
block([
    Paragraph('<font name="JHB" color="#B23A2E">問題:</font>原本 PVS::search 每層都重新用吃子排序根節點,'
              '把「會輸的吃子」排第一;測試程式時間一到就 kill 引擎並取「最後回報的走法」,'
              '深層被中斷時就走出敗著(開局第 7 手被將死)。固定深度本來都正確,只有限時會出錯。',
              styles["body"]),
    Preformatted("// 每層搜尋結束:把根節點最佳手寫入 TT\n"
                 "if (best_move != Move{} && !ctx.stop) {\n"
                 "    TTEntry& rt = g_tt[root_key & TT_MASK];\n"
                 "    rt.key = root_key;\n"
                 "    rt.best_move = pack_move(best_move);\n"
                 "    rt.depth = depth;  rt.flag = TT_EXACT;\n"
                 "}", styles["code"]),
    Paragraph('<font name="JHB" color="#1F7A45">解法/結果:</font>每層先搜、先回報真正最佳手,'
              '即使被中斷也是合理走法。<b>Weak 40% → 100%</b>,Strong、Boss 同步提升。', styles["body"]),
])
say("「我最大的優化是把根節點最佳手存進置換表。因為比賽是限時、時間到就抓最後回報的手;"
    "舊版排序會先報一個會輸的吃子,所以會被將死。修好之後每一步都穩定,三個對手都 10:0。」")

# ============================================================ 11 成果
sec("11. 成果(每步 2000ms、各 10 局、雙色輪流)")
data = [["對手", "修正前", "修正後"],
        ["Weak", "40%", "100%(10:0)"],
        ["Strong", "約 90%", "100%(10:0)"],
        ["Boss", "約 70%", "100%(10:0)"]]
t = Table(data, colWidths=[5 * cm, 5 * cm, 6 * cm])
t.setStyle(TableStyle([
    ("FONT", (0, 0), (-1, -1), "JH", 9.5),
    ("FONT", (0, 0), (-1, 0), "JHB", 9.5),
    ("BACKGROUND", (0, 0), (-1, 0), NAVY),
    ("TEXTCOLOR", (0, 0), (-1, 0), white),
    ("GRID", (0, 0), (-1, -1), 0.5, GREY),
    ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
    ("TOPPADDING", (0, 0), (-1, -1), 4),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ("LEFTPADDING", (0, 0), (-1, -1), 8),
]))
story.append(t)
sp(4)

# ============================================================ 12 Q&A 快查
sec("12. TA 問答快查(濃縮)")
extra = [
    ("三種演算法差在哪?", "MiniMax 無剪枝(對照);AlphaBeta 加窗口剪枝;PVS 再加零寬窗口、置換表、靜止搜尋等,實際對局用 PVS。"),
    ("時間如何控制?會超時判負嗎?", "迭代加深,每層檢查 total×2≥movetime 就停;測試程式 kill 後取最後回報手,故非超時即判負,真正問題是根節點排序(已修)。"),
    ("為什麼用 Zobrist 而非整個盤面當鍵?", "Zobrist 可 O(1) 增量更新且分布均勻,適合當雜湊鍵;存整個盤面又慢又佔空間。"),
    ("迭代加深不是重複算嗎?會不會浪費?", "淺層成本相對很小,且其結果(置換表 + 最佳手排序)讓深層大幅加速,整體反而更快、又能隨時中斷取最佳手。"),
    ("評估函數的分數單位?", "以兵≈20 為基準的內部分;正=走子方有利。將死用 P_MAX(±100000)表示,遠大於一般評估值。"),
]
for q, a in extra:
    qa(q, a)

# ============================================================ 13 參數速查
sec("13. 關鍵參數速查")
data2 = [["參數", "值 / 條件"],
         ["P_MAX / M_MAX", "+100000 / −100000"],
         ["置換表", "TT_BITS=20 → 約 100 萬筆"],
         ["靜止搜尋上限", "MAX_QDEPTH = 8"],
         ["材料(評估)", "兵20 馬60 象65 車100 后200"],
         ["LMR", "depth≥3 且 第3手後 且 非吃子非將軍 → 減1(depth≥6 減2)"],
         ["空著裁剪", "depth≥3、beta<P_MAX−100、非兵子力≥2;R=3(depth≥6 為4)"],
         ["將軍延伸", "ply<20 且該手使對方被將軍 → +1 層"],
         ["期望窗口", "±50,失敗 ×3 放寬"],
         ["兵形罰分", "疊兵 −15/個、孤兵 −10"]]
t2 = Table(data2, colWidths=[4.2 * cm, 11.8 * cm])
t2.setStyle(TableStyle([
    ("FONT", (0, 0), (-1, -1), "JH", 9),
    ("FONT", (0, 0), (-1, 0), "JHB", 9),
    ("BACKGROUND", (0, 0), (-1, 0), NAVY),
    ("TEXTCOLOR", (0, 0), (-1, 0), white),
    ("GRID", (0, 0), (-1, -1), 0.5, GREY),
    ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
    ("TOPPADDING", (0, 0), (-1, -1), 3),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ("LEFTPADDING", (0, 0), (-1, -1), 6),
]))
story.append(t2)

doc = SimpleDocTemplate("MiniChess_AI_Demo速查.pdf", pagesize=A4,
                        leftMargin=1.4 * cm, rightMargin=1.4 * cm,
                        topMargin=1.2 * cm, bottomMargin=1.2 * cm,
                        title="MiniChess AI Demo 速查")
doc.build(story)
print("saved MiniChess_AI_Demo速查.pdf, pages:", doc.page)
