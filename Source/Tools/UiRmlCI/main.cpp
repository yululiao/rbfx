//
// Standalone round-trip CI for the RmlUi layout editor's text model. Pure std + the
// DOM-free editor-model units, no engine code, no CMake target: build with cl.exe
// (see run.ps1) and it exits non-zero on any failure.
//
// Guarantees asserted:
//   1. Lossless identity: load(every sample .rml/.rcss) -> GetText() == original bytes.
//   2. Index invariants: spans in range, children ordered/non-overlapping inside parents.
//   3. Bindings/templates/comments survive untouched (identity covers them; plus explicit check).
//   4. Localization: a targeted style edit changes only the targeted bytes; siblings intact.
//   5. Generation: MakeElement + InsertElement add standard RML without disturbing siblings.
//   6. Model round-trip: BuildTreeFromText + EmitRml reproduce every sample .rml byte-for-byte.
//   7. Minimal diff: a targeted model edit (style value / id) changes ONE confined window
//      inside the target's open tag; head, comments, bindings and siblings stay untouched.
//   8. Style batch coalescing (historical regressions): a multi-add on a style-less element
//      mints ONE style attribute; duplicate style attributes self-heal on rewrite; clearing
//      removes the attribute outright (no style="" husk).
//   9. Head <link> commands: insert/edit/remove keep the head structure and leave the body
//      bytes untouched.
//  10. Attribute fidelity: valueless attributes (disabled) and authored empty values
//      (foo="") survive an untouched round-trip; only an actually-cleared valued
//      attribute is removed.
//  11. Separator hygiene: a declaration appended after an authored trailing ';'
//      reuses it (never mints ';;'); remove-last + add-new in one save coalesces
//      into one clean whole-value rewrite.
//

#include "RmlTextModel.h"
#include "UIViewDocumentModel.h"

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

ea::string ToEa(const std::string& s)
{
    return ea::string(s.c_str(), s.length());
}

std::string ToStd(const ea::string& s)
{
    return std::string(s.c_str(), s.length());
}

int CountOf(const std::string& hay, const std::string& needle)
{
    int n = 0;
    size_t p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos)
    {
        ++n;
        p += needle.size();
    }
    return n;
}

bool HasBodyTag(const std::string& text)
{
    size_t p = 0;
    while ((p = text.find("<body", p)) != std::string::npos)
    {
        const char c = p + 5 < text.size() ? text[p + 5] : '\0';
        if (c == '\0' || c == '>' || c == ' ' || c == '/' || c == '\t' || c == '\r' || c == '\n')
            return true;
        p += 5;
    }
    return false;
}

