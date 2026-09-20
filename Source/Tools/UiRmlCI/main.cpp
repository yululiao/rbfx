//
// Standalone round-trip CI for the RmlTextModel. Pure std, no engine, no CMake target:
// build with cl.exe (see run.ps1) and it exits non-zero on any failure.
//
// Guarantees asserted:
//   1. Lossless identity: load(every sample .rml/.rcss) -> GetText() == original bytes.
//   2. Index invariants: spans in range, children ordered/non-overlapping inside parents.
//   3. Bindings/templates/comments survive untouched (identity covers them; plus explicit check).
//   4. Localization: a targeted style edit changes only the targeted bytes; siblings intact.
//   5. Generation: MakeElement + InsertElement add standard RML without disturbing siblings.
//

#include "RmlTextModel.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what)
{
    ++g_checks;
    if (!cond)
    {
        ++g_failures;
        std::cout << "  [FAIL] " << what << "\n";
    }
}

bool ReadFile(const std::filesystem::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    // Strip UTF-8 BOM for identity comparison bookkeeping (kept out of the model).
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

// Compute the [changedStart, changedEndOld/changedEndNew) window between two strings via common
// prefix/suffix. Returns true if the change is confined to a window within [lo, hi) of `a`.
bool ChangeConfined(const std::string& a, const std::string& b, int lo, int hi)
{
    int n = static_cast<int>(std::min(a.size(), b.size()));
    int p = 0;
    while (p < n && a[p] == b[p])
        ++p;
    int sa = static_cast<int>(a.size()) - 1, sb = static_cast<int>(b.size()) - 1;
    while (sa >= p && sb >= p && a[sa] == b[sb])
    {
        --sa;
        --sb;
    }
    // Old changed region is a[p..sa].
    int oldStart = p, oldEnd = sa + 1;
    return oldStart >= lo && oldEnd <= hi;
}

void VerifyIdentity(const std::string& path, const std::string& text)
{
    RmlTextModel m;
    m.Load(text);
    Check(m.GetText() == text, "identity round-trip: " + path);
}

void VerifyInvariants(const std::string& path, const std::string& text)
{
    RmlTextModel m;
    m.Load(text);
    const int n = static_cast<int>(m.GetText().size());
    bool ok = true;
    for (int i = 0; i < m.NodeCount(); ++i)
    {
        const RmlNode& node = m.Node(i);
        auto inRange = [&](const RmlSpan& s) { return s.offset >= 0 && s.length >= 0 && s.End() <= n; };
        if (node.kind == RmlNodeKind::Element)
        {
            if (!inRange(node.whole) || !inRange(node.openTag) || !inRange(node.nameSpan) || !inRange(node.innerSpan))
                ok = false;
            for (const RmlAttribute& a : node.attributes)
            {
                if (!inRange(a.whole) || !inRange(a.valueSpan) || a.whole.End() > node.openTag.End())
                    ok = false;
                for (const RmlStyleDecl& d : a.styleDecls)
                    if (!inRange(d.whole) || !inRange(d.valueSpan))
                        ok = false;
            }
            // Children ordered + non-overlapping + inside the element.
            int prevEnd = node.whole.offset;
            for (int c : node.children)
            {
                const RmlNode& ch = m.Node(c);
                const RmlSpan& s = (ch.kind == RmlNodeKind::Element) ? ch.whole : ch.span;
                if (s.offset < prevEnd || s.End() > node.whole.End() || !inRange(s))
                    ok = false;
                prevEnd = s.End();
            }
        }
        else if (node.kind != RmlNodeKind::Root)
        {
            if (!inRange(node.span))
                ok = false;
        }
    }
    Check(ok, "index invariants: " + path);
}

std::string LowerExt(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void WalkSamples(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        std::cout << "  (skip, missing: " << root.string() << ")\n";
        return;
    }
    int count = 0;
    for (auto& de : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (!de.is_regular_file())
            continue;
        std::string ext = LowerExt(de.path().extension().string());
        if (ext != ".rml" && ext != ".rcss" && ext != ".xml" && ext != ".rcss")
            continue;
        std::string text;
        if (!ReadFile(de.path(), text))
            continue;
        const std::string p = de.path().string();
        VerifyIdentity(p, text);
        if (ext == ".rml" || ext == ".xml")
            VerifyInvariants(p, text);
        ++count;
    }
    std::cout << "  scanned " << count << " file(s) under " << root.string() << "\n";
}

void TestLocalization()
{
    std::cout << "TestLocalization\n";
    const std::string doc = "<rml><head></head><body><div id=\"a\" style=\"width: 10px; height: 20px\">x</div></body></rml>";
    RmlTextModel m;
    m.Load(doc);
    int div = m.FindFirstElement("div");
    Check(div > 0, "found div");
    std::string w;
    Check(m.GetStyleProperty(div, "width", w) && w == "10px", "width reads 10px");

    const int divStart = m.Node(div).whole.offset;
    const int divEnd = m.Node(div).whole.End();

    std::string before = m.GetText();
    Check(m.SetStyleProperty(div, "width", "300px"), "set width 300px");
    std::string after = m.GetText();
    // Re-resolve div (parse reloaded).
    div = m.FindFirstElement("div");
    std::string h, w2;
    Check(m.GetStyleProperty(div, "width", w2) && w2 == "300px", "width now 300px");
    Check(m.GetStyleProperty(div, "height", h) && h == "20px", "height untouched");
    Check(after.find(">x<") != std::string::npos, "text node intact");
    Check(after.find("id=\"a\"") != std::string::npos, "id attr intact");
    Check(ChangeConfined(before, after, divStart, divEnd + 10), "edit localized to div region");
}

void TestBindingPreservation()
{
    std::cout << "TestBindingPreservation\n";
    const std::string doc = "<rml>\n<body>\n  <span>{{counter}}</span>\n  <button data-model=\"{{__data_model_id}}\" "
                            "data-event-click=\"count\">Go</button>\n</body>\n</rml>";
    RmlTextModel m;
    m.Load(doc);
    Check(m.GetText() == doc, "bindings + token preserved on identity");
    // Editing an unrelated inline style on the button must not touch the bindings.
    int button = m.FindFirstElement("button");
    Check(button > 0, "found button");
    m.SetStyleProperty(button, "width", "42px");
    const std::string& t = m.GetText();
    Check(t.find("{{__data_model_id}}") != std::string::npos, "data-model token preserved after edit");
    Check(t.find("{{counter}}") != std::string::npos, "span binding preserved");
    Check(t.find("data-event-click=\"count\"") != std::string::npos, "data-event preserved");
}

void TestGenerationInsert()
{
    std::cout << "TestGenerationInsert\n";
    RmlTextModel m;
    m.Load("<rml><body>\n  <div></div>\n</body></rml>");
    int body = m.FindFirstElement("body");
    Check(body > 0, "found body");
    Check(m.ElementChildCount(body) == 1, "body has 1 element child");
    std::string markup = m.MakeElement("span", "gen", "cls", "position: absolute; left: 4px; top: 8px", false);
    Check(markup == "<span id=\"gen\" class=\"cls\" style=\"position: absolute; left: 4px; top: 8px\"></span>",
        "generated markup is standard RML");
    int before = m.ElementChildCount(body);
    (void)before;
    m.InsertElement(body, 0, markup);
    body = m.FindFirstElement("body");
    Check(m.ElementChildCount(body) == 2, "insert grew body to 2 element children");
    int first = m.ElementChildAt(body, 0);
    Check(first > 0 && m.Node(first).tag == "span", "inserted node is first child");
    std::string posv;
    Check(m.GetStyleProperty(first, "position", posv) && posv == "absolute", "generated style readable");
}

// Find the Text-leaf child index of an element node (its inner character data).
int FirstTextChild(const RmlTextModel& m, int el)
{
    for (int c : m.Node(el).children)
        if (m.Node(c).kind == RmlNodeKind::Text)
            return c;
    return -1;
}

void TestBatchPatches()
{
    std::cout << "TestBatchPatches\n";
    // Multiple logical edits computed against ONE parse, then applied together. This mirrors the
    // editor's save-time reconcile: every offset must stay consistent until the single apply.
    const std::string doc =
        "<rml>\n<body>\n  <div id=\"a\" style=\"width: 10px\">x</div>\n  <span class=\"s\">hi</span>\n</body>\n</rml>";
    RmlTextModel m;
    m.Load(doc);
    int body = m.FindFirstElement("body");
    int div = m.ElementChildAt(body, 0);
    int span = m.ElementChildAt(body, 1);
    int divText = FirstTextChild(m, div);
    Check(div > 0 && span > 0 && divText > 0, "resolved div/span/text handles");

    std::vector<RmlPatch> ps;
    RmlPatch p;
    Check(m.ComputeStylePropertyPatch(div, "width", "99px", p), "patch1 style value");
    ps.push_back(p);
    Check(m.ComputeStylePropertyPatch(span, "color", "red", p), "patch2 new style attr");
    ps.push_back(p);
    Check(m.ComputeTextPatch(divText, "X!", p), "patch3 text leaf");
    ps.push_back(p);
    std::string mk = m.MakeElement("button", "", "", "", false);
    Check(m.ComputeInsertPatch(body, m.ElementChildCount(body), mk, p), "patch4 append child");
    ps.push_back(p);

    // Nothing changed until apply (all four computed against the same, still-original parse).
    Check(m.GetText() == doc, "compute does not mutate");

    m.ApplyPatches(ps);
    const std::string& after = m.GetText();
    Check(after.find("width: 99px") != std::string::npos, "style value patched");
    Check(after.find("style=\"color: red\"") != std::string::npos, "new style attr created");
    Check(after.find(">X!<") != std::string::npos, "text patched");
    Check(after.find("<button></button>") != std::string::npos, "child inserted");
    Check(after.find("id=\"a\"") != std::string::npos, "div id preserved");
    Check(after.find("class=\"s\"") != std::string::npos, "span class preserved");
    Check(after.find("<rml>") == 0 && after.find("</body>") != std::string::npos, "document frame intact");

    // Post-apply re-query through the refreshed model.
    body = m.FindFirstElement("body");
    div = m.ElementChildAt(body, 0);
    span = m.ElementChildAt(body, 1);
    int btn = m.ElementChildAt(body, 2);
    std::string v;
    Check(m.GetStyleProperty(div, "width", v) && v == "99px", "width now 99px");
    Check(m.GetStyleProperty(span, "color", v) && v == "red", "span color red");
    Check(m.Node(btn).tag == "button", "button is third element child");
}

void TestStableSameOffset()
{
    std::cout << "TestStableSameOffset\n";
    // The editor's reconcile queues several zero-width inserts at a single offset when a node
    // gains id + class + style at once. ApplyPatches must keep them in queue order, not scramble.
    RmlTextModel m;
    m.Load("<rml><body><div></div></body></rml>");
    const int div = m.FindFirstElement("div");
    const int nameEnd = m.Node(div).nameSpan.End();
    std::vector<RmlPatch> ps;
    ps.push_back(RmlPatch{{nameEnd, 0}, " id=\"a\""});
    ps.push_back(RmlPatch{{nameEnd, 0}, " class=\"c\""});
    m.ApplyPatches(ps);
    Check(m.GetText().find("<div id=\"a\" class=\"c\">") != std::string::npos,
        "same-offset inserts keep queue order (id before class)");
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> roots;
    for (int i = 1; i < argc; ++i)
        roots.emplace_back(argv[i]);

    TestLocalization();
    TestBindingPreservation();
    TestGenerationInsert();
    TestBatchPatches();
    TestStableSameOffset();

    std::cout << "WalkSamples\n";
    for (const auto& r : roots)
        WalkSamples(std::filesystem::path(r));

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed.\n";
    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "RML ROUND-TRIP CI: PASS\n";
    return 0;
}
