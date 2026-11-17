#pragma once
// prism.document(): a text::Document as a model field (doc/design/text-stack.md). Reads and edits
// are dispatched to the logic thread like a list's; vb.text_editor / code_editor / text_view show it.

#include "common.hpp"
#include "runtime.hpp"
#include "bound_handles.hpp"

#include <prism/text/find.hpp>
#include <prism/text/to_markdown.hpp>
#ifdef PRISM_PYTHON_MARKDOWN
#include <prism/text/markdown.hpp>
#endif

#include <string>
#include <utility>
#include <vector>

// The document a model owns. Without a view(), it shows as a text editor.
struct SlotDocument : SlotBase {
    prism::text::Document doc;
    explicit SlotDocument(prism::text::Document d) : doc(std::move(d)) {}
    void build(ViewBuilder& vb) override { vb.text_editor(doc); }
};

inline prism::text::Document document_from(const std::string& text, bool markdown) {
#ifdef PRISM_PYTHON_MARKDOWN
    if (markdown) return prism::text::markdown(text);
#else
    if (markdown) throw nb::value_error("document(markdown=True): this build has no prism-markdown");
#endif
    return prism::text::Document{text};
}

struct BoundDocument {
    std::shared_ptr<SlotBase> owner;
    prism::text::Document* doc = nullptr;

    prism::text::Document& bound() const {
        if (!doc) throw nb::value_error("document handle is not bound to a Model");
        return *doc;
    }
    std::string text() const {
        auto* d = &bound();
        return dispatch_sync_read<std::string>([d] { return d->text(); });
    }
    // A load, not an edit: no undo back to the text before.
    void set_text(std::string text, bool markdown) {
        auto* d = &bound();
        auto replacement = std::make_shared<prism::text::Document>(document_from(text, markdown));
        list_op_dispatch(owner, [d, replacement] { d->reset(std::move(*replacement)); });
    }
    std::string to_markdown() const {
        auto* d = &bound();
        return dispatch_sync_read<std::string>([d] { return prism::text::to_markdown(*d); });
    }
    // Byte offsets (start, end) of every match, as Python sees the text.
    std::vector<std::pair<std::size_t, std::size_t>> find_all(std::string needle, bool case_sensitive, bool whole_word) const {
        auto* d = &bound();
        return dispatch_sync_read<std::vector<std::pair<std::size_t, std::size_t>>>([d, needle, case_sensitive, whole_word] {
            std::vector<std::pair<std::size_t, std::size_t>> out;
            for (const auto& r : prism::text::find_all(*d, needle, {case_sensitive, whole_word}))
                out.emplace_back(d->offset_of(r.begin), d->offset_of(r.end));
            return out;
        });
    }
    std::size_t replace_all(std::string needle, std::string with, bool case_sensitive, bool whole_word) {
        auto* d = &bound();
        return dispatch_sync_read<std::size_t>([d, needle, with, case_sensitive, whole_word] {
            return prism::text::replace_all(*d, needle, with, {case_sensitive, whole_word});
        });
    }
    Connection observe(nb::callable cb) {
        auto conn = bound().on_change().connect([w = WeakCallback(cb)](const prism::text::Change&) { w(); });
        if (owner) conn.keep_alive(owner);
        return conn;
    }
};

inline BoundDocument& as_document(nb::object h, const char* what) {
    if (!nb::isinstance<BoundDocument>(h))
        throw nb::type_error((std::string(what) + ": expected a document field (prism.document())").c_str());
    return nb::cast<BoundDocument&>(h);
}