Urho3D::UiNode* FindNodeByTagId(Urho3D::UiNode* node, const char* tag, const char* id)
{
    if (node->tag_ == tag && node->id_ == id)
        return node;
    for (const auto& child : node->children_)
        if (Urho3D::UiNode* found = FindNodeByTagId(child.Get(), tag, id))
            return found;
    return nullptr;
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

// Full editor-model round-trip (guarantee 6): tree build + save-time reconcile over the
// untouched spine must reproduce the document byte-for-byte. Files without a <body>
// (template fragments) have no editor tree and are skipped - the spine identity test
// above already covers them.
void VerifyModelRoundTrip(const std::string& path, const std::string& text)
{
    Urho3D::UiDocumentModel model;
    if (!model.BuildTreeFromText(ToEa(text)))
    {
        Check(!HasBodyTag(text), "model build succeeded (or no <body>): " + path);
        return;
    }
    Check(ToStd(model.EmitRml()) == text, "model round-trip: " + path);
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
        {
            VerifyInvariants(p, text);
            VerifyModelRoundTrip(p, text);
        }
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

// Targeted model edits must stay surgical (guarantees 7): the emitted diff is ONE hunk
// confined to the edited node's open tag, and every zone the editor does not understand
// (head, comments, {{bindings}}, sibling markup, whitespace) is preserved byte-for-byte.
void TestModelTargetedEdit()
{
    std::cout << "TestModelTargetedEdit\n";
    const std::string doc =
        "<rml>\n"
        "<head>\n"
        "  <link type=\"text/rcss\" href=\"theme.rcss\"/>\n"
        "  <style>body { top: 0px; }</style>\n"
        "</head>\n"
        "<body>\n"
        "  <!-- keep me -->\n"
        "  <div id=\"target\" class=\"box\" style=\"width: 10px; height: 20px\">old</div>\n"
        "  <span>{{counter}}</span>\n"
        "</body>\n"
        "</rml>\n";
    const std::string before = doc;
    const size_t bodyAt = before.find("<body");

    // Locate the div's open tag in the ORIGINAL text (all change windows are measured there).
    RmlTextModel locate;
    locate.Load(before);
    const int divIdx = locate.FindFirstElement("div");
    Check(divIdx > 0, "located div in spine");
    const int openLo = locate.Node(divIdx).openTag.offset;
    const int openHi = locate.Node(divIdx).openTag.End();

    // Style value edit.
    Urho3D::UiDocumentModel model;
    Check(model.BuildTreeFromText(ToEa(before)), "targeted-edit doc builds");
    Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "target");
    Check(div != nullptr, "found target div");
    div->SetStyle("width", "300px");
    std::string after = ToStd(model.EmitRml());
    Check(after.find("width: 300px") != std::string::npos, "style value changed");
    Check(after.find("height: 20px") != std::string::npos, "sibling declaration kept");
    Check(ChangeConfined(before, after, openLo, openHi), "style edit confined to open tag");
    Check(CountOf(after, "style=") == 1, "exactly one style attribute");

    // Id edit on a pristine rebuild.
    Check(model.BuildTreeFromText(ToEa(before)), "pristine rebuild");
    div = FindNodeByTagId(model.root_.Get(), "div", "target");
    Check(div != nullptr, "re-found target div");
    div->id_ = "renamed";
    after = ToStd(model.EmitRml());
    Check(after.find("id=\"renamed\"") != std::string::npos, "id renamed");
    Check(after.find("id=\"target\"") == std::string::npos, "old id gone");
    Check(ChangeConfined(before, after, openLo, openHi), "id edit confined to open tag");

    // Ignorant zones byte-preserved (ChangeConfined implies them; explicit checks give
    // readable failure messages).
    Check(after.substr(0, bodyAt) == before.substr(0, bodyAt), "head bytes untouched");
    Check(after.find("<!-- keep me -->") != std::string::npos, "comment preserved");
    Check(after.find("{{counter}}") != std::string::npos, "binding preserved");
    Check(after.find("<span>{{counter}}</span>") != std::string::npos, "sibling element untouched");
    Check(after.find(">old<") != std::string::npos, "text content untouched");
}

// Historical regression (guarantee 8): two style adds on a style-less element used to
// mint two whole style attributes at the same offset (`style="a" style="b"`). The
// reconcile must coalesce any multi-change style edit into ONE patch.
void TestStyleBatchSinglePatch()
{
    std::cout << "TestStyleBatchSinglePatch\n";
    const std::string doc = "<rml><body><div id=\"a\"></div></body></rml>";
    Urho3D::UiDocumentModel model;
    Check(model.BuildTreeFromText(ToEa(doc)), "batch doc builds");
    Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
    Check(div != nullptr, "found div");
    div->SetStyle("width", "10px");
    div->SetStyle("height", "20px");
    const std::string after = ToStd(model.EmitRml());
    Check(CountOf(after, "style=") == 1, "exactly one style attribute");
    Check(after.find("style=\"width: 10px; height: 20px\"") != std::string::npos,
        "declarations merged with '; ' separator");
}

// Historical regression (guarantee 8): documents saved by older builds carry duplicate
// style attributes. Unedited, they round-trip verbatim; any style rewrite must collapse
// them back to one attribute carrying the model's declarations.
void TestStyleSelfHeal()
{
    std::cout << "TestStyleSelfHeal\n";
    const std::string doc =
        "<rml><body><div id=\"a\" style=\"width: 10px\" style=\"height: 20px\">x</div></body></rml>";
    Urho3D::UiDocumentModel model;
    Check(model.BuildTreeFromText(ToEa(doc)), "self-heal doc builds");
    Check(ToStd(model.EmitRml()) == doc, "unedited damaged doc preserved verbatim");
    Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
    Check(div != nullptr, "found div");
    div->SetStyle("width", "30px");
    const std::string after = ToStd(model.EmitRml());
    Check(CountOf(after, "style=") == 1, "duplicate style collapsed to one");
    Check(after.find("width: 30px") != std::string::npos, "edited value written");
    Check(after.find("height: 20px") == std::string::npos, "second attribute dropped");
}

// Historical regression (guarantee 8): clearing style used to leave a style="" husk
// that re-seeded the no-separator merge bug on the next multi-add. The attribute must
// vanish outright.
void TestStyleClearRemovesAttribute()
{
    std::cout << "TestStyleClearRemovesAttribute\n";
    const std::string doc = "<rml><body><div id=\"a\" style=\"width: 10px\">x</div></body></rml>";
    Urho3D::UiDocumentModel model;
    Check(model.BuildTreeFromText(ToEa(doc)), "clear doc builds");
    Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
    Check(div != nullptr, "found div");
    div->RemoveStyle("width");
    const std::string after = ToStd(model.EmitRml());
    Check(after.find("style=") == std::string::npos, "style attribute removed outright (no husk)");
    Check(after.find("<div id=\"a\">x</div>") != std::string::npos, "element otherwise intact");
}

// Historical regression (guarantee 10): valueless attributes (disabled, checked, ...)
// and authored empty values (foo="") load into the model as empty strings; the
// reconcile used to read that state as "the value was cleared" and DELETE the
// attribute on save - opening and saving HelloRmlUI.rml silently re-enabled its
// disabled inputs.
void TestValuelessAttributeRoundTrip()
{
    std::cout << "TestValuelessAttributeRoundTrip\n";
    const std::string doc =
        "<rml>\n<body>\n  <input type=\"submit\" disabled>Go</input>\n"
        "  <div id=\"a\" foo=\"\">x</div>\n</body>\n</rml>\n";
    Urho3D::UiDocumentModel model;
    Check(model.BuildTreeFromText(ToEa(doc)), "valueless doc builds");
    Check(ToStd(model.EmitRml()) == doc, "valueless + empty-value attributes preserved verbatim");

    // Clearing a VALUED attribute in the editor still removes it outright.
    const std::string doc2 = "<rml><body><div id=\"a\" foo=\"bar\">x</div></body></rml>";
    Check(model.BuildTreeFromText(ToEa(doc2)), "valued doc builds");
    Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
    Check(div != nullptr, "found div");
    for (auto& attr : div->attributes_)
    {
        if (attr.first == "foo")
            attr.second = "";
    }
    const std::string after = ToStd(model.EmitRml());
    Check(after.find("foo=") == std::string::npos, "cleared valued attribute removed");
}

// Separator hygiene (guarantee 11, the P1-4 corners). The originally recorded
// combo - remove the LAST declaration and add a new one in one save - used to
// leave a stray ';' behind; the style coalescing rewrote that into one clean
// patch. Its sibling stayed alive until now: a single append onto a style whose
// authored value ends with ';' (style="height: 100dp;" - HelloRmlUI.rml carries
// exactly this shape) minted 'height: 100dp;; width: 10px'.
void TestStyleSemicolonHygiene()
{
    std::cout << "TestStyleSemicolonHygiene\n";
    {
        const std::string doc =
            "<rml><body><div id=\"a\" style=\"width: 10px; height: 20px\">x</div></body></rml>";
        Urho3D::UiDocumentModel model;
        Check(model.BuildTreeFromText(ToEa(doc)), "hygiene doc builds");
        Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
        Check(div != nullptr, "found div");
        div->RemoveStyle("height");
        div->SetStyle("color", "red");
        const std::string after = ToStd(model.EmitRml());
        Check(CountOf(after, "style=") == 1, "one style attribute");
        Check(after.find("style=\"width: 10px; color: red\"") != std::string::npos,
            "remove-last + add-new rewrites cleanly");
        Check(after.find(";;") == std::string::npos, "no double separator");
        Check(after.find("; \"") == std::string::npos, "no separator before closing quote");
    }
    {
        const std::string doc =
            "<rml><body><div id=\"a\" style=\"height: 100dp;\">x</div></body></rml>";
        Urho3D::UiDocumentModel model;
        Check(model.BuildTreeFromText(ToEa(doc)), "trailing-semicolon doc builds");
        Check(ToStd(model.EmitRml()) == doc, "authored trailing ';' preserved unedited");
        Urho3D::UiNode* div = FindNodeByTagId(model.root_.Get(), "div", "a");
        Check(div != nullptr, "found div");
        div->SetStyle("width", "10px");
        const std::string after = ToStd(model.EmitRml());
        Check(after.find(";;") == std::string::npos, "single append never mints ';;'");
        Check(after.find("style=\"height: 100dp; width: 10px\"") != std::string::npos,
            "appended after the authored ';'");
    }
}

// Head <link> commands (guarantee 9): the head is not part of the editor tree, so these
// edit a throwaway spine parse. Insert lands after the last link, edit rewrites exactly
// the addressed link, remove takes the whole authored line - and the body bytes never move.
void TestHeadLinkCommands()
{
    std::cout << "TestHeadLinkCommands\n";
    const std::string doc =
        "<rml>\n<head>\n  <link type=\"text/rcss\" href=\"a.rcss\"/>\n"
        "  <link type=\"text/rcss\" href=\"b.rcss\"/>\n</head>\n<body>\n</body>\n</rml>\n";
    const std::string bodyTail = doc.substr(doc.find("<body"));
    Urho3D::UiDocumentModel cmd;
    ea::string out;

    // Insert: third link lands after the last existing one; body bytes untouched.
    Check(cmd.InsertHeadLink(ToEa(doc), "text/rcss", "c.rcss", out), "insert link");
    const std::string inserted = ToStd(out);
    Check(CountOf(inserted, "<link ") == 3, "head now has three links");
    Check(inserted.find("<link type=\"text/rcss\" href=\"c.rcss\"/>") != std::string::npos,
        "new link markup present");
    Check(inserted.substr(inserted.find("<body")) == bodyTail, "body bytes untouched by insert");
    {
        RmlTextModel m;
        m.Load(inserted);
        const int head = m.FindFirstElement("head");
        std::vector<std::string> hrefs;
        for (int c : m.Node(head).children)
        {
            const RmlNode& ch = m.Node(c);
            if (ch.kind != RmlNodeKind::Element || ch.tag != "link")
                continue;
            for (const RmlAttribute& a : ch.attributes)
                if (a.name == "href")
                    hrefs.push_back(a.value);
        }
        Check(hrefs.size() == 3 && hrefs[0] == "a.rcss" && hrefs[1] == "b.rcss" && hrefs[2] == "c.rcss",
            "inserted after the last existing link");
    }
    Check(!cmd.InsertHeadLink(ToEa(inserted), "text/rcss", "c.rcss", out), "duplicate insert rejected");
    Check(!cmd.InsertHeadLink(ToEa(doc), "text/rcss", "", out), "empty href rejected");

    // Edit: rewrite the first link's type/href in place; the rest stays put.
    Check(cmd.EditHeadLinkAt(ToEa(doc), 0, "text/template", "t.rml", out), "edit link");
    Check(ToStd(out) ==
        "<rml>\n<head>\n  <link type=\"text/template\" href=\"t.rml\"/>\n"
        "  <link type=\"text/rcss\" href=\"b.rcss\"/>\n</head>\n<body>\n</body>\n</rml>\n",
        "edit rewrites exactly the first link");
    Check(!cmd.EditHeadLinkAt(ToEa(doc), 9, "text/rcss", "x.rcss", out), "stale ordinal rejected");

    // Remove: the whole authored line (indent + newline) goes away.
    Check(cmd.RemoveHeadLinkAt(ToEa(doc), 1, out), "remove link");
    const std::string afterRemove = ToStd(out);
    Check(afterRemove ==
        "<rml>\n<head>\n  <link type=\"text/rcss\" href=\"a.rcss\"/>\n</head>\n<body>\n</body>\n</rml>\n",
        "removed the whole authored line");
    Check(cmd.RemoveHeadLinkAt(ToEa(afterRemove), 0, out), "remove last link");
    const std::string emptyHead = ToStd(out);
    Check(emptyHead == "<rml>\n<head>\n</head>\n<body>\n</body>\n</rml>\n",
        "empty head left clean (no blank line)");
    Check(!cmd.RemoveHeadLinkAt(ToEa(emptyHead), 0, out), "remove on empty head rejected");
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
    TestModelTargetedEdit();
    TestStyleBatchSinglePatch();
    TestStyleSelfHeal();
    TestStyleClearRemovesAttribute();
    TestValuelessAttributeRoundTrip();
    TestStyleSemicolonHygiene();
    TestHeadLinkCommands();

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
